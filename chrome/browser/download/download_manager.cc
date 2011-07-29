// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_manager.h"

#include "base/callback.h"
#include "base/file_util.h"
#include "base/i18n/case_conversion.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/stringprintf.h"
#include "base/sys_string_conversions.h"
#include "base/task.h"
#include "base/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_create_info.h"
#include "chrome/browser/download/download_extensions.h"
#include "chrome/browser/download/download_file_manager.h"
#include "chrome/browser/download/download_history.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/download/download_manager_delegate.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_request_handle.h"
#include "chrome/browser/download/download_safe_browsing_client.h"
#include "chrome/browser/download/download_status_updater.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/history/download_history_info.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "content/browser/browser_thread.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/resource_dispatcher_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/content_notification_types.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "net/base/mime_util.h"
#include "net/base/net_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

DownloadManager::DownloadManager(DownloadManagerDelegate* delegate,
                                 DownloadStatusUpdater* status_updater)
    : shutdown_needed_(false),
      profile_(NULL),
      file_manager_(NULL),
      status_updater_(status_updater->AsWeakPtr()),
      delegate_(delegate) {
  if (status_updater_)
    status_updater_->AddDelegate(this);
}

DownloadManager::~DownloadManager() {
  DCHECK(!shutdown_needed_);
  if (status_updater_)
    status_updater_->RemoveDelegate(this);
}

void DownloadManager::Shutdown() {
  VLOG(20) << __FUNCTION__ << "()"
           << " shutdown_needed_ = " << shutdown_needed_;
  if (!shutdown_needed_)
    return;
  shutdown_needed_ = false;

  FOR_EACH_OBSERVER(Observer, observers_, ManagerGoingDown());

  if (file_manager_) {
    BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
        NewRunnableMethod(file_manager_,
                          &DownloadFileManager::OnDownloadManagerShutdown,
                          make_scoped_refptr(this)));
  }

  AssertContainersConsistent();

  // Go through all downloads in downloads_.  Dangerous ones we need to
  // remove on disk, and in progress ones we need to cancel.
  for (DownloadSet::iterator it = downloads_.begin(); it != downloads_.end();) {
    DownloadItem* download = *it;

    // Save iterator from potential erases in this set done by called code.
    // Iterators after an erasure point are still valid for lists and
    // associative containers such as sets.
    it++;

    if (download->safety_state() == DownloadItem::DANGEROUS &&
        download->IsPartialDownload()) {
      // The user hasn't accepted it, so we need to remove it
      // from the disk.  This may or may not result in it being
      // removed from the DownloadManager queues and deleted
      // (specifically, DownloadManager::RemoveDownload only
      // removes and deletes it if it's known to the history service)
      // so the only thing we know after calling this function is that
      // the download was deleted if-and-only-if it was removed
      // from all queues.
      download->Delete(DownloadItem::DELETE_DUE_TO_BROWSER_SHUTDOWN);
    } else if (download->IsPartialDownload()) {
      download->Cancel(false);
      download_history_->UpdateEntry(download);
    }
  }

  // At this point, all dangerous downloads have had their files removed
  // and all in progress downloads have been cancelled.  We can now delete
  // anything left.

  // Copy downloads_ to separate container so as not to set off checks
  // in DownloadItem destruction.
  DownloadSet downloads_to_delete;
  downloads_to_delete.swap(downloads_);

  in_progress_.clear();
  active_downloads_.clear();
  history_downloads_.clear();
#if !defined(NDEBUG)
  save_page_as_downloads_.clear();
#endif
  STLDeleteElements(&downloads_to_delete);

  file_manager_ = NULL;

  download_history_.reset();
  download_prefs_.reset();

  shutdown_needed_ = false;
}

void DownloadManager::GetTemporaryDownloads(
    const FilePath& dir_path, std::vector<DownloadItem*>* result) {
  DCHECK(result);

  for (DownloadMap::iterator it = history_downloads_.begin();
       it != history_downloads_.end(); ++it) {
    if (it->second->is_temporary() &&
        it->second->full_path().DirName() == dir_path)
      result->push_back(it->second);
  }
}

void DownloadManager::GetAllDownloads(
    const FilePath& dir_path, std::vector<DownloadItem*>* result) {
  DCHECK(result);

  for (DownloadMap::iterator it = history_downloads_.begin();
       it != history_downloads_.end(); ++it) {
    if (!it->second->is_temporary() &&
        (dir_path.empty() || it->second->full_path().DirName() == dir_path))
      result->push_back(it->second);
  }
}

void DownloadManager::GetCurrentDownloads(
    const FilePath& dir_path, std::vector<DownloadItem*>* result) {
  DCHECK(result);

  for (DownloadMap::iterator it = history_downloads_.begin();
       it != history_downloads_.end(); ++it) {
    DownloadItem* item =it->second;
    // Skip temporary items.
    if (item->is_temporary())
      continue;
    // Skip items that have all their data, and are OK to save.
    if (!item->IsPartialDownload() &&
        (item->safety_state() != DownloadItem::DANGEROUS))
      continue;
    // Skip items that don't match |dir_path|.
    // If |dir_path| is empty, all remaining items match.
    if (!dir_path.empty() && (it->second->full_path().DirName() != dir_path))
      continue;

    result->push_back(item);
  }

  // If we have a parent profile, let it add its downloads to the results.
  Profile* original_profile = profile_->GetOriginalProfile();
  if (original_profile != profile_)
    original_profile->GetDownloadManager()->GetCurrentDownloads(dir_path,
                                                                result);
}

void DownloadManager::SearchDownloads(const string16& query,
                                      std::vector<DownloadItem*>* result) {
  DCHECK(result);

  string16 query_lower(base::i18n::ToLower(query));

  for (DownloadMap::iterator it = history_downloads_.begin();
       it != history_downloads_.end(); ++it) {
    DownloadItem* download_item = it->second;

    if (download_item->is_temporary() || download_item->is_extension_install())
      continue;

    // Display Incognito downloads only in Incognito window, and vice versa.
    // The Incognito Downloads page will get the list of non-Incognito downloads
    // from its parent profile.
    if (profile_->IsOffTheRecord() != download_item->is_otr())
      continue;

    if (download_item->MatchesQuery(query_lower))
      result->push_back(download_item);
  }

  // If we have a parent profile, let it add its downloads to the results.
  Profile* original_profile = profile_->GetOriginalProfile();
  if (original_profile != profile_)
    original_profile->GetDownloadManager()->SearchDownloads(query, result);
}

// Query the history service for information about all persisted downloads.
bool DownloadManager::Init(Profile* profile) {
  DCHECK(profile);
  DCHECK(!shutdown_needed_)  << "DownloadManager already initialized.";
  shutdown_needed_ = true;

  profile_ = profile;
  download_history_.reset(new DownloadHistory(profile));
  download_history_->Load(
      NewCallback(this, &DownloadManager::OnQueryDownloadEntriesComplete));

  download_prefs_.reset(new DownloadPrefs(profile_->GetPrefs()));

  // In test mode, there may be no ResourceDispatcherHost.  In this case it's
  // safe to avoid setting |file_manager_| because we only call a small set of
  // functions, none of which need it.
  ResourceDispatcherHost* rdh = g_browser_process->resource_dispatcher_host();
  if (rdh) {
    file_manager_ = rdh->download_file_manager();
    DCHECK(file_manager_);
  }

  other_download_manager_observer_.reset(
      new OtherDownloadManagerObserver(this));

  return true;
}

// We have received a message from DownloadFileManager about a new download. We
// create a download item and store it in our download map, and inform the
// history system of a new download. Since this method can be called while the
// history service thread is still reading the persistent state, we do not
// insert the new DownloadItem into 'history_downloads_' or inform our
// observers at this point. OnCreateDownloadEntryComplete() handles that
// finalization of the the download creation as a callback from the
// history thread.
void DownloadManager::StartDownload(int32 download_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DownloadItem* download = GetActiveDownloadItem(download_id);
  if (!download)
    return;

#if defined(ENABLE_SAFE_BROWSING)
  // Create a client to verify download URL with safebrowsing.
  // It deletes itself after the callback.
  scoped_refptr<DownloadSBClient> sb_client = new DownloadSBClient(
      download_id, download->url_chain(), download->referrer_url(),
          profile_->GetPrefs()->GetBoolean(prefs::kSafeBrowsingEnabled));
  sb_client->CheckDownloadUrl(
      NewCallback(this, &DownloadManager::CheckDownloadUrlDone));
#else
  CheckDownloadUrlDone(download_id, false);
#endif
}

void DownloadManager::CheckForHistoryFilesRemoval() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  for (DownloadMap::iterator it = history_downloads_.begin();
       it != history_downloads_.end(); ++it) {
    CheckForFileRemoval(it->second);
  }
}

void DownloadManager::CheckForFileRemoval(DownloadItem* download_item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (download_item->IsComplete() &&
      !download_item->file_externally_removed()) {
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        NewRunnableMethod(this,
                          &DownloadManager::CheckForFileRemovalOnFileThread,
                          download_item->db_handle(),
                          download_item->GetTargetFilePath()));
  }
}

void DownloadManager::CheckForFileRemovalOnFileThread(
    int64 db_handle, const FilePath& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  if (!file_util::PathExists(path)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this,
                          &DownloadManager::OnFileRemovalDetected,
                          db_handle));
  }
}

void DownloadManager::OnFileRemovalDetected(int64 db_handle) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadMap::iterator it = history_downloads_.find(db_handle);
  if (it != history_downloads_.end()) {
    DownloadItem* download_item = it->second;
    download_item->OnDownloadedFileRemoved();
  }
}

void DownloadManager::CheckDownloadUrlDone(int32 download_id,
                                           bool is_dangerous_url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DownloadItem* download = GetActiveDownloadItem(download_id);
  if (!download)
    return;

  if (is_dangerous_url)
    download->MarkUrlDangerous();

  download_history_->CheckVisitedReferrerBefore(download_id,
      download->referrer_url(),
      NewCallback(this, &DownloadManager::CheckVisitedReferrerBeforeDone));
}

void DownloadManager::CheckVisitedReferrerBeforeDone(
    int32 download_id,
    bool visited_referrer_before) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DownloadItem* download = GetActiveDownloadItem(download_id);
  if (!download)
    return;

  // Check whether this download is for an extension install or not.
  // Allow extensions to be explicitly saved.
  DownloadStateInfo state = download->state_info();
  if (!state.prompt_user_for_save_location) {
    if (UserScript::IsURLUserScript(download->GetURL(),
        download->mime_type()) ||
        (download->mime_type() == Extension::kMimeType)) {
      state.is_extension_install = true;
    }
  }

  if (state.force_file_name.empty()) {
    FilePath generated_name;
    download_util::GenerateFileNameFromRequest(*download,
                                               &generated_name);

    // Freeze the user's preference for showing a Save As dialog.  We're going
    // to bounce around a bunch of threads and we don't want to worry about race
    // conditions where the user changes this pref out from under us.
    if (download_prefs_->PromptForDownload()) {
      // But ignore the user's preference for the following scenarios:
      // 1) Extension installation. Note that we only care here about the case
      //    where an extension is installed, not when one is downloaded with
      //    "save as...".
      // 2) Filetypes marked "always open." If the user just wants this file
      //    opened, don't bother asking where to keep it.
      if (!state.is_extension_install &&
          !ShouldOpenFileBasedOnExtension(generated_name))
        state.prompt_user_for_save_location = true;
    }
    if (download_prefs_->IsDownloadPathManaged()) {
      state.prompt_user_for_save_location = false;
    }

    // Determine the proper path for a download, by either one of the following:
    // 1) using the default download directory.
    // 2) prompting the user.
    if (state.prompt_user_for_save_location && !last_download_path_.empty()) {
      state.suggested_path = last_download_path_;
    } else {
      state.suggested_path = download_prefs_->download_path();
    }
    state.suggested_path = state.suggested_path.Append(generated_name);
  } else {
    state.suggested_path = state.force_file_name;
  }

  if (!state.prompt_user_for_save_location && state.force_file_name.empty()) {
    state.is_dangerous_file =
        IsDangerousFile(*download, state, visited_referrer_before);
  }

  // We need to move over to the download thread because we don't want to stat
  // the suggested path on the UI thread.
  // We can only access preferences on the UI thread, so check the download path
  // now and pass the value to the FILE thread.
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          this,
          &DownloadManager::CheckIfSuggestedPathExists,
          download->id(),
          state,
          download_prefs()->download_path()));
}

void DownloadManager::CheckIfSuggestedPathExists(int32 download_id,
                                                 DownloadStateInfo state,
                                                 const FilePath& default_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  // Make sure the default download directory exists.
  // TODO(phajdan.jr): only create the directory when we're sure the user
  // is going to save there and not to another directory of his choice.
  file_util::CreateDirectory(default_path);

  // Check writability of the suggested path. If we can't write to it, default
  // to the user's "My Documents" directory. We'll prompt them in this case.
  FilePath dir = state.suggested_path.DirName();
  FilePath filename = state.suggested_path.BaseName();
  if (!file_util::PathIsWritable(dir)) {
    VLOG(1) << "Unable to write to directory \"" << dir.value() << "\"";
    state.prompt_user_for_save_location = true;
    PathService::Get(chrome::DIR_USER_DOCUMENTS, &state.suggested_path);
    state.suggested_path = state.suggested_path.Append(filename);
  }

  // If the download is deemed dangerous, we'll use a temporary name for it.
  if (state.IsDangerous()) {
    state.target_name = FilePath(state.suggested_path).BaseName();
    // Create a temporary file to hold the file until the user approves its
    // download.
    FilePath::StringType file_name;
    FilePath path;
#if defined(OS_WIN)
    string16 unconfirmed_prefix =
        l10n_util::GetStringUTF16(IDS_DOWNLOAD_UNCONFIRMED_PREFIX);
#else
    std::string unconfirmed_prefix =
        l10n_util::GetStringUTF8(IDS_DOWNLOAD_UNCONFIRMED_PREFIX);
#endif

    while (path.empty()) {
      base::SStringPrintf(
          &file_name,
          unconfirmed_prefix.append(
              FILE_PATH_LITERAL(" %d.crdownload")).c_str(),
          base::RandInt(0, 100000));
      path = dir.Append(file_name);
      if (file_util::PathExists(path))
        path = FilePath();
    }
    state.suggested_path = path;
  } else {
    // Do not add the path uniquifier if we are saving to a specific path as in
    // the drag-out case.
    if (state.force_file_name.empty()) {
      state.path_uniquifier = download_util::GetUniquePathNumberWithCrDownload(
          state.suggested_path);
    }
    // We know the final path, build it if necessary.
    if (state.path_uniquifier > 0) {
      download_util::AppendNumberToPath(&(state.suggested_path),
                                        state.path_uniquifier);
      // Setting path_uniquifier to 0 to make sure we don't try to unique it
      // later on.
      state.path_uniquifier = 0;
    } else if (state.path_uniquifier == -1) {
      // We failed to find a unique path.  We have to prompt the user.
      VLOG(1) << "Unable to find a unique path for suggested path \""
              << state.suggested_path.value() << "\"";
      state.prompt_user_for_save_location = true;
    }
  }

  // Create an empty file at the suggested path so that we don't allocate the
  // same "non-existant" path to multiple downloads.
  // See: http://code.google.com/p/chromium/issues/detail?id=3662
  if (!state.prompt_user_for_save_location &&
      state.force_file_name.empty()) {
    if (state.IsDangerous())
      file_util::WriteFile(state.suggested_path, "", 0);
    else
      file_util::WriteFile(download_util::GetCrDownloadPath(
          state.suggested_path), "", 0);
  }

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      NewRunnableMethod(this,
                        &DownloadManager::OnPathExistenceAvailable,
                        download_id,
                        state));
}

void DownloadManager::OnPathExistenceAvailable(
    int32 download_id, const DownloadStateInfo& new_state) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DownloadItem* download = GetActiveDownloadItem(download_id);
  if (!download)
    return;

  VLOG(20) << __FUNCTION__ << "()"
           << " download = " << download->DebugString(true);

  download->SetFileCheckResults(new_state);

  FilePath suggested_path = download->suggested_path();

  if (download->prompt_user_for_save_location()) {
    // We must ask the user for the place to put the download.
    DownloadRequestHandle request_handle = download->request_handle();
    TabContents* contents = request_handle.GetTabContents();

    // |id_ptr| will be deleted in either FileSelected() or
    // FileSelectionCancelled().
    int32* id_ptr = new int32;
    *id_ptr = download_id;

    delegate_->ChooseDownloadPath(
        this, contents, suggested_path, reinterpret_cast<void*>(id_ptr));

    FOR_EACH_OBSERVER(Observer, observers_,
                      SelectFileDialogDisplayed(download_id));
  } else {
    // No prompting for download, just continue with the suggested name.
    ContinueDownloadWithPath(download, suggested_path);
  }
}

void DownloadManager::CreateDownloadItem(DownloadCreateInfo* info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DownloadItem* download = new DownloadItem(this, *info,
                                            profile_->IsOffTheRecord());
  int32 download_id = info->download_id;
  DCHECK(!ContainsKey(in_progress_, download_id));
  DCHECK(!ContainsKey(active_downloads_, download_id));
  downloads_.insert(download);
  active_downloads_[download_id] = download;
}

void DownloadManager::ContinueDownloadWithPath(DownloadItem* download,
                                               const FilePath& chosen_file) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(download);

  int32 download_id = download->id();

  // NOTE(ahendrickson) Eventually |active_downloads_| will replace
  // |in_progress_|, but we don't want to change the semantics yet.
  DCHECK(!ContainsKey(in_progress_, download_id));
  DCHECK(ContainsKey(downloads_, download));
  DCHECK(ContainsKey(active_downloads_, download_id));

  // Make sure the initial file name is set only once.
  DCHECK(download->full_path().empty());
  download->OnPathDetermined(chosen_file);

  VLOG(20) << __FUNCTION__ << "()"
           << " download = " << download->DebugString(true);

  in_progress_[download_id] = download;
  UpdateAppIcon();  // Reflect entry into in_progress_.

  // Rename to intermediate name.
  FilePath download_path;
  if (download->IsDangerous()) {
    // The download is not safe.  We can now rename the file to its
    // tentative name using RenameInProgressDownloadFile.
    // NOTE: The |Rename| below will be a no-op for dangerous files, as we're
    // renaming it to the same name.
    download_path = download->full_path();
  } else {
    // The download is a safe download.  We need to
    // rename it to its intermediate '.crdownload' path.  The final
    // name after user confirmation will be set from
    // DownloadItem::OnDownloadCompleting.
    download_path =
        download_util::GetCrDownloadPath(download->full_path());
  }

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          file_manager_, &DownloadFileManager::RenameInProgressDownloadFile,
          download->id(), download_path));

  download->Rename(download_path);

  download_history_->AddEntry(download,
      NewCallback(this, &DownloadManager::OnCreateDownloadEntryComplete));
}

void DownloadManager::UpdateDownload(int32 download_id, int64 size) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadMap::iterator it = active_downloads_.find(download_id);
  if (it != active_downloads_.end()) {
    DownloadItem* download = it->second;
    if (download->IsInProgress()) {
      download->Update(size);
      UpdateAppIcon();  // Reflect size updates.
      download_history_->UpdateEntry(download);
    }
  }
}

void DownloadManager::OnResponseCompleted(int32 download_id,
                                          int64 size,
                                          int os_error,
                                          const std::string& hash) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // ERR_CONNECTION_CLOSED is allowed since a number of servers in the wild
  // advertise a larger Content-Length than the amount of bytes in the message
  // body, and then close the connection. Other browsers - IE8, Firefox 4.0.1,
  // and Safari 5.0.4 - treat the download as complete in this case, so we
  // follow their lead.
  if (os_error == 0 || os_error == net::ERR_CONNECTION_CLOSED) {
    OnAllDataSaved(download_id, size, hash);
  } else {
    OnDownloadError(download_id, size, os_error);
  }
}

void DownloadManager::OnAllDataSaved(int32 download_id,
                                     int64 size,
                                     const std::string& hash) {
  VLOG(20) << __FUNCTION__ << "()" << " download_id = " << download_id
           << " size = " << size;
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // If it's not in active_downloads_, that means it was cancelled; just
  // ignore the notification.
  if (active_downloads_.count(download_id) == 0)
    return;

  DownloadItem* download = active_downloads_[download_id];
  download->OnAllDataSaved(size);

  // When hash is not available, it means either it is not calculated
  // or there is error while it is calculated. We will skip the download hash
  // check in that case.
  if (!hash.empty()) {
#if defined(ENABLE_SAFE_BROWSING)
    scoped_refptr<DownloadSBClient> sb_client =
        new DownloadSBClient(download_id,
                             download->url_chain(),
                             download->referrer_url(),
                             profile_->GetPrefs()->GetBoolean(
                                 prefs::kSafeBrowsingEnabled));
    sb_client->CheckDownloadHash(
        hash, NewCallback(this, &DownloadManager::CheckDownloadHashDone));
#else
    CheckDownloadHashDone(download_id, false);
#endif
  }
  MaybeCompleteDownload(download);
}

// TODO(lzheng): This function currently works as a callback place holder.
// Once we decide the hash check is reliable, we could move the
// MaybeCompleteDownload in OnAllDataSaved to this function.
void DownloadManager::CheckDownloadHashDone(int32 download_id,
                                            bool is_dangerous_hash) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DVLOG(1) << "CheckDownloadHashDone, download_id: " << download_id
           << " is dangerous_hash: " << is_dangerous_hash;

  // If it's not in active_downloads_, that means it was cancelled or
  // the download already finished.
  if (active_downloads_.count(download_id) == 0)
    return;

  DVLOG(1) << "CheckDownloadHashDone, url: "
           << active_downloads_[download_id]->GetURL().spec();
}

void DownloadManager::AssertQueueStateConsistent(DownloadItem* download) {
  // TODO(rdsmith): Change to DCHECK after http://crbug.com/85408 resolved.
  if (download->state() == DownloadItem::REMOVING) {
    CHECK(!ContainsKey(downloads_, download));
    CHECK(!ContainsKey(active_downloads_, download->id()));
    CHECK(!ContainsKey(in_progress_, download->id()));
    CHECK(!ContainsKey(history_downloads_, download->db_handle()));
    return;
  }

  // Should be in downloads_ if we're not REMOVING.
  CHECK(ContainsKey(downloads_, download));

  // Check history_downloads_ consistency.
  if (download->db_handle() != DownloadHistory::kUninitializedHandle) {
    CHECK(ContainsKey(history_downloads_, download->db_handle()));
  } else {
    // TODO(rdsmith): Somewhat painful; make sure to disable in
    // release builds after resolution of http://crbug.com/85408.
    for (DownloadMap::iterator it = history_downloads_.begin();
         it != history_downloads_.end(); ++it) {
      CHECK(it->second != download);
    }
  }

  CHECK(ContainsKey(active_downloads_, download->id()) ==
        (download->state() == DownloadItem::IN_PROGRESS));
  CHECK(ContainsKey(in_progress_, download->id()) ==
        (download->state() == DownloadItem::IN_PROGRESS));
}

bool DownloadManager::IsDownloadReadyForCompletion(DownloadItem* download) {
  // If we don't have all the data, the download is not ready for
  // completion.
  if (!download->all_data_saved())
    return false;

  // If the download is dangerous, but not yet validated, it's not ready for
  // completion.
  if (download->safety_state() == DownloadItem::DANGEROUS)
    return false;

  // If the download isn't active (e.g. has been cancelled) it's not
  // ready for completion.
  if (active_downloads_.count(download->id()) == 0)
    return false;

  // If the download hasn't been inserted into the history system
  // (which occurs strictly after file name determination, intermediate
  // file rename, and UI display) then it's not ready for completion.
  if (download->db_handle() == DownloadHistory::kUninitializedHandle)
    return false;

  return true;
}

void DownloadManager::MaybeCompleteDownload(DownloadItem* download) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  VLOG(20) << __FUNCTION__ << "()" << " download = "
           << download->DebugString(false);

  if (!IsDownloadReadyForCompletion(download))
    return;

  // TODO(rdsmith): DCHECK that we only pass through this point
  // once per download.  The natural way to do this is by a state
  // transition on the DownloadItem.

  // Confirm we're in the proper set of states to be here;
  // in in_progress_, have all data, have a history handle, (validated or safe).
  DCHECK_NE(DownloadItem::DANGEROUS, download->safety_state());
  DCHECK_EQ(1u, in_progress_.count(download->id()));
  DCHECK(download->all_data_saved());
  DCHECK(download->db_handle() != DownloadHistory::kUninitializedHandle);
  DCHECK_EQ(1u, history_downloads_.count(download->db_handle()));

  VLOG(20) << __FUNCTION__ << "()" << " executing: download = "
           << download->DebugString(false);

  // Remove the id from in_progress
  in_progress_.erase(download->id());
  UpdateAppIcon();  // Reflect removal from in_progress_.

  download_history_->UpdateEntry(download);

  // Finish the download.
  download->OnDownloadCompleting(file_manager_);
}

void DownloadManager::DownloadCompleted(int32 download_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadItem* download = GetDownloadItem(download_id);
  DCHECK(download);
  download_history_->UpdateEntry(download);
  active_downloads_.erase(download_id);
}

void DownloadManager::OnDownloadRenamedToFinalName(int download_id,
                                                   const FilePath& full_path,
                                                   int uniquifier) {
  VLOG(20) << __FUNCTION__ << "()" << " download_id = " << download_id
           << " full_path = \"" << full_path.value() << "\""
           << " uniquifier = " << uniquifier;
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DownloadItem* item = GetDownloadItem(download_id);
  if (!item)
    return;

  if (item->safety_state() == DownloadItem::SAFE) {
    DCHECK_EQ(0, uniquifier) << "We should not uniquify SAFE downloads twice";
  }

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          file_manager_, &DownloadFileManager::CompleteDownload, download_id));

  if (uniquifier)
    item->set_path_uniquifier(uniquifier);

  item->OnDownloadRenamedToFinalName(full_path);
  download_history_->UpdateDownloadPath(item, full_path);
}

void DownloadManager::DownloadCancelled(int32 download_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadMap::iterator it = in_progress_.find(download_id);
  if (it == in_progress_.end())
    return;
  DownloadItem* download = it->second;

  VLOG(20) << __FUNCTION__ << "()" << " download_id = " << download_id
           << " download = " << download->DebugString(true);

  // Clean up will happen when the history system create callback runs if we
  // don't have a valid db_handle yet.
  if (download->db_handle() != DownloadHistory::kUninitializedHandle) {
    in_progress_.erase(it);
    active_downloads_.erase(download_id);
    UpdateAppIcon();  // Reflect removal from in_progress_.
    download_history_->UpdateEntry(download);
  }

  DownloadCancelledInternal(download_id, download->request_handle());
}

void DownloadManager::DownloadCancelledInternal(
    int download_id, const DownloadRequestHandle& request_handle) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  request_handle.CancelRequest();

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          file_manager_, &DownloadFileManager::CancelDownload, download_id));
}

void DownloadManager::OnDownloadError(int32 download_id,
                                      int64 size,
                                      int os_error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadMap::iterator it = active_downloads_.find(download_id);
  // A cancel at the right time could remove the download from the
  // |active_downloads_| map before we get here.
  if (it == active_downloads_.end())
    return;

  DownloadItem* download = it->second;

  VLOG(20) << __FUNCTION__ << "()" << " Error " << os_error
           << " at offset " << download->received_bytes()
           << " for download = " << download->DebugString(true);

  download->Interrupted(size, os_error);

  // TODO(ahendrickson) - Remove this when we add resuming of interrupted
  // downloads, as we will keep the download item around in that case.
  //
  // Clean up will happen when the history system create callback runs if we
  // don't have a valid db_handle yet.
  if (download->db_handle() != DownloadHistory::kUninitializedHandle) {
    in_progress_.erase(download_id);
    active_downloads_.erase(download_id);
    UpdateAppIcon();  // Reflect removal from in_progress_.
    download_history_->UpdateEntry(download);
  }

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          file_manager_, &DownloadFileManager::CancelDownload, download_id));
}

void DownloadManager::UpdateAppIcon() {
  if (status_updater_)
    status_updater_->Update();
}

void DownloadManager::RemoveDownload(int64 download_handle) {
  DownloadMap::iterator it = history_downloads_.find(download_handle);
  if (it == history_downloads_.end())
    return;

  // Make history update.
  DownloadItem* download = it->second;
  download_history_->RemoveEntry(download);

  // Remove from our tables and delete.
  history_downloads_.erase(it);
  int downloads_count = downloads_.erase(download);
  DCHECK_EQ(1, downloads_count);

  // Tell observers to refresh their views.
  NotifyModelChanged();

  delete download;
}

int DownloadManager::RemoveDownloadsBetween(const base::Time remove_begin,
                                            const base::Time remove_end) {
  download_history_->RemoveEntriesBetween(remove_begin, remove_end);

  // All downloads visible to the user will be in the history,
  // so scan that map.
  DownloadMap::iterator it = history_downloads_.begin();
  std::vector<DownloadItem*> pending_deletes;
  while (it != history_downloads_.end()) {
    DownloadItem* download = it->second;
    if (download->start_time() >= remove_begin &&
        (remove_end.is_null() || download->start_time() < remove_end) &&
        (download->IsComplete() ||
         download->IsCancelled() ||
         download->IsInterrupted())) {
      AssertQueueStateConsistent(download);

      // Remove from the map and move to the next in the list.
      history_downloads_.erase(it++);

      // Also remove it from any completed dangerous downloads.
      pending_deletes.push_back(download);

      continue;
    }

    ++it;
  }

  // If we aren't deleting anything, we're done.
  if (pending_deletes.empty())
    return 0;

  // Remove the chosen downloads from the main owning container.
  for (std::vector<DownloadItem*>::iterator it = pending_deletes.begin();
       it != pending_deletes.end(); it++) {
    downloads_.erase(*it);
  }

  // Tell observers to refresh their views.
  NotifyModelChanged();

  // Delete the download items themselves.
  int num_deleted = static_cast<int>(pending_deletes.size());

  STLDeleteContainerPointers(pending_deletes.begin(), pending_deletes.end());
  pending_deletes.clear();

  return num_deleted;
}

int DownloadManager::RemoveDownloads(const base::Time remove_begin) {
  return RemoveDownloadsBetween(remove_begin, base::Time());
}

int DownloadManager::RemoveAllDownloads() {
  if (this != profile_->GetOriginalProfile()->GetDownloadManager()) {
    // This is an incognito downloader. Clear All should clear main download
    // manager as well.
    profile_->GetOriginalProfile()->GetDownloadManager()->RemoveAllDownloads();
  }
  // The null times make the date range unbounded.
  return RemoveDownloadsBetween(base::Time(), base::Time());
}

void DownloadManager::SavePageAsDownloadStarted(DownloadItem* download) {
#if !defined(NDEBUG)
  save_page_as_downloads_.insert(download);
#endif
  downloads_.insert(download);
  // Add to history and notify observers.
  AddDownloadItemToHistory(download, DownloadHistory::kUninitializedHandle);
  NotifyModelChanged();
}

// Initiate a download of a specific URL. We send the request to the
// ResourceDispatcherHost, and let it send us responses like a regular
// download.
void DownloadManager::DownloadUrl(const GURL& url,
                                  const GURL& referrer,
                                  const std::string& referrer_charset,
                                  TabContents* tab_contents) {
  DownloadUrlToFile(url, referrer, referrer_charset, DownloadSaveInfo(),
                    tab_contents);
}

void DownloadManager::DownloadUrlToFile(const GURL& url,
                                        const GURL& referrer,
                                        const std::string& referrer_charset,
                                        const DownloadSaveInfo& save_info,
                                        TabContents* tab_contents) {
  DCHECK(tab_contents);
  // We send a pointer to content::ResourceContext, instead of the usual
  // reference, so that a copy of the object isn't made.
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
      NewRunnableFunction(&download_util::DownloadUrl,
                          url,
                          referrer,
                          referrer_charset,
                          save_info,
                          g_browser_process->resource_dispatcher_host(),
                          tab_contents->GetRenderProcessHost()->id(),
                          tab_contents->render_view_host()->routing_id(),
                          &tab_contents->browser_context()->
                              GetResourceContext()));
}

void DownloadManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
  observer->ModelChanged();
}

void DownloadManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool DownloadManager::ShouldOpenFileBasedOnExtension(
    const FilePath& path) const {
  FilePath::StringType extension = path.Extension();
  if (extension.empty())
    return false;
  if (Extension::IsExtension(path))
    return false;
  DCHECK(extension[0] == FilePath::kExtensionSeparator);
  extension.erase(0, 1);
  return download_prefs_->IsAutoOpenEnabledForExtension(extension);
}

bool DownloadManager::IsDownloadProgressKnown() {
  for (DownloadMap::iterator i = in_progress_.begin();
       i != in_progress_.end(); ++i) {
    if (i->second->total_bytes() <= 0)
      return false;
  }

  return true;
}

int64 DownloadManager::GetInProgressDownloadCount() {
  return in_progress_.size();
}

int64 DownloadManager::GetReceivedDownloadBytes() {
  DCHECK(IsDownloadProgressKnown());
  int64 received_bytes = 0;
  for (DownloadMap::iterator i = in_progress_.begin();
       i != in_progress_.end(); ++i) {
    received_bytes += i->second->received_bytes();
  }
  return received_bytes;
}

int64 DownloadManager::GetTotalDownloadBytes() {
  DCHECK(IsDownloadProgressKnown());
  int64 total_bytes = 0;
  for (DownloadMap::iterator i = in_progress_.begin();
       i != in_progress_.end(); ++i) {
    total_bytes += i->second->total_bytes();
  }
  return total_bytes;
}

void DownloadManager::FileSelected(const FilePath& path, void* params) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  int32* id_ptr = reinterpret_cast<int32*>(params);
  DCHECK(id_ptr != NULL);
  int32 download_id = *id_ptr;
  delete id_ptr;

  DownloadItem* download = GetActiveDownloadItem(download_id);
  if (!download)
    return;
  VLOG(20) << __FUNCTION__ << "()" << " path = \"" << path.value() << "\""
            << " download = " << download->DebugString(true);

  if (download->prompt_user_for_save_location())
    last_download_path_ = path.DirName();

  // Make sure the initial file name is set only once.
  ContinueDownloadWithPath(download, path);
}

void DownloadManager::FileSelectionCanceled(void* params) {
  // The user didn't pick a place to save the file, so need to cancel the
  // download that's already in progress to the temporary location.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  int32* id_ptr = reinterpret_cast<int32*>(params);
  DCHECK(id_ptr != NULL);
  int32 download_id = *id_ptr;
  delete id_ptr;

  DownloadItem* download = GetActiveDownloadItem(download_id);
  if (!download)
    return;

  VLOG(20) << __FUNCTION__ << "()"
           << " download = " << download->DebugString(true);

  DownloadCancelledInternal(download_id, download->request_handle());
}

// TODO(phajdan.jr): This is apparently not being exercised in tests.
bool DownloadManager::IsDangerousFile(const DownloadItem& download,
                                      const DownloadStateInfo& state,
                                      bool visited_referrer_before) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  bool auto_open = ShouldOpenFileBasedOnExtension(state.suggested_path);
  download_util::DownloadDangerLevel danger_level =
      download_util::GetFileDangerLevel(state.suggested_path.BaseName());

  if (danger_level == download_util::Dangerous)
    return !(auto_open && state.has_user_gesture);

  if (danger_level == download_util::AllowOnUserGesture &&
      (!state.has_user_gesture || !visited_referrer_before))
    return true;

  if (state.is_extension_install) {
    // Extensions that are not from the gallery are considered dangerous.
    ExtensionService* service = profile()->GetExtensionService();
    if (!service || !service->IsDownloadFromGallery(download.GetURL(),
                                                    download.referrer_url()))
      return true;
  }
  return false;
}

// Operations posted to us from the history service ----------------------------

// The history service has retrieved all download entries. 'entries' contains
// 'DownloadHistoryInfo's in sorted order (by ascending start_time).
void DownloadManager::OnQueryDownloadEntriesComplete(
    std::vector<DownloadHistoryInfo>* entries) {
  for (size_t i = 0; i < entries->size(); ++i) {
    DownloadItem* download = new DownloadItem(this, entries->at(i));
    DCHECK(!ContainsKey(history_downloads_, download->db_handle()));
    downloads_.insert(download);
    history_downloads_[download->db_handle()] = download;
    VLOG(20) << __FUNCTION__ << "()" << i << ">"
             << " download = " << download->DebugString(true);
  }
  NotifyModelChanged();
  CheckForHistoryFilesRemoval();
}

void DownloadManager::AddDownloadItemToHistory(DownloadItem* download,
                                               int64 db_handle) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // It's not immediately obvious, but HistoryBackend::CreateDownload() can
  // call this function with an invalid |db_handle|. For instance, this can
  // happen when the history database is offline. We cannot have multiple
  // DownloadItems with the same invalid db_handle, so we need to assign a
  // unique |db_handle| here.
  if (db_handle == DownloadHistory::kUninitializedHandle)
    db_handle = download_history_->GetNextFakeDbHandle();

  // TODO(rdsmith): Convert to DCHECK() when http://crbug.com/84508
  // is fixed.
  CHECK_NE(DownloadHistory::kUninitializedHandle, db_handle);

  DCHECK(download->db_handle() == DownloadHistory::kUninitializedHandle);
  download->set_db_handle(db_handle);

  DCHECK(!ContainsKey(history_downloads_, download->db_handle()));
  history_downloads_[download->db_handle()] = download;
}

// Once the new DownloadItem's creation info has been committed to the history
// service, we associate the DownloadItem with the db handle, update our
// 'history_downloads_' map and inform observers.
void DownloadManager::OnCreateDownloadEntryComplete(int32 download_id,
                                                    int64 db_handle) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadItem* download = GetActiveDownloadItem(download_id);
  if (!download)
    return;

  VLOG(20) << __FUNCTION__ << "()" << " db_handle = " << db_handle
           << " download_id = " << download_id
           << " download = " << download->DebugString(true);

  AddDownloadItemToHistory(download, db_handle);

  // Show in the appropriate browser UI.
  // This includes buttons to save or cancel, for a dangerous download.
  ShowDownloadInBrowser(download);

  // Inform interested objects about the new download.
  NotifyModelChanged();

  // If the download is still in progress, try to complete it.
  //
  // Otherwise, download has been cancelled or interrupted before we've
  // received the DB handle.  We post one final message to the history
  // service so that it can be properly in sync with the DownloadItem's
  // completion status, and also inform any observers so that they get
  // more than just the start notification.
  if (download->IsInProgress()) {
    MaybeCompleteDownload(download);
  } else {
    DCHECK(download->IsCancelled())
        << " download = " << download->DebugString(true);
    in_progress_.erase(download_id);
    active_downloads_.erase(download_id);
    download_history_->UpdateEntry(download);
    download->UpdateObservers();
  }
}

void DownloadManager::ShowDownloadInBrowser(DownloadItem* download) {
  // The 'contents' may no longer exist if the user closed the tab before we
  // get this start completion event.
  DownloadRequestHandle request_handle = download->request_handle();
  TabContents* content = request_handle.GetTabContents();

  // If the contents no longer exists, we ask the embedder to suggest another
  // tab.
  if (!content)
    content = delegate_->GetAlternativeTabContentsToNotifyForDownload(this);

  if (content)
    content->OnStartDownload(download);
}

// Clears the last download path, used to initialize "save as" dialogs.
void DownloadManager::ClearLastDownloadPath() {
  last_download_path_ = FilePath();
}

void DownloadManager::NotifyModelChanged() {
  FOR_EACH_OBSERVER(Observer, observers_, ModelChanged());
}

DownloadItem* DownloadManager::GetDownloadItem(int download_id) {
  // The |history_downloads_| map is indexed by the download's db_handle,
  // not its id, so we have to iterate.
  for (DownloadMap::iterator it = history_downloads_.begin();
       it != history_downloads_.end(); ++it) {
    DownloadItem* item = it->second;
    if (item->id() == download_id)
      return item;
  }
  return NULL;
}

DownloadItem* DownloadManager::GetActiveDownloadItem(int download_id) {
  DCHECK(ContainsKey(active_downloads_, download_id));
  DownloadItem* download = active_downloads_[download_id];
  DCHECK(download != NULL);
  return download;
}

// Confirm that everything in all maps is also in |downloads_|, and that
// everything in |downloads_| is also in some other map.
void DownloadManager::AssertContainersConsistent() const {
#if !defined(NDEBUG)
  // Turn everything into sets.
  DownloadSet active_set, history_set;
  const DownloadMap* input_maps[] = {&active_downloads_, &history_downloads_};
  DownloadSet* local_sets[] = {&active_set, &history_set};
  DCHECK_EQ(ARRAYSIZE_UNSAFE(input_maps), ARRAYSIZE_UNSAFE(local_sets));
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(input_maps); i++) {
    for (DownloadMap::const_iterator it = input_maps[i]->begin();
         it != input_maps[i]->end(); it++) {
      local_sets[i]->insert(&*it->second);
    }
  }

  // Check if each set is fully present in downloads, and create a union.
  const DownloadSet* all_sets[] = {&active_set, &history_set,
                                   &save_page_as_downloads_};
  DownloadSet downloads_union;
  for (int i = 0; i < static_cast<int>(ARRAYSIZE_UNSAFE(all_sets)); i++) {
    DownloadSet remainder;
    std::insert_iterator<DownloadSet> insert_it(remainder, remainder.begin());
    std::set_difference(all_sets[i]->begin(), all_sets[i]->end(),
                        downloads_.begin(), downloads_.end(),
                        insert_it);
    DCHECK(remainder.empty());
    std::insert_iterator<DownloadSet>
        insert_union(downloads_union, downloads_union.end());
    std::set_union(downloads_union.begin(), downloads_union.end(),
                   all_sets[i]->begin(), all_sets[i]->end(),
                   insert_union);
  }

  // Is everything in downloads_ present in one of the other sets?
  DownloadSet remainder;
  std::insert_iterator<DownloadSet>
      insert_remainder(remainder, remainder.begin());
  std::set_difference(downloads_.begin(), downloads_.end(),
                      downloads_union.begin(), downloads_union.end(),
                      insert_remainder);
  DCHECK(remainder.empty());
#endif
}

// DownloadManager::OtherDownloadManagerObserver implementation ----------------

DownloadManager::OtherDownloadManagerObserver::OtherDownloadManagerObserver(
    DownloadManager* observing_download_manager)
    : observing_download_manager_(observing_download_manager),
      observed_download_manager_(NULL) {
  if (observing_download_manager->profile_->GetOriginalProfile() ==
      observing_download_manager->profile_) {
    return;
  }

  observed_download_manager_ = observing_download_manager_->
      profile_->GetOriginalProfile()->GetDownloadManager();
  observed_download_manager_->AddObserver(this);
}

DownloadManager::OtherDownloadManagerObserver::~OtherDownloadManagerObserver() {
  if (observed_download_manager_)
    observed_download_manager_->RemoveObserver(this);
}

void DownloadManager::OtherDownloadManagerObserver::ModelChanged() {
  observing_download_manager_->NotifyModelChanged();
}

void DownloadManager::OtherDownloadManagerObserver::ManagerGoingDown() {
  observed_download_manager_ = NULL;
}

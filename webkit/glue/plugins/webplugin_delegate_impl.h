// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_GLUE_PLUGIN_WEBPLUGIN_DELEGATE_IMPL_H_
#define WEBKIT_GLUE_PLUGIN_WEBPLUGIN_DELEGATE_IMPL_H_

#include "build/build_config.h"

#include <string>
#include <list>

#include "base/file_path.h"
#include "base/gfx/native_widget_types.h"
#include "base/gfx/rect.h"
#include "base/ref_counted.h"
#include "base/task.h"
#include "third_party/npapi/bindings/npapi.h"
#include "webkit/glue/webcursor.h"
#include "webkit/glue/webplugin_delegate.h"

#if defined(OS_LINUX)
typedef struct _GdkDrawable GdkPixmap;
#endif

namespace NPAPI {
class PluginInstance;
}

// An implementation of WebPluginDelegate that proxies all calls to
// the plugin process.
class WebPluginDelegateImpl : public webkit_glue::WebPluginDelegate {
 public:
  enum PluginQuirks {
    PLUGIN_QUIRK_SETWINDOW_TWICE = 1,  // Win32
    PLUGIN_QUIRK_THROTTLE_WM_USER_PLUS_ONE = 2,  // Win32
    PLUGIN_QUIRK_DONT_CALL_WND_PROC_RECURSIVELY = 4,  // Win32
    PLUGIN_QUIRK_DONT_SET_NULL_WINDOW_HANDLE_ON_DESTROY = 8,  // Win32
    PLUGIN_QUIRK_DONT_ALLOW_MULTIPLE_INSTANCES = 16,  // Win32
    PLUGIN_QUIRK_DIE_AFTER_UNLOAD = 32,  // Win32
    PLUGIN_QUIRK_PATCH_SETCURSOR = 64,  // Win32
    PLUGIN_QUIRK_BLOCK_NONSTANDARD_GETURL_REQUESTS = 128,  // Win32
    PLUGIN_QUIRK_WINDOWLESS_OFFSET_WINDOW_TO_DRAW = 256,  // Linux
    PLUGIN_QUIRK_WINDOWLESS_INVALIDATE_AFTER_SET_WINDOW = 512,  // Linux
    PLUGIN_QUIRK_NO_WINDOWLESS = 1024,  // Windows
  };

  static WebPluginDelegateImpl* Create(const FilePath& filename,
                                       const std::string& mime_type,
                                       gfx::PluginWindowHandle containing_view);

  static bool IsPluginDelegateWindow(gfx::NativeWindow window);
  static bool GetPluginNameFromWindow(gfx::NativeWindow window,
                                      std::wstring *plugin_name);

  // Returns true if the window handle passed in is that of the dummy
  // activation window for windowless plugins.
  static bool IsDummyActivationWindow(gfx::NativeWindow window);

  // WebPluginDelegate implementation
  virtual void PluginDestroyed();
  virtual bool Initialize(const GURL& url,
                          const std::vector<std::string>& arg_names,
                          const std::vector<std::string>& arg_values,
                          webkit_glue::WebPlugin* plugin,
                          bool load_manually);
  virtual void UpdateGeometry(const gfx::Rect& window_rect,
                              const gfx::Rect& clip_rect);
  virtual void Paint(gfx::NativeDrawingContext context, const gfx::Rect& rect);
  virtual void Print(gfx::NativeDrawingContext context);

  virtual void SetFocus();  // only called when windowless
  // only called when windowless
  // See NPAPI NPP_HandleEvent for more information.
  virtual bool HandleInputEvent(const WebKit::WebInputEvent& event,
                                WebKit::WebCursorInfo* cursor);
  virtual NPObject* GetPluginScriptableObject();
  virtual void DidFinishLoadWithReason(const GURL& url, NPReason reason,
                                       intptr_t notify_data);
  virtual int GetProcessId();

  virtual void SendJavaScriptStream(const GURL& url,
                                    const std::string& result,
                                    bool success, bool notify_needed,
                                    intptr_t notify_data);
  virtual void DidReceiveManualResponse(const GURL& url,
                                        const std::string& mime_type,
                                        const std::string& headers,
                                        uint32 expected_length,
                                        uint32 last_modified);
  virtual void DidReceiveManualData(const char* buffer, int length);
  virtual void DidFinishManualLoading();
  virtual void DidManualLoadFail();
  virtual FilePath GetPluginPath();
  virtual void InstallMissingPlugin();
  virtual webkit_glue::WebPluginResourceClient* CreateResourceClient(
      int resource_id,
      const GURL& url,
      bool notify_needed,
      intptr_t notify_data,
      intptr_t stream);

  virtual bool IsWindowless() const { return windowless_ ; }
  virtual gfx::Rect GetRect() const { return window_rect_; }
  virtual gfx::Rect GetClipRect() const { return clip_rect_; }

  // Returns a combination of PluginQuirks.
  int GetQuirks() const { return quirks_; }

#if defined(OS_MACOSX)
  // Informs the delegate that the context used for painting windowless plugins
  // has changed.
  virtual void UpdateContext(gfx::NativeDrawingContext context);
#endif

 private:
  friend class DeleteTask<WebPluginDelegateImpl>;

  WebPluginDelegateImpl(gfx::PluginWindowHandle containing_view,
                        NPAPI::PluginInstance *instance);
  ~WebPluginDelegateImpl();

  // Called by Initialize() for platform-specific initialization.
  void PlatformInitialize();

  // Called by DestroyInstance(), used for platform-specific destruction.
  void PlatformDestroyInstance();

#if !defined(OS_MACOSX)
  //--------------------------
  // used for windowed plugins
  void WindowedUpdateGeometry(const gfx::Rect& window_rect,
                              const gfx::Rect& clip_rect);
  // Create the native window.
  // Returns true if the window is created (or already exists).
  // Returns false if unable to create the window.
  bool WindowedCreatePlugin();

  // Destroy the native window.
  void WindowedDestroyWindow();

  // Reposition the native window to be in sync with the given geometry.
  // Returns true if the native window has moved or been clipped differently.
  bool WindowedReposition(const gfx::Rect& window_rect,
                          const gfx::Rect& clip_rect);

  // Tells the plugin about the current state of the window.
  // See NPAPI NPP_SetWindow for more information.
  void WindowedSetWindow();
#endif

#if defined(OS_WIN)
  // Registers the window class for our window
  ATOM RegisterNativeWindowClass();

  // Our WndProc functions.
  static LRESULT CALLBACK DummyWindowProc(
      HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK NativeWndProc(
      HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK FlashWindowlessWndProc(
      HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  // Used for throttling Flash messages.
  static void ClearThrottleQueueForWindow(HWND window);
  static void OnThrottleMessage();
  static void ThrottleMessage(WNDPROC proc, HWND hwnd, UINT message,
      WPARAM wParam, LPARAM lParam);
#endif

  //----------------------------
  // used for windowless plugins
  void WindowlessUpdateGeometry(const gfx::Rect& window_rect,
                                const gfx::Rect& clip_rect);
  void WindowlessPaint(gfx::NativeDrawingContext hdc, const gfx::Rect& rect);

  // Tells the plugin about the current state of the window.
  // See NPAPI NPP_SetWindow for more information.
  void WindowlessSetWindow(bool force_set_window);

  //-----------------------------------------
  // used for windowed and windowless plugins

  NPAPI::PluginInstance* instance() { return instance_.get(); }

  // Closes down and destroys our plugin instance.
  void DestroyInstance();

#if !defined(OS_MACOSX)
  // used for windowed plugins
  gfx::PluginWindowHandle windowed_handle_;
  bool windowed_did_set_window_;
  gfx::Rect windowed_last_pos_;
#endif

  // TODO(dglazkov): No longer used by Windows, make sure the removal
  // causes no regressions and eliminate from other platforms.
  // this is an optimization to avoid calling SetWindow to the plugin
  // when it is not necessary.  Initially, we need to call SetWindow,
  // and after that we only need to call it when the geometry changes.
  // use this flag to indicate whether we really need it or not.
  bool windowless_needs_set_window_;

  // used by windowed and windowless plugins
  bool windowless_;

  webkit_glue::WebPlugin* plugin_;
  scoped_refptr<NPAPI::PluginInstance> instance_;

#if defined(OS_WIN)
  // Original wndproc before we subclassed.
  WNDPROC plugin_wnd_proc_;

  // Used to throttle WM_USER+1 messages in Flash.
  uint32 last_message_;
  bool is_calling_wndproc;
#endif // OS_WIN

#if defined(OS_LINUX)
  // The pixmap we're drawing into, for a windowless plugin.
  GdkPixmap* pixmap_;
  double first_event_time_;

  // On Linux some plugins assume that the GtkSocket container is in the same
  // process. So we create a GtkPlug to plug into the browser's container, and
  // a GtkSocket to hold the plugin. We then send the GtkPlug to the browser
  // process.
  GtkWidget* plug_;
  GtkWidget* socket_;

  // Ensure pixmap_ exists and is at least width by height pixels.
  void EnsurePixmapAtLeastSize(int width, int height);
#endif

  gfx::PluginWindowHandle parent_;
  NPWindow window_;
#if defined(OS_MACOSX)
  NP_CGContext cg_context_;
#endif
  gfx::Rect window_rect_;
  gfx::Rect clip_rect_;
  std::vector<gfx::Rect> cutout_rects_;
  int quirks_;

#if defined(OS_WIN)
  // Windowless plugins don't have keyboard focus causing issues with the
  // plugin not receiving keyboard events if the plugin enters a modal
  // loop like TrackPopupMenuEx or MessageBox, etc.
  // This is a basic issue with windows activation and focus arising due to
  // the fact that these windows are created by different threads. Activation
  // and focus are thread specific states, and if the browser has focus,
  // the plugin may not have focus.
  // To fix a majority of these activation issues we create a dummy visible
  // child window to which we set focus whenever the windowless plugin
  // receives a WM_LBUTTONDOWN/WM_RBUTTONDOWN message via NPP_HandleEvent.

  HWND dummy_window_for_activation_;
  bool CreateDummyWindowForActivation();

  // Returns true if the event passed in needs to be tracked for a potential
  // modal loop.
  static bool ShouldTrackEventForModalLoops(NPEvent* event);

  // The message filter hook procedure, which tracks modal loops entered by
  // a plugin in the course of a NPP_HandleEvent call.
  static LRESULT CALLBACK HandleEventMessageFilterHook(int code, WPARAM wParam,
                                                       LPARAM lParam);
#endif
  // Called by the message filter hook when the plugin enters a modal loop.
  void OnModalLoopEntered();

  // Returns true if the message passed in corresponds to a user gesture.
  static bool IsUserGestureMessage(unsigned int message);

  // Indicates the end of a user gesture period.
  void OnUserGestureEnd();

  // The url with which the plugin was instantiated.
  std::string plugin_url_;

#if defined(OS_WIN)
  // Handle to the message filter hook
  HHOOK handle_event_message_filter_hook_;
#endif

  // Event which is set when the plugin enters a modal loop in the course
  // of a NPP_HandleEvent call.
#if defined(OS_WIN)
  HANDLE handle_event_pump_messages_event_;
#endif

  // Holds the depth of the HandleEvent callstack.
  int handle_event_depth_;

  // This flag indicates whether we started tracking a user gesture message.
  bool user_gesture_message_posted_;

  // Runnable Method Factory used to invoke the OnUserGestureEnd method
  // asynchronously.
#if !defined(OS_LINUX)
  ScopedRunnableMethodFactory<WebPluginDelegateImpl> user_gesture_msg_factory_;
#endif

#if defined(OS_WIN)
  // TrackPopupMenu interceptor. Parameters are the same as the Win32 function
  // TrackPopupMenu.
  static BOOL WINAPI TrackPopupMenuPatch(HMENU menu, unsigned int flags, int x,
                                         int y, int reserved, HWND window,
                                         const RECT* rect);

  // SetCursor interceptor for windowless plugins.
  static HCURSOR WINAPI SetCursorPatch(HCURSOR cursor);
#endif

#if defined(OS_MACOSX)
  // Runnable Method Factory used to drip null events into the plugin
  ScopedRunnableMethodFactory<WebPluginDelegateImpl> null_event_factory_;

  // indicates that it's time to send the plugin a null event
  void OnNullEvent();

  // last mouse position within the plugin's rect (used for null events)
  int last_mouse_x_;
  int last_mouse_y_;
#endif

  // Holds the current cursor set by the windowless plugin.
  WebCursor current_windowless_cursor_;

  friend class webkit_glue::WebPluginDelegate;

  DISALLOW_EVIL_CONSTRUCTORS(WebPluginDelegateImpl);
};

#endif  // #ifndef WEBKIT_GLUE_PLUGIN_WEBPLUGIN_DELEGATE_IMPL_H_

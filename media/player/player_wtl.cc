// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stand alone media player application used for testing the media library.

// ATL compatibility with Chrome build settings.
#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN

#undef min
#undef max
#define NOMINMAX

// Note this header must come before other ATL headers.
#include "media/player/stdafx.h"
#include <atlcrack.h>   // NOLINT
#include <atlctrls.h>   // NOLINT
#include <atlctrlw.h>   // NOLINT
#include <atldlgs.h>    // NOLINT
#include <atlframe.h>   // NOLINT
#include <atlmisc.h>    // NOLINT
#include <atlprint.h>   // NOLINT
#include <atlscrl.h>    // NOLINT

#include "base/at_exit.h"
#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "base/time.h"

// Note these headers are order sensitive.
#include "media/base/factory.h"
#include "media/base/pipeline_impl.h"
#include "media/player/movie.h"
#include "media/player/resource.h"
#include "media/player/wtl_renderer.h"
#include "media/player/view.h"
#include "media/player/props.h"
#include "media/player/seek.h"
#include "media/player/list.h"
#include "media/player/mainfrm.h"

// Note these headers are NOT order sensitive.
#include "media/filters/audio_renderer_impl.h"
#include "media/filters/ffmpeg_audio_decoder.h"
#include "media/filters/ffmpeg_demuxer.h"
#include "media/filters/ffmpeg_video_decoder.h"
#include "media/filters/file_data_source.h"

// Enable timing code by turning on TESTING macro.
// #define TESTING 1

#ifdef TESTING
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>  // NOLINT
#include <stdio.h>    // NOLINT
#include <process.h>  // NOLINT
#include <string.h>   // NOLINT

// Fetch current time as milliseconds.
// Return as double for high duration and precision.
static inline double GetTime() {
  LARGE_INTEGER perf_time, perf_hz;
  QueryPerformanceFrequency(&perf_hz);  // May change with speed step.
  QueryPerformanceCounter(&perf_time);
  return perf_time.QuadPart * 1000.0 / perf_hz.QuadPart;  // Convert to ms.
}
#endif

namespace switches {
const wchar_t kExit[] = L"exit";
}  // namespace switches


CAppModule g_module;

int Run(wchar_t* win_cmd_line, int cmd_show) {
  base::AtExitManager exit_manager;

  // Windows version of Init uses OS to fetch command line.
  CommandLine::Init(0, NULL);
  const CommandLine* cmd_line = CommandLine::ForCurrentProcess();

  std::vector<std::wstring> filenames(cmd_line->GetLooseValues());

  CMessageLoop the_loop;
  g_module.AddMessageLoop(&the_loop);

  CMainFrame wnd_main;
  if (wnd_main.CreateEx() == NULL) {
    DCHECK(false) << "Main window creation failed!";
    return 0;
  }

  wnd_main.ShowWindow(cmd_show);

  if (!filenames.empty()) {
    const wchar_t* url = filenames[0].c_str();
    wnd_main.MovieOpenFile(url);
  }

  if (cmd_line->HasSwitch(switches::kExit)) {
    wnd_main.OnOptionsExit(0, 0, 0);
  }

  int result = the_loop.Run();

  media::Movie::get()->Close();

  g_module.RemoveMessageLoop();
  return result;
}

int WINAPI _tWinMain(HINSTANCE instance, HINSTANCE /*previous_instance*/,
                     wchar_t* cmd_line, int cmd_show) {
#ifdef TESTING
  double player_time_start = GetTime();
#endif
  INITCOMMONCONTROLSEX iccx;
  iccx.dwSize = sizeof(iccx);
  iccx.dwICC = ICC_COOL_CLASSES | ICC_BAR_CLASSES;
  if (!::InitCommonControlsEx(&iccx)) {
    DCHECK(false) << "Failed to initialize common controls";
    return 1;
  }
  if (FAILED(g_module.Init(NULL, instance))) {
    DCHECK(false) << "Failed to initialize application module";
    return 1;
  }
  int result = Run(cmd_line, cmd_show);

  g_module.Term();
#ifdef TESTING
  double player_time_end = GetTime();
  char outputbuf[512];
  _snprintf_s(outputbuf, sizeof(outputbuf),
              "player time %5.2f ms\n",
              player_time_end - player_time_start);
  OutputDebugStringA(outputbuf);
  printf("%s", outputbuf);
#endif
  return result;
}


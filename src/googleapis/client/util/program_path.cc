/*
 * \copyright Copyright 2013 Google Inc. All Rights Reserved.
 * \license @{
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @}
 */


#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#if defined(_MSC_VER)
#include <windows.h>
#include "googleapis/base/windows_compatability.h"
#else
#include <unistd.h>
#endif
#if __APPLE__
#include "TargetConditionals.h"
#if !TARGET_OS_IPHONE
#define HAVE_LIBPROC
#include <libproc.h>
#endif
#endif

#include <string>
using std::string;

#include "googleapis/client/util/program_path.h"

namespace googleapis {

namespace client {

#ifdef HAVE_LIBPROC

std::string GetCurrentProgramFilenamePath() {  // OSX version
  char buf[PROC_PIDPATHINFO_MAXSIZE];
  int ret = proc_pidpath(getpid(), buf, sizeof(buf));
  if (ret <= 0) {
    // TODO(user): Fix the API to return the path and a status. Logging the
    // error and returning a bad path is useless.
    // LOG(ERROR) << "Could not get pidpath";
    return "./";
  }
  return std::string(buf);
}

#elif defined(_MSC_VER)

std::string GetCurrentProgramFilenamePath() {  // Windows version
  char* value = NULL;
  if (_get_pgmptr(&value)) return "./";

  // Convert windows path to unix style path so our public interface
  // is consistent, especially when we operate on paths.
  return FromWindowsPath(value);
}

#else

std::string GetCurrentProgramFilenamePath() {  // Linux (default) version
  std::string path_to_proc_info("/proc/");
  path_to_proc_info.append(std::to_string(getpid())).append("/exe");
  char buf[PATH_MAX];
  int bytes = readlink(path_to_proc_info.c_str(), buf, sizeof(buf));
  if (bytes <= 0) {
    // LOG(ERROR) << "Could not read " << path_to_proc_info;
    return "./";
  }
  return std::string(buf, bytes);
}

#endif

std::string DetermineDefaultApplicationName() {
  auto program_path(GetCurrentProgramFilenamePath());
  auto basename(Basename(program_path));
  int dot = basename.rfind('.');
  if (dot != std::string::npos) {
    basename = basename.substr(0, dot);
  }
  return basename;
}

std::string Basename(const std::string& path) {
  int slash = path.rfind("/");
  if (slash == std::string::npos) return path;
  return path.substr(slash + 1);
}

std::string StripBasename(const std::string& path) {
  int slash = path.rfind("/");
  if (slash == std::string::npos) return "";
  if (slash == path.size() - 1) {
    return path.substr(0, slash);  // remove trailing slash
  } else {
    return path.substr(0, slash + 1);  // keep trailing slash
  }
}

}  // namespace client

}  // namespace googleapis

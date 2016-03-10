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


#include "googleapis/config.h"

#if !defined(_MSC_VER)
#include <dirent.h>
#else
#include <windows.h>
#include <direct.h>
#include <shellapi.h>
#include <tchar.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <stack>
#include <string>
using std::string;
#include <vector>

#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/strings/util.h"
#include "googleapis/util/file.h"

namespace googleapis {

#ifdef _MSC_VER
static const int kBinaryMode = O_BINARY;
inline string ToNativePath(const string& path) {  return ToWindowsPath(path); }
#else
static const int kBinaryMode = 0;
inline const string& ToNativePath(const string& path) { return path; }
#endif

#if defined(_MSC_VER)

static int mkdir(const string& path, mode_t permissions) {
  string windows_path = ToNativePath(path);

  if (_mkdir(windows_path.c_str()) != 0) {
    LOG(ERROR) << "Could not create directory " << windows_path;
    return -1;
  }

  if (_chmod(windows_path.c_str(), permissions) != 0) {
    LOG(ERROR) << "Could not change permissions on " << windows_path;
    if (_rmdir(windows_path.c_str()) != 0) {
      LOG(ERROR) << "Could not remove directory";
    }
    return -1;
  }

  return 0;
}

#undef open
#define open win_open
static int open(const char* windows_path, int options) {
  int result;
  // permissions would be interesting but ignore for the sake of portability.
  int permissions = _S_IREAD | _S_IWRITE;
  _sopen_s(&result, windows_path, options, _SH_DENYNO, permissions);
  return result;
}

#endif  // #if defined(_MSC_VER)

#if defined(HAVE_FSTAT64)
#define file_STAT64  stat64
#define file_FSTAT64 fstat64
#define file_LSEEK64 lseek64
#else
#define file_STAT64  stat
#define file_FSTAT64 fstat
#define file_LSEEK64 lseek
#endif

static int ModeToOflags(const char* mode, bool* maybe_create) {
  if (!mode || !mode[0]) return -1;

  int default_flags = O_NONBLOCK;
  char code = *mode;
  bool plus = false;
  while (*++mode) {
    switch (*mode) {
      case 'b':
        default_flags |= kBinaryMode;
        break;
      case '+':
        plus = true;
        break;
      default:
        return -1;
    }
  }

  switch (code) {
    case 'r':
      *maybe_create = false;
      return (plus ? O_RDWR : O_RDONLY) | default_flags;
    case 'w':
      *maybe_create = true;
      return (plus ? O_RDWR : O_WRONLY) | O_TRUNC | default_flags;
    case 'a':
      *maybe_create = true;
      return (plus ? O_RDWR : O_WRONLY) | O_APPEND | default_flags;
    default:
      return -1;
  }
}

/* static */
StringPiece File::Basename(const StringPiece& path) {
  int slash = path.rfind("/");
  if (slash == string::npos) return path;
  return path.substr(slash + 1);
}

/* static */
StringPiece File::StripBasename(const StringPiece& path) {
  int slash = path.rfind("/");
  if (slash == string::npos) return "";
  if (slash == path.size() - 1) {
    return path.substr(0, slash);  // remove trailing slash
  } else {
    return path.substr(0, slash + 1);  // keep trailing slash
  }
}

/* static */
bool File::Exists(const string& path) {
  string native_path = ToNativePath(path);
  return access(native_path.c_str(), F_OK) == 0;
}

/* static */
bool File::Delete(const string& path) {
  string native_path = ToNativePath(path);
  if (unlink(native_path.c_str()) < 0) {
    if (errno != ENOENT) {
      LOG(ERROR) << "Could not delete " << path << ": " << strerror(errno);
      return false;
    }
  }
  return true;
}

/* static */
bool File::DeleteDir(const string& path) {
  string native_path = ToNativePath(path);
  if (rmdir(native_path.c_str()) < 0) {
    if (errno != ENOENT) {
      LOG(ERROR) << "Could not delete " << path << ": " << strerror(errno);
      return false;
    }
  }
  return true;
}

/* static */
bool File::RecursivelyDeleteDir(const string& path) {
  std::vector<std::string> subdirs;
  std::vector<std::string> files;
#ifdef _MSC_VER
  string windows_path = ToNativePath(googleapis::StrCat(path, "/*"));
  string dir_str;
  const TCHAR* dir = ToWindowsString(windows_path, &dir_str);
  int len = _tcslen(dir);
  std::unique_ptr<TCHAR[]> shop_from(new TCHAR[len + 2]);
  _tcscpy_s(shop_from.get(), len + 1, dir);
  shop_from[len] = 0;
  shop_from[len + 1] = 0;  // double-null terminate

  SHFILEOPSTRUCT fileop;
  memset(&fileop, 0, sizeof(SHFILEOPSTRUCT));
  fileop.wFunc = FO_DELETE;
  fileop.pFrom = shop_from.get();
  fileop.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
  int ret = SHFileOperation(&fileop);
  bool have_dir = ret != 0;  // We succeeded so the dir must have been there.
#else
  DIR* dir = opendir(path.c_str());
  bool have_dir = dir != NULL;
  if (have_dir) {
    struct dirent ent;
    struct dirent* prev = NULL;
    while ((readdir_r(dir, &ent, &prev) == 0) && prev) {
      ent = *prev;
      StringPiece name(ent.d_name);
      if (name == "." || name == "..") continue;


      if (ent.d_type == DT_DIR) {
        subdirs.push_back(name.as_string());
      } else {
        files.push_back(name.as_string());
      }
    }
    closedir(dir);
  }
#endif
  if (!have_dir) {
    int reason = errno;
    if (File::Exists(path)) {
      LOG(ERROR) << "Could not open directory "
                 << path << ": " << strerror(reason);
      return false;
    }
    return true;
  }

  for (std::vector<std::string>::const_iterator it = subdirs.begin();
       it != subdirs.end();
       ++it) {
    RecursivelyDeleteDir(googleapis::StrCat(path, "/", *it));
  }
  for (std::vector<std::string>::const_iterator it = files.begin();
       it != files.end();
       ++it) {
    File::Delete(googleapis::StrCat(path, "/", *it));
  }
  return File::DeleteDir(path);
}

/* static */
util::Status File::RecursivelyCreateDirWithPermissions(
     const string& path, mode_t permissions) {
  std::stack<StringPiece> to_do;

  int last_slash = path.size();
  while (last_slash > 0) {
    string parent = string(path, 0, last_slash);
    if (File::Exists(parent)) {
      break;
    }
    to_do.push(StringPiece(path).substr(0, last_slash));
    last_slash = path.rfind("/", last_slash - 1);
    if (!last_slash || last_slash == string::npos) break;
  }

  while (!to_do.empty()) {
    string new_path = to_do.top().as_string();
    to_do.pop();
    if (mkdir(new_path.c_str(), permissions) != 0) {
      googleapis::util::Status status(util::error::UNKNOWN,
                          googleapis::StrCat("Could not create directory ", new_path,
                                 ": ", strerror(errno)));
      LOG(ERROR) << status.error_message();
      return status;
    }
  }
  return googleapis::util::Status();  // OK
}


/* static */
File* File::OpenWithOptions(
    const string& path, const char* mode, const FileOpenOptions& options) {
  string native_path = ToNativePath(path);
  bool maybe_create = false;
  int oflags = ModeToOflags(mode, &maybe_create);
  if (oflags < 0) {
    LOG(ERROR) << "Invalid mode=" << mode;
    return NULL;
  }
  int fd;
  if (maybe_create) {
    oflags |= O_CREAT;
  }
#ifndef _MSC_VER
  fd = open(native_path.c_str(), oflags, options.permissions());
#else
  _sopen_s(&fd, native_path.c_str(), oflags | O_CREAT, _SH_DENYNO,
           options.permissions() & (_S_IREAD | _S_IWRITE));
#endif

  if (fd < 0) {
    LOG(ERROR) << "Could not open " << native_path
               << ": " << strerror(errno);
    return NULL;
  }
  if (fd < 0) {
    LOG(ERROR) << "Error opening " << native_path << ": " << strerror(errno);
    return NULL;
  }
  return new File(fd);
}

File::File(int fd) : fd_(fd) {}
File::~File() {
  if (fd_ >= 0) {
    close(fd_);
  }
}

util::Status File::Close(const file::Options&) {
  bool ok = true;
  if (fd_ >= 0) {
    ok = (close(fd_) == 0);
    fd_ = -1;
  }
  delete this;
  return ok ? googleapis::util::Status() : googleapis::util::Status(
      util::error::DATA_LOSS, "Error closing file");
}

util::Status File::Flush() {
#ifdef _MSC_VER
  return googleapis::util::Status();
#else
  do {
    if (fsync(fd_) == 0) return googleapis::util::Status();  // OK
  } while (errno == EINTR || errno == EAGAIN);
  return googleapis::util::Status(
      util::error::UNKNOWN,
      googleapis::StrCat("Could not flush file:", strerror(errno)));
#endif
}

int64 File::Size() {
  struct file_STAT64 info;
  int result = file_FSTAT64(fd_, &info);
  if (result < 0) {
    LOG(ERROR) << "Could not stat file from descriptor";
    return -1;
  }
  return info.st_size;
}

util::Status File::WriteString(const StringPiece& bytes) {
  const char* buffer = bytes.data();
  for (ssize_t len = 0, remaining = bytes.size();
       remaining > 0;
       remaining -= len) {
    len = write(fd_, buffer, remaining);
    if (len < 0) {
      if (errno == EAGAIN) {
        len = 0;
        continue;
      }
      return googleapis::util::Status(util::error::DATA_LOSS, "Error writing to file");
    }
  }
  return googleapis::util::Status();  // OK
}

util::Status File::Read(char* buffer, int64 len, int64* got) {
  for (*got = 0; *got < len; /* empty */) {
    int64 in = read(fd_, buffer, len - *got);
    if (in < 0) {
      if (errno == EAGAIN) {
        continue;
      }
      googleapis::util::Status status =
          googleapis::util::Status(util::error::UNKNOWN,
                       googleapis::StrCat("Error reading from file: ",
                              strerror(errno)));
      LOG(ERROR) << status.error_message();
      return status;
    } else if (in == 0) {
      break;
    }
    *got += in;
    buffer += in;
  }
  return googleapis::util::Status();  // OK
}

/* static */
util::Status File::WritePath(const string& path, const StringPiece& data) {
  File* file = File::Open(path, "wb");
  if (!file) {
    return googleapis::util::Status(util::error::INVALID_ARGUMENT,
                        "Could not write to file");
  }
  googleapis::util::Status status = file->WriteString(data);
  file->Close(file::Defaults()).IgnoreError();
  return status;
}

util::Status File::ReadPath(const string& path, string* s) {
  string native_path = ToNativePath(path);
  s->clear();
  int default_flags = O_SYNC | kBinaryMode;

  int fd = open(native_path.c_str(), O_RDONLY | default_flags);
  if (fd < 0) {
    string error(strerror(errno));
    LOG(ERROR) << "Error opening " << native_path << ": " << error;
    return googleapis::util::Status(util::error::NOT_FOUND, error);
  }

  char buffer[1 << 10];  // Use a 1k buffer to keep down stack size.
  googleapis::util::Status status;
  do {
    ssize_t len = read(fd, buffer, sizeof(buffer));
    if (len == -1) {
      if ((errno == EAGAIN) || (errno == EINTR)) continue;
      string error = strerror(errno);
      LOG(ERROR) << error;
      status = googleapis::util::Status(util::error::DATA_LOSS, error);
      break;
    } else if (!len) {
      break;
    }
    s->append(buffer, len);
  } while (true);
  close(fd);
  return status;
}

util::Status File::Seek(int64 pos, const file::Options& options) {
  while (true) {
    // If seek isnt 64 bit then this will fail if pos is too big.
    // For OSx 10.6 we dont have FSTAT64 but it is 64 bit anyway
    // so not worth checking ourselves since we dont easily know.
    int64 now = static_cast<int64>(file_LSEEK64(fd_, pos, SEEK_SET));
    if (now < 0 && (errno == EINTR || errno == EAGAIN)) {
      continue;
    }
    if (now == pos) {
      return googleapis::util::Status();
    }
    return googleapis::util::Status(util::error::UNKNOWN,
                        googleapis::StrCat("Seek failed. errno=", errno));
  }
}

int64 File::Tell() {
  while (true) {
    int64 now = static_cast<int64>(file_LSEEK64(fd_, 0, SEEK_CUR));
    if (now < 0 && (errno == EINTR || errno == EAGAIN)) {
      continue;
    }
    return now;
  }
}


namespace file {

StringPiece Basename(const StringPiece path) {
  int slash = path.rfind("/");
  if (slash == string::npos) return path;
  return path.substr(slash + 1);
}

}  // namespace file

}  // namespace googleapis

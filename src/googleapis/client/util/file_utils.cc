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



#include <stdlib.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <sys/types.h>
#include <errno.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif


#include "googleapis/client/util/file_utils.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

namespace {
using client::StatusInvalidArgument;
using client::StatusOk;

#ifndef _WIN32
using std::snprintf;
#endif

util::Status CheckPermissions(
     const string& path, bool expect_file, bool allow_writable) {
  const mode_t kPermissionMask = S_IRWXU | S_IRWXG | S_IRWXO;
  struct stat info;
  if (stat(path.c_str(), &info) < 0) {
    return StatusInvalidArgument(StrCat("Could not read from path=", path));
  }

  mode_t permissions = info.st_mode & kPermissionMask;
  bool is_link = S_ISLNK(info.st_mode);
  bool is_dir = S_ISDIR(info.st_mode);
  bool is_regular = S_ISREG(info.st_mode);

  if (is_link) {
    return StatusInvalidArgument(StrCat(path, " is a symbolic link"));
  }

  if (is_dir != !expect_file) {
    return StatusInvalidArgument(
        StrCat(path, " is not a ", expect_file ? "file" : "dir"));
  }
  if (expect_file && !is_regular) {
    return StatusInvalidArgument(StrCat(path, " is not a regular file"));
  }

  if (permissions & ~S_IRWXU) {
    char oct[10];
    snprintf(oct, sizeof(oct), "%o", permissions);
    return StatusInvalidArgument(
        StrCat(path,
               " allows permissions for other users (",
               oct,
               " octal). The file should only allow owner access to ensure its"
               " integrity and protect its contents."));
  }

  if (!allow_writable && permissions & S_IWUSR) {
    return StatusInvalidArgument(StrCat(path, " should not be writable"));
  }

  if (!is_dir && permissions & S_IXUSR) {
    return StatusInvalidArgument(StrCat(path, " should not be executable"));
  }

  return StatusOk();
}

}  // anonymous namespace

namespace client {

util::Status SensitiveFileUtils::VerifyIsSecureFile(
     const string& path, bool allow_writable) {
  return CheckPermissions(path, true, allow_writable);
}

util::Status SensitiveFileUtils::VerifyIsSecureDirectory(
     const string& path) {
  return CheckPermissions(path, false, true);
}

util::Status SensitiveFileUtils::WriteSensitiveStringToFile(
     const StringPiece& data, const string& path, bool overwrite) {
  if (googleapis::File::Exists(path)) {
    if (!overwrite) {
      return StatusInvalidArgument(StrCat(path, " already exists"));
    }

    // Delete the old file securely first, then we'll rewrite it fresh.
    googleapis::util::Status status = DeleteSensitiveFile(path);
  }

  FileOpenOptions options;
  options.set_permissions(S_IRUSR | S_IWUSR);

  File* file = File::OpenWithOptions(path, "w", options);
  if (!file) {
    return StatusUnknown(StrCat("Could not write to ", path));
  }
  googleapis::util::Status status = file->WriteString(data);
  if (!file->Close(file::Defaults()).ok()) {
    return StatusUnknown(StrCat("Failed to close path=", path));
  }
  return status;
}

util::Status SensitiveFileUtils::CreateSecureDirectoryRecursively(
     const string& path) {
  if (googleapis::File::Exists(path)) {
    return VerifyIsSecureDirectory(path);
  }
  return File::RecursivelyCreateDirWithPermissions(
      path, S_IRUSR | S_IWUSR | S_IXUSR);
}

util::Status SensitiveFileUtils::DeleteSensitiveFile(const string& path) {
  struct stat info;
  if (stat(path.c_str(), &info) < 0) {
    if (errno == ENOENT) return StatusOk();
    return StatusFromErrno(errno, StrCat("Could not stat ", path));
  }
  int64 remaining = info.st_size;
  if (remaining > 0) {
    FileOpenOptions options;
    options.set_permissions(S_IRUSR | S_IWUSR);
    File* file = File::OpenWithOptions(path, "r+", options);
    if (!file) {
      return StatusUnknown(StrCat("Could not write to ", path));
    }

    const int64 kMaxWriteChunk = 1 << 13;  // 8K;
    int64 buffer_length = std::min(remaining, kMaxWriteChunk);
    std::unique_ptr<char[]> buffer(new char[buffer_length]);
    memset(buffer.get(), 0xff, buffer_length);

    for (int64 wrote = 0; remaining > 0; remaining -= wrote) {
      int64 this_write = std::min(remaining, buffer_length);
      googleapis::util::Status status = file->Write(buffer.get(), this_write);
      if (!status.ok()) {
        LOG(ERROR) << "Error overwriting secure path=" << path
                   << ": " << status.error_message();
        break;
      }
      wrote = this_write;
    }
    file->Flush();
    file->Close(file::Defaults()).IgnoreError();
  }

  if (!File::Delete(path)) {
    return StatusUnknown(StrCat("Could not delete ", path));
  }

  if (remaining > 0) {
    return StatusDataLoss("Deleted file but not securely");
  }

  return StatusOk();
}
}  // namespace client

}  // namespace googleapis

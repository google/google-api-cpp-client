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
//

#include <memory>
#include <algorithm>
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/data/file_data_writer.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {

FileDataWriter::FileDataWriter(
    const string& path, const FileOpenOptions& options)
      : path_(path), options_(options), file_(NULL) {
}

// When clearing the writer, erase the underlying file.
util::Status FileDataWriter::DoClear() {
  if (File::Exists(path_)) {
    if (!File::Delete(path_)) {
      return StatusUnknown("Could not delete file");
    }
  }
  return StatusOk();
}

util::Status FileDataWriter::DoBegin() {
  if (file_) file_->Close(file::Defaults()).IgnoreError();
  file_ = File::OpenWithOptions(path_, "w", options_);
  if (!file_) {
    googleapis::util::Status status =
        StatusInvalidArgument(StrCat("Could not open ", path_));
    LOG(WARNING) << status.error_message();
    return status;
  }
  return StatusOk();
}

util::Status FileDataWriter::DoEnd() {
  if (file_) {
    bool ok = file_->Close(file::Defaults()).ok();
    file_ = NULL;
    if (!ok) {
      return StatusUnknown("Error closing file");
    }
  }
  return StatusOk();
}

FileDataWriter::~FileDataWriter() {
  if (file_) {
    file_->Flush();
    file_->Close(file::Defaults()).IgnoreError();
  }
}

util::Status FileDataWriter::DoWrite(int64 bytes, const char* buffer) {
  return file_->Write(buffer, bytes);
}

DataReader* FileDataWriter::DoNewDataReader(Closure* deleter) {
  if (file_) file_->Flush();
  return NewManagedFileDataReader(path_, deleter);
}


DataWriter* NewFileDataWriter(const string& path) {
  FileOpenOptions options;
  options.set_permissions(S_IRUSR | S_IWUSR);
  return new FileDataWriter(path, options);
}

DataWriter* NewFileDataWriter(
    const string& path, const FileOpenOptions& options) {
  return new FileDataWriter(path, options);
}

}  // namespace client

}  // namespace googleapis

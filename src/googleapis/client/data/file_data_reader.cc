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


#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

namespace client {

class FileDataReader : public DataReader {
 public:
  FileDataReader(const StringPiece& path, Closure* deleter)
      : DataReader(deleter), file_(NULL) {
    string path_string = path.as_string();
    file_ = File::Open(path_string, "rb");
    if (!file_) {
      set_status(
          StatusInvalidArgument(StrCat("Could not open ", path_string)));
      LOG(WARNING) << "Could not open " << path_string;
      return;
    }

    // We dont need to specify the actual file size. However if we do know
    // the size, it could help consumers of this be better prepared to handle
    // the data, particularly if the size turns out to be big.
    // If this turns out to be a problem, then we can make a variation where
    // the size is injected or an attribute where we explicitly ask to
    // compute it.
    set_total_length(file_->Size());
  }

  virtual ~FileDataReader() {
    if (file_) file_->Close(file::Defaults()).IgnoreError();
  }

  virtual bool seekable() const { return true; }

 protected:
  virtual int64 DoSetOffset(int64 position) {
    if (!file_) {
      set_status(StatusInvalidArgument("File invalid"));
      return -1;
    }

    // We set the length in the constructor so it is known in practice.
    // but we're going to double check here so we dont seek past the EOF.
    int64 len = file_->Size();
    if (position > len) {
      position = len;
    }
    googleapis::util::Status status = file_->Seek(position, file::Defaults());
    if (!status.ok()) {
      set_status(status);
      return -1;
    }

    return position;
  }

  virtual int64 DoReadToBuffer(int64 max_bytes, char* storage) {
    if (!file_) {
      DCHECK(error());
      return 0;
    }
    int64 len;
    googleapis::util::Status status = file_->Read(storage, max_bytes, &len);
    if (!status.ok()) {
      set_status(status);
    } else if (len == 0) {
      set_done(true);
    }
    return len;
  }

 private:
  File* file_;

  DISALLOW_COPY_AND_ASSIGN(FileDataReader);
};

DataReader* NewManagedFileDataReader(
    const StringPiece& path, Closure* deleter) {
  return new FileDataReader(path, deleter);
}

DataReader* NewUnmanagedFileDataReader(const StringPiece& path) {
  return NewManagedFileDataReader(path, NULL);
}

}  // namespace client

// The open source doesnt have File
// so New*FileDataReader will be implemented in istream_data_reader.

}  // namespace googleapis

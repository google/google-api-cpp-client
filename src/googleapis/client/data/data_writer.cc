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


#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/util/status.h"

namespace googleapis {

namespace client {

DataWriter::DataWriter() : size_(0), began_(false) {}
DataWriter::~DataWriter() {}

void DataWriter::Clear() {
  size_ = 0;
  status_ = DoClear();
}

void DataWriter::Begin() {
  size_ = 0;
  status_ = DoBegin();
  began_ = status_.ok();
}

void DataWriter::End() {
  status_ = DoEnd();
}

util::Status DataWriter::Write(int64 bytes, const char* data) {
  if (!began_) {
    VLOG(1) << "AutoBegin";
    Begin();
  }
  if (!status_.ok()) {
    LOG(WARNING) << "Writing to a bad writer fails automatically";
    return status_;
  }
  if (bytes < 0) {
    return StatusInvalidArgument("attempted negative write");
  }
  status_ = DoWrite(bytes, data);
  if (status_.ok()) {
    size_ += bytes;
  }
  return status_;
}

DataReader* DataWriter::NewManagedDataReader(Closure* deleter) {
  if (!status_.ok()) {
    LOG(ERROR) << "Error from bad writer";
    return NewManagedInvalidDataReader(status_, deleter);
  }
  return DoNewDataReader(deleter);
}

util::Status DataWriter::DoClear() { return StatusOk(); }
util::Status DataWriter::DoBegin() { return StatusOk(); }
util::Status DataWriter::DoEnd() { return StatusOk(); }


// TODO(ewiseblatt): 20120306
// Move this to another file
class FileDataWriter : public DataWriter {
 public:
  FileDataWriter(const StringPiece& path, const FileOpenOptions& options)
      : path_(path.as_string()), options_(options), file_(NULL) {
  }

  util::Status DoBegin() {
    if (file_) file_->Close();

    file_ = File::OpenWithOptions(path_.c_str(), "w", options_);
    if (!file_) {
      util::Status status =
          StatusInvalidArgument(StrCat("Could not open ", path_));
      LOG(WARNING) << status.error_message();
      return status;
    }
    return StatusOk();
  }

  util::Status DoEnd() {
    if (file_) {
      bool ok = file_->Close();
      file_ = NULL;
      if (!ok) {
        return StatusUnknown("Error closing file");
      }
    }
    return StatusOk();
  }

  ~FileDataWriter() {
    if (file_) {
      file_->Flush();
      file_->Close();
    }
  }

  virtual util::Status DoWrite(int64 bytes, const char* buffer) {
return file_->Write(buffer, bytes);
  }

  virtual DataReader* DoNewDataReader(Closure* deleter) {
    if (file_) file_->Flush();
    return NewManagedFileDataReader(path_, deleter);
  }

 private:
  string path_;
  FileOpenOptions options_;
  File* file_;
};


// TODO(ewiseblatt): 20120306
// Move this to another file
class StringDataWriter : public DataWriter {
 public:
  StringDataWriter() : storage_(&local_storage_) {}
  explicit StringDataWriter(string* storage) : storage_(storage) {}
  ~StringDataWriter() {}

  virtual util::Status DoClear() {
    storage_->clear();
    return StatusOk();
  }

  virtual util::Status DoBegin() {
    return DoClear();
  }

  virtual util::Status DoWrite(int64 bytes, const char* buffer) {
    storage_->append(buffer, bytes);
    return StatusOk();
  }

  virtual DataReader* DoNewDataReader(Closure* deleter) {
    return NewUnmanagedInMemoryDataReader(*storage_);
  }

 private:
  string local_storage_;
  string* storage_;
};

DataWriter* NewFileDataWriter(const StringPiece& path) {
  FileOpenOptions options;
  options.set_permissions(S_IRUSR | S_IWUSR);
  return new FileDataWriter(path, options);
}

DataWriter* NewFileDataWriter(
    const StringPiece& path, const FileOpenOptions& options) {
  return new FileDataWriter(path, options);
}

DataWriter* NewStringDataWriter(string* s) {
  return new StringDataWriter(s);
}
DataWriter* NewStringDataWriter() {
  return new StringDataWriter();
}

}  // namespace client

} // namespace googleapis
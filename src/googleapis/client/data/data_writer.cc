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


#include <memory>
#include <algorithm>
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/strings/stringpiece.h"

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

util::Status DataWriter::Write(const StringPiece& data) {
    return Write(data.size(), data.data());
}

util::Status DataWriter::Write(DataReader* reader, int64 max_bytes) {
  if (!ok()) return status();  // Writer is in an error state

  if (!reader->done()) {
    if (max_bytes < 0) {
      max_bytes = kint64max;
    }
    int64 reader_remaining = reader->TotalLengthIfKnown() - reader->offset();
    int64 remaining = reader_remaining < 0
        ? max_bytes
        : std::min(reader_remaining, max_bytes);
    if (remaining) {
      const int64 kDefaultChunkSize = 1 << 12;  // 4K max
      int64 chunk_size = std::min(remaining, kDefaultChunkSize);
      std::unique_ptr<char[]> buffer(new char[chunk_size]);

      // Write chunks until we're done or hit an error somewhere
      while (remaining && !reader->done() && ok()) {
        int64 read = reader->ReadToBuffer(std::min(remaining, chunk_size),
                                          buffer.get());
        Write(read, buffer.get()).IgnoreError();
        remaining -= read;
      }
    }
  }

  // If the reader finished in an error state then propagate it to the writer.
  if (reader->error()) {
    set_status(reader->status());
  }

  // Return the writer status.
  return status();
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


// TODO(user): 20120306
// Move this to another file
class StringDataWriter : public DataWriter {
 public:
  StringDataWriter() : storage_(&local_storage_) {}
  explicit StringDataWriter(string* storage) : storage_(storage) {}
  ~StringDataWriter() override {}

  googleapis::util::Status DoClear() override {
    storage_->clear();
    return StatusOk();
  }

  googleapis::util::Status DoBegin() override {
    return DoClear();
  }

  googleapis::util::Status DoWrite(int64 bytes, const char* buffer) override {
    storage_->append(buffer, bytes);
    return StatusOk();
  }

  DataReader* DoNewDataReader(Closure* deleter) override {
    return NewManagedInMemoryDataReader(*storage_, deleter);
  }

 private:
  string local_storage_;
  string* storage_;
};

DataWriter* NewStringDataWriter(string* s) {
  return new StringDataWriter(s);
}
DataWriter* NewStringDataWriter() {
  return new StringDataWriter();
}

}  // namespace client

}  // namespace googleapis

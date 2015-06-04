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


#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/base/macros.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

using std::istream;

namespace {
const int64 kUnknownLength = -1;
}  // anonymous namespace

namespace client {

class IstreamDataReader : public DataReader {
 public:
  IstreamDataReader(istream* stream, int64 total_len, Closure* deleter)
      : DataReader(deleter), stream_(stream) {
    if (total_len != kUnknownLength) {
      set_total_length(total_len);
    }
    int64 pos = stream->tellg();
    if (pos != 0) {
      set_status(StatusInvalidArgument("Stream not at beginning."));
    }
    if (stream->eof()) {
      set_done(true);
    } else if (stream->fail()) {
      set_status(StatusInvalidArgument("Invalid Stream"));
    }
  }

  ~IstreamDataReader() {
  }

  virtual bool seekable() const { return true; }

 protected:
  virtual int64 DoSetOffset(int64 position) {
    stream_->clear();  // clear error flags
    stream_->seekg(position);
    if (stream_->fail() || stream_->bad()) {
      set_status(StatusUnknown("Could not seek stream"));
      return -1;
    }
    return stream_->tellg();
  }

  virtual int64 DoReadToBuffer(int64 max_bytes, char* storage) {
    stream_->read(storage, max_bytes);

    if (stream_->rdstate()) {
      // Note that if we were at the eof and tried to read,
      // then it will set the fail bit. Note also the stream wont necessarily
      // know it is at the eof before the read if it has not yet encountered it.
      if (stream_->eof()) {
        set_done(true);
      } else if (stream_->fail()) {
        set_status(StatusUnknown("Could not read stream"));
      }
    }
    return stream_->gcount();
  }

 private:
  istream* stream_;

  DISALLOW_COPY_AND_ASSIGN(IstreamDataReader);
};

DataReader* NewUnmanagedIstreamDataReader(std::istream* stream) {
  return NewManagedIstreamDataReaderWithLength(stream, kUnknownLength, NULL);
}

DataReader* NewUnmanagedIstreamDataReaderWithLength(
    std::istream* stream, int64 length) {
  return NewManagedIstreamDataReaderWithLength(stream, length, NULL);
}

DataReader* NewManagedIstreamDataReader(
    std::istream* stream, Closure* deleter) {
  return NewManagedIstreamDataReaderWithLength(stream, kUnknownLength, deleter);
}

DataReader* NewManagedIstreamDataReaderWithLength(
    std::istream* stream, int64 length, Closure* deleter) {
  return new IstreamDataReader(stream, length, deleter);
}

}  // namespace client

}  // namespace googleapis

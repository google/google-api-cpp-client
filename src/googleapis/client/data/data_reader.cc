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
// Implements the DataReader base class.

#include <algorithm>
#include <memory>
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {
static const int64 kDefaultBufferSize = 1 << 13;  // 8K

namespace {
struct FreeDeleter {
  inline void operator()(void* ptr) const {
    free(ptr);
  }
};
}  // namespace

DataReader::DataReader(Closure* deleter)
    : deleter_(deleter),
      total_length_(-1), offset_(0), done_(false) {
}

DataReader::~DataReader() {
  if (deleter_) deleter_->Run();
}

bool DataReader::seekable() const { return false; }

void DataReader::set_total_length(int64 length) {
  total_length_ = length;
  if (length == 0) {
    done_ = true;
  }
}

void DataReader::set_status(util::Status status) {
  status_ = status;
  if (!status.ok()) {
    done_ = true;
  }
}

int64 DataReader::SetOffset(int64 position) {
  if (position < 0) {
    set_status(StatusInvalidArgument(StrCat("Negative offset: ", position)));
    offset_ = -1;
    return -1;
  }
  if (position < offset_ || offset_ < 0) {
    // Unset done if we are moving backward, otherwise leave as is.
    done_ = false;
  }
  if (!status_.ok()) {
    // Reset the status
    status_ = StatusOk();
  }
  offset_ = DoSetOffset(position);
  if (offset_ < 0 && status_.ok()) {
    set_status(StatusUnknown(StrCat("Could not seek to ", position)));
  }
  return offset_;
}

int64 DataReader::ReadToString(int64 max_bytes, string* append_to) {
  if (max_bytes < 0) {
    set_status(StatusInvalidArgument("Invalid Argument"));
    return 0;
  }
  int64 len = TotalLengthIfKnown();
  if (len >= 0) {
    int remaining = len - offset();
    if (remaining > 0) {
      append_to->reserve(remaining + append_to->size());
    }
  }

  std::unique_ptr<char, FreeDeleter> buffer(
      reinterpret_cast<char*>(malloc(kDefaultBufferSize)));
  char* storage = buffer.get();
  if (storage == NULL) {
    set_status(StatusResourceExhausted("out of memory"));
    return 0;
  }

  int64 total_read = 0;
  while (total_read < max_bytes && !done()) {
    const int64 bytes_to_read =
        std::min(kDefaultBufferSize, max_bytes - total_read);
    int64 read = DoReadToBuffer(bytes_to_read, storage);
    DCHECK_LE(0, read);  // specialization violated interface
    if (read < 0) {
      set_status(StatusInternalError("Internal Error"));
      return 0;
    }
    if (read) {
      offset_ += read;
      total_read += read;
      append_to->append(storage, read);
    }
  }
  return total_read;
}


int64 DataReader::ReadToBuffer(int64 max_bytes, char* storage) {
  if (max_bytes < 0) {
    set_status(StatusInvalidArgument("negative read"));
  }

  int64 total_read = 0;
  while (total_read < max_bytes && !done()) {
    int64 read = DoReadToBuffer(max_bytes - total_read, storage + total_read);
    DCHECK_LE(0, read);  // specialization violated interface
    if (read < 0) {
      set_status(StatusInternalError("Internal Error"));
      return 0;
    }
    offset_ += read;
    total_read += read;
  }

  return total_read;
}

bool DataReader::ReadUntilPatternInclusive(
    const string &pattern, string* consumed) {
  consumed->clear();
  bool result = DoAppendUntilPatternInclusive(pattern, consumed);
  offset_ += consumed->size();
  return result;
}

int64 DataReader::DoSetOffset(int64 position) {
  set_status(StatusUnimplemented("Reader cannot seek to offset"));
  return -1;
}

bool DataReader::DoAppendUntilPatternInclusive(
    const string& pattern, string* consumed) {
  const char* find = pattern.c_str();
  int64 found_offset = consumed->size();
  while (*find) {
    if (done()) return false;
    int64 original_size = consumed->size();
    char c;
    if (!DoReadToBuffer(1, &c)) continue;
    consumed->push_back(c);
    if (c == *find) {
      ++find;
      continue;
    }

    // We arent matching the pattern so reset our pattern.
    // But if the pattern has a repeated substring then we might still be
    // in the middle of matching it from an offset between the old
    // found_offset and the end of the consumed string. So let's look for
    // one.
    //
    // This wont be the case if we werent in the middle of matching a string
    if (found_offset == original_size) {
      ++found_offset;
      continue;
    }

    const char* end_consumed = consumed->c_str() + consumed->size();
    while ((found_offset = consumed->find(
               pattern.c_str(), found_offset + 1, 1)) != string::npos) {
      const char* have;
      for (have = consumed->c_str() + found_offset + 1,
               find = pattern.c_str() + 1;
           have < end_consumed && *find == *have;
           ++find, ++have) {
      }
      if (have == end_consumed) break;

      // Start over again from a found_offset closer to the end of the
      // consumed string.
    }

    if (found_offset == string::npos) {
      // The consumed string so far doesnt end with a substring of the pattern.
      found_offset = consumed->size();
      find = pattern.c_str();
    }
  }

  return true;
}

class InvalidDataReader : public DataReader {
 public:
  InvalidDataReader(util::Status status, Closure* deleter)
      : DataReader(deleter), status_(status) {
    set_status(status);
  }

 protected:
  virtual int64 DoSetOffset(int64 offset) {
    set_status(status_);
    return -1;
  }

  virtual int64 DoReadToBuffer(int64 max_bytes, char* storage) { return 0; }

 private:
  googleapis::util::Status status_;
};

DataReader* NewManagedInvalidDataReader(
    googleapis::util::Status status, Closure* deleter) {
  return new InvalidDataReader(status, deleter);
}

}  // namespace client

}  // namespace googleapis

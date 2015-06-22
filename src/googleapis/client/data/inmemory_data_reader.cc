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


#include <algorithm>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {

class InMemoryDataReader : public DataReader {
 public:
  InMemoryDataReader(const StringPiece& data, Closure* deleter)
      : DataReader(deleter), data_(data) {
    set_total_length(data_.size());
  }

  virtual ~InMemoryDataReader() {
  }

  virtual bool seekable() const { return true; }

 protected:
  virtual int64 DoSetOffset(int64 position) {
    if (position > data_.size()) return data_.size();

    // The base class keeps track of offset and will update on return.
    return position;
  }

  virtual int64 DoReadToBuffer(int64 max_bytes, char* storage) {
    int64 remaining = data_.size() - offset();
    if (remaining == 0) {
      set_done(true);
      return 0;
    }
    int64 read = std::min(max_bytes, remaining);
    if (read > 0) {
      memcpy(storage, data_.data() + offset(), read);
      if (read == remaining) {
        // We'll be aggressive here though it's still legal if we dont
        // yet know we're done.
        set_done(true);
      }
    }
    return read;
  }

  virtual bool DoAppendUntilPatternInclusive(
      const string& pattern, string* consumed) {
    int64 start = offset();
    int64 found = data_.find(pattern, start);
    int64 end = found == string::npos ? data_.size() : found + pattern.size();
    StrAppend(consumed, data_.substr(start, end - start));
    return found != StringPiece::npos;
  }

 private:
  StringPiece data_;
};

DataReader* NewUnmanagedInMemoryDataReader(const StringPiece& data) {
  return new InMemoryDataReader(data, NULL);
}

DataReader* NewManagedInMemoryDataReader(
    const StringPiece& data, Closure* deleter) {
  return new InMemoryDataReader(data, deleter);
}

DataReader* NewManagedInMemoryDataReader(string* str) {
  return NewManagedInMemoryDataReader(
      StringPiece(*str), DeletePointerClosure(str));
}

}  // namespace client

}  // namespace googleapis

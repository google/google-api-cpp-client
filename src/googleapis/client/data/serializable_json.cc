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


#include <istream>  // NOLINT
#include <ostream>  // NOLINT
#include <memory>
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/serializable_json.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {

SerializableJson::~SerializableJson() {
}

util::Status SerializableJson::LoadFromJsonStream(std::istream* stream) {
  std::unique_ptr<DataReader> reader(NewUnmanagedIstreamDataReader(stream));
  if (!reader->ok()) {
    return reader->status();
  }
  return LoadFromJsonReader(reader.get());
}

util::Status SerializableJson::StoreToJsonStream(
     std::ostream* stream) const {
  std::unique_ptr<DataReader> reader(MakeJsonReader());
  if (!reader->ok()) return reader->status();

  string data = reader->RemainderToString();
  if (!reader->ok()) return reader->status();

  *stream << data;
  if (stream->fail()) {
    return StatusUnknown(StrCat("Error writing ", data.size(), "bytes"));
  }
  return StatusOk();
}

}  // namespace client

}  // namespace googleapis

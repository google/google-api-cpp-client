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
#include "googleapis/client/data/jsoncpp_data.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include <json/reader.h>
#include <json/writer.h>

namespace googleapis {

namespace client {

JsonCppData::JsonCppData(const Json::Value& value)
  : is_mutable_(false), value_(const_cast<Json::Value*>(&value)) {
}
JsonCppData::JsonCppData(Json::Value* value)
  : is_mutable_(true), value_(value) {
}
JsonCppData::~JsonCppData() {
}

void JsonCppData::CheckIsMutable() const {
  CHECK(is_mutable_);
}

DataReader* JsonCppData::MakeJsonReader() const {
  Json::FastWriter writer;
  const string& json = writer.write(Storage());
  return NewManagedInMemoryDataReader(json);  // copy the data
}

util::Status JsonCppData::LoadFromJsonReader(DataReader* data_reader) {
  string storage = data_reader->RemainderToString();
  if (!data_reader->ok()) {
    return data_reader->status();
  }

  const char* data = storage.data();
  int64 size = storage.size();
  Json::Reader json_reader;
  bool result = json_reader.parse(data, data + size, *MutableStorage(), false);
  if (!result) {
    return StatusInvalidArgument(json_reader.getFormatedErrorMessages());
  }
  return StatusOk();
}

util::Status JsonCppData::LoadFromJsonStream(std::istream* stream) {
  Json::Reader reader;
  bool result = reader.parse(*stream, *MutableStorage());
  if (!result) {
return StatusInvalidArgument(reader.getFormatedErrorMessages());
  }
  return StatusOk();
}

util::Status JsonCppData::StoreToJsonStream(std::ostream* stream) const {
  Json::StyledStreamWriter writer;
  writer.write(*stream, Storage());
  if (stream->fail()) {
    return StatusUnknown("Error storing JSON");
  }
  return StatusOk();
}

void JsonCppData::Clear() {
  MutableStorage()->clear();
}

JsonCppDictionary::JsonCppDictionary(const Json::Value& value)
  : JsonCppData(value) {
}

JsonCppDictionary::JsonCppDictionary(Json::Value* value)
  : JsonCppData(value) {
}

}  // namespace client

}  // namespace googleapis

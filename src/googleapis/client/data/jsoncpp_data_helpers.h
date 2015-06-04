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
// The Json discovery documents specify 64 bit integers as strings, so we
// need to serialize/deserialize them as strings.

#ifndef GOOGLEAPIS_DATA_JSONCPP_DATA_HELPERS_H_
#define GOOGLEAPIS_DATA_JSONCPP_DATA_HELPERS_H_

#include <string>
using std::string;
#include "googleapis/client/util/date_time.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/strings/numbers.h"
#include <json/json.h>
namespace googleapis {

namespace client {

// Converts a Json::Value into C++ value.
// This just normalizes the third party api so that we dont have to
// special case the name of the method for each different template type.
template<typename T> void ClearCppValueHelper(T* value);
template<typename T>
const T JsonValueToCppValueHelper(const Json::Value& value);
template<typename T>
T JsonValueToMutableCppValueHelper(Json::Value* value);
template<typename T>
void SetJsonValueFromCppValueHelper(const T& value, Json::Value* storage);
template<typename T>
void SetCppValueFromJsonValueHelper(const Json::Value& storage, T* value);

// suppress warnings about const POD return types
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#endif

// Explicit templated get implementation specific to int.
template<>
inline void ClearCppValueHelper<int16>(int16* value) { *value = 0; }
template<>
inline const int16 JsonValueToCppValueHelper<int16>(const Json::Value& value) {
  return value.asInt();
}
template<>
inline int16 JsonValueToMutableCppValueHelper<int16>(Json::Value* value) {
  return value->asInt();
}
template<>
inline void SetJsonValueFromCppValueHelper<int16>(
    const int16& val, Json::Value* storage) {
  *storage = val;
}
template<>
inline void SetCppValueFromJsonValueHelper<int16>(
    const Json::Value& storage, int16* value) {
  *value = storage.asInt();
}

template<>
inline void ClearCppValueHelper<int32>(int32* value) { *value = 0; }
template<>
inline const int32 JsonValueToCppValueHelper<int32>(const Json::Value& value) {
  return value.asInt();
}
template<>
inline int32 JsonValueToMutableCppValueHelper<int32>(Json::Value* value) {
  return value->asInt();
}
template<>
inline void SetJsonValueFromCppValueHelper<int32>(
     const int32& val, Json::Value* storage) {
  *storage = val;
}
template<>
inline void SetCppValueFromJsonValueHelper<int32>(
    const Json::Value& storage, int32* value) {
  *value = storage.asInt();
}

template<>
inline void ClearCppValueHelper<int64>(int64* value) { *value = 0L; }
template<>
inline const int64 JsonValueToCppValueHelper<int64>(const Json::Value& value) {
  if (value.isNull()) return 0L;
  return ParseLeadingInt64Value(value.asCString(), 0L);
}
template<>
inline int64 JsonValueToMutableCppValueHelper<int64>(Json::Value* value) {
  return JsonValueToCppValueHelper<int64>(*value);
}
template<>
inline void SetJsonValueFromCppValueHelper<int64>(
     const int64& val, Json::Value* storage) {
  *storage = SimpleItoa(val).c_str();
}
template<>
inline void SetCppValueFromJsonValueHelper<int64>(
    const Json::Value& storage, int64* value) {
  *value = JsonValueToCppValueHelper<int64>(storage);
}

template<>
inline void ClearCppValueHelper<uint16>(uint16* value) { *value = 0; }
template<>
inline const uint16 JsonValueToCppValueHelper<uint16>(
    const Json::Value& value) {
  return value.asUInt();
}
template<>
inline uint16 JsonValueToMutableCppValueHelper<uint16>(Json::Value* value) {
  return value->asUInt();
}
template<>
inline void SetJsonValueFromCppValueHelper<uint16>(
     const uint16& val, Json::Value* storage) {
  *storage = val;
}
template<>
inline void SetCppValueFromJsonValueHelper<uint16>(
    const Json::Value& storage, uint16* value) {
  *value = storage.asUInt();
}

template<>
inline void ClearCppValueHelper<uint32>(uint32* value) { *value = 0; }
template<>
inline const uint32 JsonValueToCppValueHelper<uint32>(
    const Json::Value& value) {
  return value.asUInt();
}
template<>
inline uint32 JsonValueToMutableCppValueHelper<uint32>(Json::Value* value) {
  return value->asUInt();
}
template<>
inline void SetJsonValueFromCppValueHelper<uint32>(
     const uint32& val, Json::Value* storage) {
  *storage = val;
}
template<>
inline void SetCppValueFromJsonValueHelper<uint32>(
    const Json::Value& storage, uint32* value) {
  *value = storage.asUInt();
}

template<>
inline void ClearCppValueHelper<uint64>(uint64* value) { *value = 0L; }
template<>
inline const uint64 JsonValueToCppValueHelper<uint64>(
    const Json::Value& value) {
  if (value.isNull()) return 0L;
  return ParseLeadingUInt64Value(value.asCString(), 0L);
}
template<>
inline uint64 JsonValueToMutableCppValueHelper<uint64>(Json::Value* value) {
  return JsonValueToCppValueHelper<uint64>(*value);
}
template<>
inline void SetJsonValueFromCppValueHelper<uint64>(
     const uint64& val, Json::Value* storage) {
  *storage = SimpleItoa(val).c_str();
}
template<>
inline void SetCppValueFromJsonValueHelper<uint64>(
    const Json::Value& storage, uint64* value) {
  *value = JsonValueToCppValueHelper<uint64>(storage);
}

template<>
inline void ClearCppValueHelper<bool>(bool* value) { *value = false; }
template<>
inline const bool JsonValueToCppValueHelper<bool>(const Json::Value& value) {
  return value.asBool();
}
template<>
inline bool JsonValueToMutableCppValueHelper<bool>(Json::Value* value) {
  return value->asBool();
}
template<>
inline void SetJsonValueFromCppValueHelper<bool>(
     const bool& val, Json::Value* storage) {
  *storage = val;
}
template<>
inline void SetCppValueFromJsonValueHelper<bool>(
    const Json::Value& storage, bool* value) {
  *value = storage.asBool();
}

template<>
inline void ClearCppValueHelper<float>(float* value) { *value = 0; }
template<>
inline const float JsonValueToCppValueHelper<float>(const Json::Value& value) {
  return static_cast<float>(value.asDouble());
}
template<>
inline float JsonValueToMutableCppValueHelper<float>(Json::Value* value) {
  return static_cast<double>(value->asDouble());
}
template<>
inline void SetJsonValueFromCppValueHelper<float>(
     const float& val, Json::Value* storage) {
  *storage = val;
}
template<>
inline void SetCppValueFromJsonValueHelper<float>(
    const Json::Value& storage, float* value) {
  *value = static_cast<float>(storage.asDouble());
}

template<>
inline void ClearCppValueHelper<double>(double* value) { *value = 0; }
template<>
inline const double JsonValueToCppValueHelper<double>(
    const Json::Value& value) {
  return value.asDouble();
}
template<>
inline double JsonValueToMutableCppValueHelper<double>(Json::Value* value) {
  return value->asDouble();
}
template<>
inline void SetJsonValueFromCppValueHelper<double>(
     const double& val, Json::Value* storage) {
  *storage = val;
}
template<>
inline void SetCppValueFromJsonValueHelper<double>(
    const Json::Value& storage, double* value) {
  *value = storage.asDouble();
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

template<>
inline void ClearCppValueHelper<string>(string* value) { value->clear(); }
template<>
inline const string JsonValueToCppValueHelper<string>(
    const Json::Value& value) {
  return value.asString();
}
template<>
inline string JsonValueToMutableCppValueHelper<string>(Json::Value* value) {
  return value->asString();
}
template<>
inline void SetJsonValueFromCppValueHelper<string>(
     const string& val, Json::Value* storage) {
  *storage = val.c_str();
}
template<>
inline void SetCppValueFromJsonValueHelper<string>(
    const Json::Value& storage, string* value) {
  *value = storage.asString();
}

template<>
inline void ClearCppValueHelper<Date>(Date* value) {
  *value = Date();
}
template<>
inline const Date JsonValueToCppValueHelper<Date>(const Json::Value& value) {
  return Date(value.asString());
}
template<>
inline Date JsonValueToMutableCppValueHelper<Date>(Json::Value* value) {
  return JsonValueToCppValueHelper<Date>(*value);
}
template<>
inline void SetJsonValueFromCppValueHelper<Date>(
     const Date& val, Json::Value* storage) {
  *storage = val.ToYYYYMMDD().c_str();
}
template<>
inline void SetCppValueFromJsonValueHelper<Date>(
    const Json::Value& storage, Date* value) {
  *value = Date(storage.asString());
}

template<>
inline void ClearCppValueHelper<DateTime>(DateTime* value) {
  *value = DateTime();
}
template<>
inline const DateTime JsonValueToCppValueHelper<DateTime>(
    const Json::Value& value) {
  return DateTime(value.asString());
}
template<>
inline DateTime JsonValueToMutableCppValueHelper<DateTime>(Json::Value* value) {
  return DateTime(value->asString());
}
template<>
inline void SetJsonValueFromCppValueHelper<DateTime>(
     const DateTime& val, Json::Value* storage) {
  *storage = val.ToString().c_str();
}
template<>
inline void SetCppValueFromJsonValueHelper<DateTime>(
    const Json::Value& storage, DateTime* value) {
  *value = DateTime(storage.asString());
}

// generic is for objects
template<typename T>
inline void ClearCppValueHelper(T* value) {
  *value->MutableStorage() = Json::Value::null;
}
template<typename T>
inline const T JsonValueToCppValueHelper(const Json::Value& value) {
  return T(value);
}
template<typename T>
inline T JsonValueToMutableCppValueHelper(Json::Value* value) {
  return T(value);
}
template<typename T>
inline void SetJsonValueFromCppValueHelper(
     const T& val, Json::Value* storage) {
  *storage = val.Storage();
}
template<typename T>
inline void SetCppValueFromJsonValueHelper(
    const Json::Value& storage, T* value) {
  *value->MutableStorage() = storage;
}

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_JSONCPP_DATA_HELPERS_H_

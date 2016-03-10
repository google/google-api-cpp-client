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


#include <float.h>
#include <cmath>
#include <set>
#include <map>
#include <memory>
#include <string>
using std::string;
#include <sstream>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/jsoncpp_data.h"
#include "googleapis/client/data/jsoncpp_data_helpers.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/strings/join.h"
#include "googleapis/strings/numbers.h"
#include "googleapis/strings/strip.h"
#include "googleapis/strings/stringpiece.h"
#include <gtest/gtest.h>
#include <json/value.h>
#include <json/writer.h>

namespace googleapis {

using std::ostringstream;
using std::istringstream;

using client::ClearCppValueHelper;
using client::DataReader;
using client::JsonCppArray;
using client::JsonCppAssociativeArray;
using client::JsonCppCapsule;
using client::JsonCppData;
using client::JsonValueToCppValueHelper;
using client::JsonValueToMutableCppValueHelper;
using client::SetJsonValueFromCppValueHelper;
using client::SetCppValueFromJsonValueHelper;

// An example object with some fields for testing purposes.
// This is what a generated class might look like.
// The class has simple fields as well as a composite "linked-list"
// as an interesting dynamic type element.
class ExampleJsonObject : public JsonCppData {
 public:
  explicit ExampleJsonObject(const Json::Value& storage)
      : JsonCppData(storage) {}
  explicit ExampleJsonObject(Json::Value* storage)
      : JsonCppData(storage) {}

  int number() const { return Storage("number").asInt(); }
  void set_number(int val) { *MutableStorage("number") = val; }

  const StringPiece str() const {
    return StringPiece(Storage("str").asCString());
  }
  void set_str(const StringPiece& s) {
    *MutableStorage("str") = s.data();
  }

  const ExampleJsonObject next() const {
    return ExampleJsonObject(Storage("next"));
  }
  ExampleJsonObject next_mutable() {
    return ExampleJsonObject(MutableStorage("next"));
  }
};

// Test fixture for this gunit test.
class JsonCppAdapterFixture : public testing::Test {
 public:
  static void InitArray(Json::Value* obj, int n) {
    for (int i = 0; i < n; i++) {
      (*obj)[i] = 10 * i;
    }
  }

  static void InitStringArray(Json::Value* obj, int n) {
    for (int i = 0; i < n; i++) {
      (*obj)[i] = StrCat("Test ", i).c_str();
    }
  }

  static void InitDictionary(
      Json::Value* obj, std::map<string, Json::Value>* dict) {
    dict->clear();
    (*dict)["1"] = 1;
    (*dict)["2"] = "two";
    (*dict)["3"] = 3.14169;

    for (std::map<string, Json::Value>::const_iterator it = dict->begin();
         it != dict->end();
         ++it) {
      (*obj)[it->first] = it->second;
    }
  }

  static void InitDictArrayOfDicts(Json::Value* obj) {
    // outer dictionary is indexed by number strings and text strings
    // first two entries are terminals, remaining are arrays.
    for (int outer_dict = 0; outer_dict < 5; ++outer_dict) {
      Json::Value* outer;
      if (outer_dict < 4) {
        outer = &(*obj)[StrCat("", outer_dict)];
        if (outer_dict < 2) {
          if (outer_dict == 1) {
            *outer = "Hello, World!";
          } else {
            *outer = 0;
          }
          continue;
        }
      } else {
        outer = &(*obj)[StrCat("Outer ", outer_dict)];
      }

      // These arrays are asymetric in size.
      for (int index = 0; index < 10 + outer_dict; ++index) {
        Json::Value* element = &(*outer)[index];
        for (int inner_dict = 0; inner_dict < 3; ++inner_dict) {
          // Give unique values.
          (*element)[StrCat("Inner ", inner_dict)] =
              100 * outer_dict + 10 * index + inner_dict;
        }
      }
    }
  }

  template<typename T>
  void TestHelper(
      const T& value_to_set, const T& default_value) {
    Json::Value storage;
    T c_value = default_value;

    SetJsonValueFromCppValueHelper<T>(value_to_set, &storage);
    EXPECT_EQ(value_to_set, JsonValueToCppValueHelper<T>(storage));
    EXPECT_EQ(value_to_set, JsonValueToMutableCppValueHelper<T>(&storage));

    SetCppValueFromJsonValueHelper<T>(storage, &c_value);
    EXPECT_EQ(value_to_set, c_value);

    ClearCppValueHelper(&c_value);
    EXPECT_EQ(default_value, c_value);
  }
};

TEST_F(JsonCppAdapterFixture, Helpers) {
  TestHelper<bool>(true, false);
  TestHelper<bool>(false, false);

  TestHelper<int16>(kint16min, 0);
  TestHelper<int16>(kint16max, 0);

  TestHelper<uint16>(kint16max, 0);
  TestHelper<uint16>(kuint16max, 0);

  TestHelper<int32>(kint32min, 0);
  TestHelper<int32>(kint32max, 0);

  TestHelper<uint32>(kint32max, 0);
  TestHelper<uint32>(kuint32max, 0);

  TestHelper<int64>(kint32min, 0);
  TestHelper<int64>(kint32max, 0);
  TestHelper<int64>(kint64min, 0);
  TestHelper<int64>(kint64max, 0);

  TestHelper<uint64>(kint32max, 0);
  TestHelper<uint64>(kint64max, 0);
  TestHelper<uint64>(kuint32max, 0);
  TestHelper<uint64>(kuint64max, 0);

  TestHelper<float>(FLT_MIN, 0);
  TestHelper<float>(FLT_MAX, 0);

  TestHelper<double>(FLT_MIN, 0);
  TestHelper<double>(FLT_MAX, 0);
  TestHelper<double>(DBL_MIN, 0);
  TestHelper<double>(DBL_MAX, 0);

  const string empty;
  const string hello = "Hello, World!";
  const string json = "{\n foo: \"bar\"\n}\n";

  TestHelper<string>(hello, empty);
  TestHelper<string>(json, empty);
}

TEST_F(JsonCppAdapterFixture, StoreEmpty) {
  JsonCppCapsule<JsonCppData> data;
  ostringstream stream;
  EXPECT_TRUE(data.StoreToJsonStream(&stream).ok());
  string json = stream.str();
  RemoveExtraWhitespace(&json);
  EXPECT_EQ("null", json);
}

TEST_F(JsonCppAdapterFixture, LoadEmpty) {
  JsonCppCapsule<JsonCppData> data;
  string json = "{}";
  istringstream in(json);
  googleapis::util::Status status = data.LoadFromJsonStream(&in);
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_EQ(0, data.Storage().size());

  std::unique_ptr<DataReader> reader(data.MakeJsonReader());
  EXPECT_TRUE(status.ok()) << status.ToString();
  JsonCppCapsule<JsonCppData> check;
  status = check.LoadFromJsonReader(reader.get());
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_EQ(data.Storage(), check.Storage());
}

TEST_F(JsonCppAdapterFixture, LoadStoreComplex) {
  Json::Value value;
  InitDictArrayOfDicts(&value);

  // To generate the json string, we're going to write out the complex dict
  // we just built into the "complex_json" string.
  JsonCppData prototype(value);
  ostringstream output;
  EXPECT_TRUE(prototype.StoreToJsonStream(&output).ok());
  const string& complex_json = output.str();

  // Now we'll read that complex dict and compare the resulting value
  // against the original value we manually constructed.
  JsonCppCapsule<JsonCppData> got;
  istringstream input(complex_json);
  googleapis::util::Status status = got.LoadFromJsonStream(&input);
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_TRUE(value == got.Storage());

  // And just to be sure, we'll check a specific value.
  EXPECT_EQ("Hello, World!", StringPiece(got.Storage("1").asCString()));

  std::unique_ptr<DataReader> reader(got.MakeJsonReader());
  EXPECT_TRUE(status.ok()) << status.ToString();
  JsonCppCapsule<JsonCppData> check;
  status = check.LoadFromJsonReader(reader.get());
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_EQ(got.Storage(), check.Storage());
}

TEST_F(JsonCppAdapterFixture, LoadStoreComplexUsingOperators) {
  Json::Value value;
  InitDictArrayOfDicts(&value);

  // To generate the json string, we're going to write out the complex dict
  // we just built into the "complex_json" string.
  JsonCppData prototype(value);
  ostringstream output;
  output << prototype;
  const string& complex_json = output.str();

  // Now we'll read that complex dict and compare the resulting value
  // against the original value we manually constructed.
  JsonCppCapsule<JsonCppData> got;
  istringstream input(complex_json);
  input >> got;
  EXPECT_TRUE(value == got.Storage());

  // And just to be sure, we'll check a specific value.
  EXPECT_EQ("Hello, World!", StringPiece(got.Storage("1").asCString()));
}

// Test our simple specialized class example.
TEST_F(JsonCppAdapterFixture, TestSimpleExample) {
  JsonCppCapsule<ExampleJsonObject> example;
  example.set_number(1);
  example.set_str("one");
  EXPECT_EQ(1, example.number());
  EXPECT_EQ("one", example.str());
}

// Test the composite "linked list" nature of our example.
TEST_F(JsonCppAdapterFixture, TestExamplePointers) {
  JsonCppCapsule<ExampleJsonObject> example;

  example.set_number(1);
  example.set_str("one");
  ExampleJsonObject next = example.next_mutable();
  next.set_number(2);
  next.set_str("two");

  EXPECT_EQ(1, example.number());
  EXPECT_EQ(2, example.next().number());
}

// Test we can read from a read-only dictionary (and not modify it).
TEST_F(JsonCppAdapterFixture, TestReadDict) {
  Json::Value storage;
  std::map<string, Json::Value> expect;
  InitDictionary(&storage, &expect);

  JsonCppAssociativeArray<JsonCppData> dict(storage);
  EXPECT_TRUE(dict.has("1"));
  EXPECT_FALSE(dict.has("-1"));

  EXPECT_EQ(expect.size(), dict.size());
  for (int i = 0; i < dict.size(); ++i) {
    string key = SimpleItoa(i + 1);
    JsonCppCapsule<JsonCppData> value;
    EXPECT_TRUE(dict.get(key.c_str(), &value)) << key;
    EXPECT_EQ(expect[key], value.Storage()) << key;
  }

  // We constructed the dict above as immutable
  // (giving it storage rather than &storage).
  // Test the immutability behavior.
  JsonCppCapsule<JsonCppData> value;
  EXPECT_DEATH(dict.Clear(), "mutable");
  EXPECT_DEATH(dict.remove("Bogus"), "mutable");
  EXPECT_DEATH(dict.put("Bogus", value), "mutable");

  EXPECT_FALSE(dict.get("Bogus", &value));
}

TEST_F(JsonCppAdapterFixture, TestAssociativeArrays) {
  JsonCppCapsule<JsonCppAssociativeArray<int> > dictInt;
  JsonCppCapsule<JsonCppAssociativeArray<string> > dictString;
  JsonCppCapsule<JsonCppAssociativeArray<JsonCppArray<int> > >dictArray;

  for (int i = 0; i < 10; ++i) {
    string key = SimpleItoa(i);
    dictInt.put(key.c_str(), 10 * i);
    dictString.put(key.c_str(), SimpleItoa(10 * i));
    JsonCppCapsule<JsonCppArray<int> > array;
    for (int j = 0; j < 3; ++j) {
      array.set(j, -10 * i - j);
    }
    dictArray.put(key.c_str(), array);
  }
  EXPECT_EQ(10, dictInt.size());
  EXPECT_EQ(10, dictString.size());

  // Test iterating over primtives
  // Use the basic iterator to test that
  std::set<string> foundInt;
  for (JsonCppAssociativeArray<int>::const_iterator it = dictInt.begin();
       it != dictInt.end();
       ++it) {
    EXPECT_EQ(it.key(), (*it).first);
    EXPECT_EQ(it.value(), (*it).second);
    foundInt.insert(it.key());
  }
  EXPECT_EQ(10, foundInt.size());

  // Test iterating over strings
  // Use C++11 style iteration to test that
  std::set<string> foundString;
  for (JsonCppAssociativeArray<string>::const_iterator elem =
           dictString.begin();
       elem != dictString.end();
       ++elem) {
    foundString.insert(elem.key());

    // value was 10-times key
    if (elem.key() == "0") {
      EXPECT_EQ("0", elem.value());
    } else {
      EXPECT_EQ(StrCat(elem.key(), "0"), elem.value());
    }
  }
  EXPECT_EQ(10, foundString.size());

  // Test iterating over arrays
  for (JsonCppAssociativeArray<JsonCppArray<int> >::const_iterator elem =
           dictArray.begin();
       elem != dictArray.end();
       ++elem) {
    const JsonCppArray<int>& array = elem.value();
    int32 n;
    ASSERT_TRUE(safe_strto32(elem.key().c_str(), &n));
    for (int j = 0; j < 3; ++j) {
      EXPECT_EQ(-10 * n - j, array.get(j));
    }
  }

  // Test explicit lookups and objects
  for (int i = 0; i < 10; ++i) {
    const string key = SimpleItoa(i);
    EXPECT_TRUE(foundInt.find(key) != foundInt.end()) << key;

    string str;
    int n;
    EXPECT_TRUE(dictInt.get(key.c_str(), &n));
    EXPECT_EQ(10 * i, n);

    EXPECT_TRUE(dictString.get(key.c_str(), &str));
    EXPECT_EQ(SimpleItoa(10 * i), str);

    JsonCppCapsule<JsonCppArray<int> > array;
    EXPECT_TRUE(dictArray.get(key.c_str(), &array));
    for (int j = 0; j < 3; ++j) {
      EXPECT_EQ(-10 * i - j, array.get(j));
    }
  }

  // Test lookup failures.
  string str = "bogus";
  EXPECT_FALSE(dictString.get("Bogus", &str));
  EXPECT_EQ("", str);

  int n = -1;
  EXPECT_FALSE(dictInt.get("Bogus", &n));
  EXPECT_EQ(0, n);
}

// Test that we can read from an immutable array.
// (and that we cannot modify it)
TEST_F(JsonCppAdapterFixture, TestReadArray) {
  const int kSize = 10;
  Json::Value storage;
  InitArray(&storage, kSize);
  JsonCppArray<int> array(storage);

  EXPECT_EQ(kSize, array.size());
  EXPECT_EQ(&storage, &array.Storage());
  EXPECT_DEATH(array.MutableStorage(), "mutable");
  for (int i = 0; i < kSize; ++i) {
    // Test the primitive getter
    EXPECT_EQ(10 * i, array.get(i));

    // Test the value object getter
    EXPECT_EQ(10 * i, array.as_value(i).asInt());

    // Test that the object getter is based on the same underlying value object
    EXPECT_EQ(&array.as_value(i), &array.as_object(i).Storage());
  }
}

// Test that we can read and write to a mutable array
// (and changes are visible as expected).
TEST_F(JsonCppAdapterFixture, TestWriteArray) {
  const int kSize = 10;
  Json::Value storage;
  InitArray(&storage, kSize);
  JsonCppArray<int> writable_array(&storage);
  const JsonCppArray<int> readable_array(storage);

  EXPECT_EQ(kSize, readable_array.size());
  EXPECT_EQ(kSize, writable_array.size());
  for (int i = 0; i < kSize; ++i) {
    EXPECT_EQ(10 * i, readable_array.get(i));
    EXPECT_EQ(10 * i, writable_array.get(i));
    writable_array.set(i, -10 * i);
    EXPECT_EQ(-10 * i, readable_array.get(i));
    EXPECT_EQ(-10 * i, writable_array.get(i));

    EXPECT_EQ(readable_array[i], writable_array[i]);
    EXPECT_EQ(readable_array[i], readable_array.get(i));

    // Test the value object getter
    EXPECT_EQ(-10 * i, writable_array.as_value(i).asInt());

    // Test that the object getter is based on the same underlying value object
    EXPECT_EQ(&writable_array.as_value(i),
              &writable_array.as_object(i).Storage());
  }

  writable_array.set(kSize, 123);
  EXPECT_EQ(kSize + 1, readable_array.size());
  EXPECT_EQ(123, readable_array.get(kSize));
}

// Test string array specialization.
TEST_F(JsonCppAdapterFixture, TestStringArray) {
  const int kSize = 3;
  Json::Value storage;
  InitStringArray(&storage, kSize);
  JsonCppArray<string> writable_array(&storage);
  const JsonCppArray<string> readable_array(storage);
  JsonCppArray<string>::const_iterator it = readable_array.begin();
  JsonCppArray<string>::const_iterator it_end = readable_array.end();

  EXPECT_TRUE(it == it);
  EXPECT_FALSE(it != it);
  EXPECT_TRUE(it != it_end);
  EXPECT_FALSE(it == it_end);

  EXPECT_EQ(kSize, readable_array.size());
  for (int i = 0; i < kSize; ++i) {
    EXPECT_EQ(readable_array.get(i), readable_array[i]);
    EXPECT_EQ(readable_array.get(i), writable_array.get(i));
    EXPECT_EQ(readable_array[i], writable_array[i]);
    EXPECT_EQ(StrCat("Test ", i), readable_array.get(i));

    // Test that the object getter is based on the same underlying value object
    EXPECT_EQ(&writable_array.as_value(i),
              &writable_array.as_object(i).Storage());
    EXPECT_EQ(*it, readable_array[i]);
    EXPECT_FALSE(it == it_end);
    ++it;
  }
  EXPECT_TRUE(it == it_end);

  writable_array.set(kSize, "X");
  EXPECT_EQ(kSize + 1, readable_array.size());
  EXPECT_EQ("X", readable_array.get(kSize));
}

// Test object array specialization.
TEST_F(JsonCppAdapterFixture, TestObjectArray) {
  JsonCppCapsule<JsonCppArray<ExampleJsonObject> >
      writable_array;
  const JsonCppArray<ExampleJsonObject>
      readable_array(writable_array.Storage());

  JsonCppCapsule<ExampleJsonObject> example;
  example.set_number(0);
  writable_array.set(0, example);
  EXPECT_EQ(0, writable_array.get(0).number());
  example.set_number(1);

  writable_array.set(1, example);
  EXPECT_EQ(2, writable_array.size());
  EXPECT_EQ(1, writable_array.get(1).number());
  EXPECT_EQ(0, writable_array.get(0).number());

  EXPECT_EQ(2, readable_array.size());
  EXPECT_EQ(0, readable_array.get(0).number());
  EXPECT_EQ(1, readable_array.get(1).number());

  EXPECT_EQ(readable_array[0], readable_array.get(0));
  EXPECT_EQ(readable_array[0], writable_array[0]);

  ExampleJsonObject test_get(writable_array.get(1));
  EXPECT_DEATH(test_get.set_number(-1), "mutable");

  JsonCppArray<ExampleJsonObject>::const_iterator it = readable_array.begin();
  for (int i = 0; i < 2; ++i) {
    EXPECT_FALSE(it == readable_array.end());
    EXPECT_EQ(*it, readable_array[i]);
    ++it;
  }
  EXPECT_TRUE(it == readable_array.end());

  int next = 0;
  for (it = readable_array.begin();
       it != readable_array.end();
       ++it) {
    EXPECT_EQ(*it, readable_array[next]);
    ++next;
  }
  EXPECT_EQ(2, next);

  ExampleJsonObject test_mutable_get(writable_array.mutable_get(1));
  test_mutable_get.set_number(-1);
  EXPECT_EQ(-1, readable_array.get(1).number());
}

TEST_F(JsonCppAdapterFixture, TestExportPrimitiveArray) {
  const int kSize = 10;
  Json::Value storage;
  InitArray(&storage, kSize);
  const JsonCppArray<int> readable_array(storage);

  std::vector<int> data_vector;
  readable_array.Export(&data_vector);
  EXPECT_EQ(kSize, data_vector.size());

  // Only export second first half using the array interface.
  const int begin_segment = 2;
  const int end_segment = 7;  // inclusive
  const int segment_count = end_segment - begin_segment + 1;
  std::unique_ptr<int[]> data_array(new int[segment_count]);

  EXPECT_TRUE(
    readable_array.Export(begin_segment, segment_count, data_array.get()));
  for (int i = 0; i < kSize; ++i) {
    EXPECT_EQ(10 * i, data_vector[i]);
    if (i >= begin_segment && i <= end_segment) {
      EXPECT_EQ(data_vector[i], data_array[i - begin_segment]) << "i=" << i;
    }
  }

  // out of bounds
  EXPECT_FALSE(readable_array.Export(kSize - 1, 2, data_array.get()));
}

TEST_F(JsonCppAdapterFixture, TestImportArray) {
  const int kSize = 10;
  Json::Value storage;
  InitArray(&storage, kSize);
  const JsonCppArray<int> readable_array(storage);

  // Initialize the vectors we'll import from using Export.
  // We'll assume this works since it is tested elsewhere.
  std::vector<int> data_vector;
  readable_array.Export(&data_vector);
  EXPECT_EQ(kSize, data_vector.size());

  // Initialize the array we'll import from using Export.
  // We'll assume this works since it is tested elsewhere.
  std::unique_ptr<int[]> data_array(new int[kSize]);
  ASSERT_TRUE(readable_array.Export(0, kSize, data_array.get()));

  // Test the import interfaces, using == operator on underlying storage
  // to verify that we loaded the arrays properly.
  Json::Value writable_storage;
  JsonCppArray<int> writable_array(&writable_storage);
  writable_array.Import(data_array.get(), kSize);
  EXPECT_EQ(storage, writable_storage);

  writable_array.Import(data_vector);
  EXPECT_EQ(storage, writable_storage);
}

TEST_F(JsonCppAdapterFixture, TestExportStringArray) {
  const int kSize = 10;
  Json::Value storage;
  InitStringArray(&storage, kSize);
  JsonCppArray<string> writable_array(&storage);
  const JsonCppArray<string> readable_array(storage);

  std::vector<string> data_vector;
  readable_array.Export(&data_vector);
  EXPECT_EQ(kSize, data_vector.size());

  // Only export second first half using the array interface.
  const int begin_segment = 2;
  const int end_segment = 7;  // inclusive
  const int segment_count = end_segment - begin_segment + 1;
  std::unique_ptr<string[]> data_array(new string[segment_count]);

  EXPECT_TRUE(
    readable_array.Export(begin_segment, segment_count, data_array.get()));
  for (int i = 0; i < kSize; ++i) {
    EXPECT_EQ(StrCat("Test ", i), data_vector[i]);
    if (i >= begin_segment && i <= end_segment) {
      EXPECT_EQ(data_vector[i], data_array[i - begin_segment]) << "i=" << i;
    }
  }

  // out of bounds
  EXPECT_FALSE(readable_array.Export(kSize - 1, 2, data_array.get()));
}

TEST_F(JsonCppAdapterFixture, TestImportStringArray) {
  const int kSize = 10;
  Json::Value storage;
  InitStringArray(&storage, kSize);
  const JsonCppArray<string> readable_array(storage);

  std::vector<string> data_vector;
  readable_array.Export(&data_vector);
  EXPECT_EQ(kSize, data_vector.size());

  std::unique_ptr<string[]> data_array(new string[kSize]);
  ASSERT_TRUE(readable_array.Export(0, kSize, data_array.get()));

  Json::Value writable_storage;
  JsonCppArray<string> writable_array(&writable_storage);
  writable_array.Import(data_array.get(), kSize);
  EXPECT_EQ(storage, writable_storage);

  writable_array.Import(data_vector);
  EXPECT_EQ(storage, writable_storage);
}

}  // namespace googleapis

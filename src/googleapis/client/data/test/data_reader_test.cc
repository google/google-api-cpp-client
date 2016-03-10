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
#include <memory>
#include <string>
using std::string;

#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace googleapis {

using client::DataReader;
using client::NewUnmanagedInvalidDataReader;
using client::StatusInternalError;
using client::StatusOk;
using client::StatusUnknown;

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;

// Closures used for mocking read requests.
typedef Callback2<int64, char*> ReadCallback;

class MockDataReader : public DataReader {
 public:
  MockDataReader() : DataReader(NULL) {}
  explicit MockDataReader(Closure* deleter) : DataReader(deleter) {}

  virtual ~MockDataReader() {
  }

  MOCK_METHOD1(DoSetOffset, int64(int64 offset));
  MOCK_METHOD2(
      DoReadToBuffer,
      int64(int64 max_bytes, char* append_to));

  void poke_done(bool done)               { set_done(done); }
  void poke_status(util::Status status) { set_status(status); }
};

class DataReaderTestFixture : public testing::Test {
 public:
  // Helper method because string::append is ambiguous for NewPermanentCallback.
  void ReadToBufferHelper(
      const string& data, int64 max_bytes, char* storage) {
    memcpy(storage, data.c_str(), data.size());
  }
};


static void Raise(bool* called) { *called = true; }
TEST_F(DataReaderTestFixture, TestCallback) {
  bool called = false;
  {
    MockDataReader reader(NewCallback(&Raise, &called));
  }
  EXPECT_TRUE(called);
}

static void ReadNextCharToBuffer(
    const char** from, int64 count, char* buffer) {
  EXPECT_EQ(1, count);
  if (*from) {
    *buffer = **from;
    ++*from;  // consume char
  }
}

TEST_F(DataReaderTestFixture, TestAttributes) {
  MockDataReader reader;
  EXPECT_TRUE(reader.ok());
  EXPECT_TRUE(reader.status().ok());
  EXPECT_FALSE(reader.done());
  EXPECT_FALSE(reader.error());
  EXPECT_EQ(0, reader.offset());
  EXPECT_EQ(-1, reader.TotalLengthIfKnown());

  reader.poke_done(true);
  EXPECT_TRUE(reader.ok());
  EXPECT_TRUE(reader.status().ok());
  EXPECT_TRUE(reader.done());
  EXPECT_FALSE(reader.error());

  reader.poke_done(false);
  EXPECT_TRUE(reader.ok());
  EXPECT_TRUE(reader.status().ok());
  EXPECT_FALSE(reader.done());
  EXPECT_FALSE(reader.error());

  googleapis::util::Status status(StatusUnknown("Poked Error"));
  reader.poke_status(status);
  EXPECT_TRUE(reader.done());
  EXPECT_TRUE(reader.error());
  EXPECT_FALSE(reader.ok());
  EXPECT_EQ(status.ToString(), reader.status().ToString());

  reader.poke_status(StatusOk());
  EXPECT_TRUE(reader.done());
  EXPECT_FALSE(reader.error());
  EXPECT_TRUE(reader.ok());
  EXPECT_TRUE(reader.status().ok());
}

TEST_F(DataReaderTestFixture, SetOffset) {
  MockDataReader reader;
  EXPECT_CALL(reader, DoSetOffset(10)).WillOnce(Return(10));
  EXPECT_EQ(10, reader.SetOffset(10));
  EXPECT_EQ(10, reader.offset());

  EXPECT_CALL(reader, DoSetOffset(5)).WillOnce(Return(5));
  EXPECT_EQ(5, reader.SetOffset(5));
  EXPECT_EQ(5, reader.offset());

  EXPECT_CALL(reader, DoSetOffset(20)).WillOnce(Return(1));
  EXPECT_EQ(1, reader.SetOffset(20));
  EXPECT_EQ(1, reader.offset());
  EXPECT_TRUE(reader.ok());

  EXPECT_CALL(reader, DoSetOffset(10)).WillOnce(Return(-1));
  EXPECT_EQ(-1, reader.SetOffset(10));
  EXPECT_EQ(-1, reader.offset());
  EXPECT_FALSE(reader.ok());
}

TEST_F(DataReaderTestFixture, ReadEmptyToString) {
  MockDataReader reader;
  const string kPrefix = " ";  // test we preserve this
  string s = kPrefix;
  const int64 kInternalBufferSize = 1 << 13;  // Defined in data_reader.cc

  std::unique_ptr<Closure> poke_done(
      NewPermanentCallback(&reader, &MockDataReader::poke_done, true));
  EXPECT_CALL(reader, DoReadToBuffer(kInternalBufferSize, _))
      .WillOnce(
          DoAll(
              InvokeWithoutArgs(poke_done.get(), &Closure::Run),
              Return(0)));
  EXPECT_EQ(0, reader.ReadToString(kint64max, &s));
  EXPECT_EQ(0, reader.offset());
  EXPECT_TRUE(reader.ok());
  EXPECT_TRUE(reader.status().ok());
  EXPECT_FALSE(reader.error());
  EXPECT_TRUE(reader.done());
  EXPECT_EQ(kPrefix, s);
}

TEST_F(DataReaderTestFixture, ReadToBufer) {
  MockDataReader reader;
  const string kExpect = "Hello, World!\n";

  std::unique_ptr<Closure> poke_done(
      NewPermanentCallback(&reader, &MockDataReader::poke_done, true));
  std::unique_ptr<ReadCallback> read_helper(
      NewPermanentCallback(this, &DataReaderTestFixture::ReadToBufferHelper,
                           kExpect));
  EXPECT_CALL(reader, DoReadToBuffer(100, _))
      .WillOnce(
          DoAll(
              Invoke(read_helper.get(), &ReadCallback::Run),
              InvokeWithoutArgs(poke_done.get(), &Closure::Run),
              Return(static_cast<int64>(kExpect.size()))));
  char buffer[100];
  EXPECT_EQ(kExpect.size(), reader.ReadToBuffer(sizeof(buffer), buffer));
  EXPECT_EQ(kExpect.size(), reader.offset());
  EXPECT_FALSE(reader.error());
  EXPECT_TRUE(reader.ok());
  EXPECT_TRUE(reader.done());
  EXPECT_EQ(kExpect, StringPiece(buffer, kExpect.size()));
}

TEST_F(DataReaderTestFixture, ReadToStringFragmented) {
  MockDataReader reader;
  const string kPrefix = " ";  // test we preserve this
  string s = kPrefix;

  const string kHello = "Hello, ";
  const string kWorld = "World!";

  // Return buffer in two parts
  std::unique_ptr<Closure> poke_done(
      NewPermanentCallback(&reader, &MockDataReader::poke_done, true));
  std::unique_ptr<ReadCallback> hello_helper(
      NewPermanentCallback(this, &DataReaderTestFixture::ReadToBufferHelper,
                           kHello));
  std::unique_ptr<ReadCallback> world_helper(
      NewPermanentCallback(this, &DataReaderTestFixture::ReadToBufferHelper,
                           kWorld));
  EXPECT_CALL(reader, DoReadToBuffer(20, _))
      .WillOnce(
          DoAll(Invoke(hello_helper.get(), &ReadCallback::Run),
                Return(static_cast<int64>(kHello.size()))));

  EXPECT_CALL(reader, DoReadToBuffer(20 - kHello.size(), _))
      .WillOnce(
          DoAll(
              Invoke(world_helper.get(), &ReadCallback::Run),
              InvokeWithoutArgs(poke_done.get(), &Closure::Run),
              Return(static_cast<int64>(kWorld.size()))));

  EXPECT_EQ(kHello.size() + kWorld.size(), reader.ReadToString(20, &s));
  EXPECT_EQ(StrCat(kPrefix, kHello, kWorld), s);
  EXPECT_EQ(kHello.size() + kWorld.size(), reader.offset());
  EXPECT_TRUE(reader.ok());
  EXPECT_TRUE(reader.done());
  EXPECT_FALSE(reader.error());
}


TEST_F(DataReaderTestFixture, ReadToBufferFragmented) {
  MockDataReader reader;
  char buffer[100];

  const string kHello = "Hello, ";
  const string kWorld = "World!";

  std::unique_ptr<Closure> poke_done(
      NewPermanentCallback(&reader, &MockDataReader::poke_done, true));
  std::unique_ptr<ReadCallback> hello_helper(
      NewPermanentCallback(this, &DataReaderTestFixture::ReadToBufferHelper,
                           kHello));
  std::unique_ptr<ReadCallback> world_helper(
      NewPermanentCallback(this, &DataReaderTestFixture::ReadToBufferHelper,
                           kWorld));
  EXPECT_CALL(reader, DoReadToBuffer(sizeof(buffer), _))
      .WillOnce(
          DoAll(
              Invoke(hello_helper.get(), &ReadCallback::Run),
              Return(static_cast<int64>(kHello.size()))));

  EXPECT_CALL(reader, DoReadToBuffer(sizeof(buffer) - kHello.size(), _))
      .WillOnce(
          DoAll(
              Invoke(world_helper.get(), &ReadCallback::Run),
              InvokeWithoutArgs(poke_done.get(), &Closure::Run),
              Return(static_cast<int64>(kWorld.size()))));

  EXPECT_EQ(kHello.size() + kWorld.size(),
            reader.ReadToBuffer(sizeof(buffer), buffer));
  EXPECT_EQ(StrCat(kHello, kWorld),
            StringPiece(buffer, kHello.size() + kWorld.size()));
  EXPECT_EQ(kHello.size() + kWorld.size(), reader.offset());
  EXPECT_TRUE(reader.done());
  EXPECT_TRUE(reader.ok());
  EXPECT_FALSE(reader.error());
}

TEST_F(DataReaderTestFixture, TestInvalidReader) {
  googleapis::util::Status status = StatusInternalError("test");
  std::unique_ptr<DataReader> reader(NewUnmanagedInvalidDataReader(status));
  EXPECT_FALSE(reader->ok());
  EXPECT_TRUE(reader->done());
  EXPECT_GT(0, reader->TotalLengthIfKnown());
  EXPECT_EQ("", reader->RemainderToString());
  EXPECT_EQ(status, reader->status());
  EXPECT_FALSE(reader->Reset());
  EXPECT_EQ(-1, reader->SetOffset(0));
  EXPECT_EQ(status, reader->status());
}

TEST_F(DataReaderTestFixture, TestReadUntilPattern) {
  const string input = "ababacXabac";
  std::pair<const string, const string> tests[] = {
      std::make_pair("aba", "aba"), std::make_pair("abac", "ababac"),
      std::make_pair("cXa", "ababacXa"), std::make_pair("Z", input),
      std::make_pair("", "")};

  for (int i = 0; i < ARRAYSIZE(tests); ++i) {
    const string& pattern = tests[i].first;
    const string& expect = tests[i].second;
    const char* remaining = input.c_str();
    MockDataReader reader;
    string found = "ERASE ME";

    int64 expect_start_offset = pattern.empty() ? 0 : input.find(pattern);
    int64 expect_end_offset = (expect_start_offset == string::npos)
        ? input.size()
        : (expect_start_offset + pattern.size());

    if (expect_start_offset == string::npos) {
      // after returning all the chars, return 0 to indicate EOF.
      Closure* poke_done(
          NewCallback(&reader, &MockDataReader::poke_done, true));
      // NOTE(user): 20130813
      // This seems to cause the later EXPECT_CALL to complain in gmock
      // about it never getting called, but it does. Once the second call
      // retires then it will call this once, which will poke_done to
      // terminate the internal loop.
      EXPECT_CALL(reader, DoReadToBuffer(1, _))
          .WillOnce(
              DoAll(
                  InvokeWithoutArgs(poke_done, &Closure::Run),
                  Return(0)));
    }
    std::unique_ptr<ReadCallback> read_callback(
        NewPermanentCallback(&ReadNextCharToBuffer, &remaining));
    EXPECT_CALL(reader, DoReadToBuffer(1, _))
        .Times(expect_end_offset)
        .WillRepeatedly(
            DoAll(Invoke(read_callback.get(), &ReadCallback::Run),
                  Return(1)))
        .RetiresOnSaturation();  // So the Return(0) variant can kick in

    string got;
    EXPECT_EQ(expect_start_offset != string::npos,
              reader.ReadUntilPatternInclusive(pattern, &got))
        << " pattern=" << pattern;
    EXPECT_EQ(expect_end_offset, reader.offset())
        << " pattern=" << pattern;
    EXPECT_EQ(input.substr(0, expect_end_offset), got)
        << " pattern=" << pattern;
    EXPECT_EQ(expect, got);
  }
}

}  // namespace googleapis

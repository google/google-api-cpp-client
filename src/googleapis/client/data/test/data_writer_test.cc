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


#include "googleapis/client/data/data_writer.h"
#include <memory>
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/file_data_writer.h"
#include "googleapis/client/util/test/googleapis_gtest.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/util/file.h"
#include "googleapis/strings/stringpiece.h"
#include <gmock/gmock.h>
#include "googleapis/util/canonical_errors.h"
#include "googleapis/util/status_test_util.h"

namespace googleapis {

using client::DataReader;
using client::DataWriter;
using client::JoinPath;
using client::NewFileDataWriter;
using client::NewStringDataWriter;
using client::NewUnmanagedInMemoryDataReader;
using client::NewUnmanagedFileDataReader;
using client::StatusOk;
using client::StatusUnknown;

using testing::_;
using testing::Return;
using client::GetTestingTempDir;

class MockDataWriter : public DataWriter {
 public:
  MockDataWriter() {}
  ~MockDataWriter() override {}

  MOCK_METHOD0(DoClear, googleapis::util::Status());
  MOCK_METHOD0(DoBegin, googleapis::util::Status());
  MOCK_METHOD0(DoEnd, googleapis::util::Status());
  MOCK_METHOD2(DoWrite, googleapis::util::Status(int64 bytes, const char* buffer));
  MOCK_METHOD1(DoNewDataReader, DataReader*(Closure* deleter));
};

class DataWriterTestFixture : public testing::Test {
 public:
};

TEST_F(DataWriterTestFixture, TestMethods) {
  MockDataWriter writer;

  EXPECT_EQ(0, writer.size());
  EXPECT_CALL(writer, DoBegin())
      .WillOnce(Return(StatusOk()));
  writer.Begin();

  const char  kWriteData[] = "TestWrite";
  const int64 kWriteLen = 4;

  EXPECT_CALL(writer, DoWrite(kWriteLen, kWriteData))
      .WillOnce(Return(StatusOk()));
  EXPECT_TRUE(writer.Write(kWriteLen, kWriteData).ok());
  EXPECT_EQ(kWriteLen, writer.size());

  EXPECT_CALL(writer, DoWrite(kWriteLen, kWriteData))
      .WillOnce(Return(StatusOk()));
  EXPECT_TRUE(writer.Write(kWriteLen, kWriteData).ok());
  EXPECT_EQ(2* kWriteLen, writer.size());

  EXPECT_CALL(writer, DoEnd())
      .WillOnce(Return(StatusOk()));
  writer.End();

  EXPECT_EQ(2* kWriteLen, writer.size());
  EXPECT_CALL(writer, DoClear())
      .WillOnce(Return(StatusOk()));
  writer.Clear();
  EXPECT_EQ(0, writer.size());
}

TEST_F(DataWriterTestFixture, TestAutoBegin) {
  MockDataWriter writer;
  const char  kWriteData[] = "TestWrite";
  const int64 kWriteLen = 4;

  EXPECT_CALL(writer, DoBegin())
      .WillOnce(Return(StatusOk()));
  EXPECT_CALL(writer, DoWrite(kWriteLen, kWriteData))
      .WillOnce(Return(StatusOk()));

  EXPECT_TRUE(writer.Write(kWriteLen, kWriteData).ok());
  EXPECT_EQ(kWriteLen, writer.size());

  EXPECT_CALL(writer, DoWrite(kWriteLen, kWriteData))
      .WillOnce(Return(StatusOk()));
  EXPECT_TRUE(writer.Write(kWriteLen, kWriteData).ok());
  EXPECT_EQ(2 * kWriteLen, writer.size());
}

TEST_F(DataWriterTestFixture, TestStringDataWriter) {
  const StringPiece kHelloWorld = "Hello, World!";
  string s;
  std::unique_ptr<DataWriter> writer(NewStringDataWriter(&s));
  writer->Begin();
  EXPECT_TRUE(writer->Write(3, kHelloWorld.data()).ok());
  EXPECT_EQ(s, kHelloWorld.substr(0, 3));
  EXPECT_TRUE(writer->Write(kHelloWorld.size() - 3,
                            kHelloWorld.data() + 3).ok());
  EXPECT_EQ(s, kHelloWorld);
  writer->End();

  std::unique_ptr<DataReader> reader1(writer->NewUnmanagedDataReader());
  std::unique_ptr<DataReader> reader2(writer->NewUnmanagedDataReader());
  EXPECT_EQ(kHelloWorld, reader1->RemainderToString());
  EXPECT_EQ(kHelloWorld, reader2->RemainderToString());

  writer->Clear();
  EXPECT_EQ(0, s.size());
  writer->Begin();
  EXPECT_TRUE(writer->Write(kHelloWorld.size(), kHelloWorld.data()).ok());
  writer->End();
  EXPECT_EQ(kHelloWorld.size(), s.size());
}

TEST_F(DataWriterTestFixture, TestFileDataWriter) {
  const StringPiece kHelloWorld = "Hello, World!";
  const string path = JoinPath(GetTestingTempDir(), "StringDataWriter.test");

  File::Delete(path);
  ASSERT_TRUE(util::IsNotFound(file::Exists(path, file::Defaults())));

  std::unique_ptr<DataWriter> writer(NewFileDataWriter(path));
  EXPECT_TRUE(util::IsNotFound(file::Exists(path, file::Defaults())));

  writer->Begin();
  EXPECT_OK(file::Exists(path, file::Defaults()));

  EXPECT_TRUE(writer->Write(3, kHelloWorld.data()).ok());
  EXPECT_TRUE(writer->Write(kHelloWorld.size() - 3,
                            kHelloWorld.data() + 3).ok());
  writer->End();

  string content;
  EXPECT_OK(file::GetContents(path, &content, file::Defaults()));
  EXPECT_EQ(kHelloWorld, content);

  std::unique_ptr<DataReader> reader1(writer->NewUnmanagedDataReader());
  std::unique_ptr<DataReader> reader2(writer->NewUnmanagedDataReader());
  EXPECT_EQ(kHelloWorld, reader1->RemainderToString());
  EXPECT_EQ(kHelloWorld, reader2->RemainderToString());

  writer->Clear();
  EXPECT_TRUE(util::IsNotFound(file::Exists(path, file::Defaults())))
      << "Clear did not erase the file";

  writer->Begin();
  EXPECT_TRUE(writer->Write(kHelloWorld).ok());
  writer->End();
  ASSERT_OK(file::Exists(path, file::Defaults()));
  writer.reset(NewFileDataWriter(path));

  writer->Begin();
  EXPECT_OK(file::GetContents(path, &content, file::Defaults()));
  EXPECT_EQ("", content) << "Expected Begin() to erase the old file";

  EXPECT_TRUE(writer->Write(kHelloWorld.size(), kHelloWorld.data()).ok());
  writer->End();

  EXPECT_OK(file::GetContents(path, &content, file::Defaults()));
  EXPECT_EQ(kHelloWorld, content);
}

TEST_F(DataWriterTestFixture, TestWriteReader) {
  string source;
  for (int i = 0; i < 1 << 13; ++i) {
    // Add 8 bit values that change but are not strictly
    // repeating at the chunk seam.
    source.push_back((i & 0x7f) + ((i >> 8) & 0x7f));
  }

  string target;
  std::unique_ptr<DataReader> reader(NewUnmanagedInMemoryDataReader(source));
  std::unique_ptr<DataWriter> writer(NewStringDataWriter(&target));
  EXPECT_TRUE(writer->Write(reader.get(), 90).ok());
  EXPECT_EQ(90, writer->size());
  EXPECT_EQ(90, target.size());
  EXPECT_EQ(StringPiece(source).substr(0, 90), target);

  EXPECT_TRUE(writer->Write(reader.get(), 10).ok());
  EXPECT_EQ(100, writer->size());
  EXPECT_EQ(100, target.size());
  EXPECT_EQ(StringPiece(source).substr(0, 100), target);

  EXPECT_TRUE(writer->Write(reader.get()).ok());
  EXPECT_EQ(source.size(), writer->size());
  EXPECT_EQ(source.size(), target.size());
  EXPECT_EQ(source, target);
}

TEST_F(DataWriterTestFixture, TestWriteReaderMemory) {
  // Test memory management
  const char* kExpect = "Hello, World";
  string* str = new string(kExpect);
  Closure* closure = DeletePointerClosure(str);
  std::unique_ptr<DataWriter> writer(NewStringDataWriter(str));
  std::unique_ptr<DataReader> reader(writer->NewManagedDataReader(closure));
  delete writer.release();
  EXPECT_EQ(StringPiece(kExpect), reader->RemainderToString());
}

// Use a delegate reader so we can test not knowing the reader size.
class TestingDataReader : public DataReader {
 public:
  explicit TestingDataReader(const StringPiece& data)
      : DataReader(NULL),
        reader_(
            client::NewManagedInMemoryDataReader(
                data.as_string())) {
  }

  int64 DoReadToBuffer(int64 max_bytes, char* storage) override {
    int64 result = reader_->ReadToBuffer(max_bytes, storage);
    if (reader_->done()) {
      set_done(true);
    }
    return result;
  }

 private:
  std::unique_ptr<DataReader> reader_;
};

TEST_F(DataWriterTestFixture, TestWriteReaderWithUnknownLength) {
  const string expect = "Hello, World!";
  std::unique_ptr<DataReader> reader(new TestingDataReader(expect));
  EXPECT_GT(0, reader->TotalLengthIfKnown());

  string got;
  std::unique_ptr<DataWriter> writer(NewStringDataWriter(&got));
  EXPECT_TRUE(writer->Write(reader.get()).ok());
  EXPECT_EQ(expect, got);
}

}  // namespace googleapis

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
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/test/googleapis_gtest.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/base/scoped_ptr.h"
#include "googleapis/util/file.h"
#include "googleapis/strings/stringpiece.h"
#include <gmock/gmock.h>

namespace googleapis {

using client::DataReader;
using client::DataWriter;
using client::JoinPath;
using client::NewFileDataWriter;
using client::NewStringDataWriter;
using client::StatusOk;
using client::StatusUnknown;

using testing::_;
using testing::Return;
using client::GetTestingTempDir;

class MockDataWriter : public DataWriter {
 public:
  MockDataWriter() {}
  virtual ~MockDataWriter() {}

  MOCK_METHOD0(DoClear, util::Status());
  MOCK_METHOD0(DoBegin, util::Status());
  MOCK_METHOD0(DoEnd, util::Status());
  MOCK_METHOD2(DoWrite, util::Status(int64 bytes, const char* buffer));
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
  scoped_ptr<DataWriter> writer(NewStringDataWriter(&s));
  writer->Begin();
  EXPECT_TRUE(writer->Write(3, kHelloWorld.data()).ok());
  EXPECT_EQ(s, StringPiece(kHelloWorld, 0, 3));
  EXPECT_TRUE(writer->Write(kHelloWorld.size() - 3,
                            kHelloWorld.data() + 3).ok());
  EXPECT_EQ(s, kHelloWorld);
  writer->End();

  scoped_ptr<DataReader> reader1(writer->NewUnmanagedDataReader());
  scoped_ptr<DataReader> reader2(writer->NewUnmanagedDataReader());
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
  ASSERT_TRUE(!File::Exists(path));

  scoped_ptr<DataWriter> writer(NewFileDataWriter(path));
  EXPECT_FALSE(File::Exists(path));

  writer->Begin();
  EXPECT_TRUE(File::Exists(path));

  EXPECT_TRUE(writer->Write(3, kHelloWorld.data()).ok());
  EXPECT_TRUE(writer->Write(kHelloWorld.size() - 3,
                            kHelloWorld.data() + 3).ok());
  writer->End();

  string content;
  EXPECT_TRUE(File::ReadFileToString(path, &content));
  EXPECT_EQ(kHelloWorld, content);

  scoped_ptr<DataReader> reader1(writer->NewUnmanagedDataReader());
  scoped_ptr<DataReader> reader2(writer->NewUnmanagedDataReader());
  EXPECT_EQ(kHelloWorld, reader1->RemainderToString());
  EXPECT_EQ(kHelloWorld, reader2->RemainderToString());

  writer->Clear();
  EXPECT_TRUE(File::Exists(path)) << "Did not expect clear to change the file";
  EXPECT_TRUE(File::ReadFileToString(path, &content));
  EXPECT_EQ(kHelloWorld, content);

  writer->Begin();
  EXPECT_TRUE(File::ReadFileToString(path, &content));
  EXPECT_EQ("", content) << "Expected Begin() to erase the old file";

  EXPECT_TRUE(writer->Write(kHelloWorld.size(), kHelloWorld.data()).ok());
  writer->End();

  EXPECT_TRUE(File::ReadFileToString(path, &content));
  EXPECT_EQ(kHelloWorld, content);
}

} // namespace googleapis
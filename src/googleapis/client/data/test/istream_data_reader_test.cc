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


#include <fstream>
#include <memory>
#include <string>
using std::string;
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/test/googleapis_gtest.h"
#include "googleapis/client/util/uri_utils.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

using client::DataReader;
using client::GetTestingTempDir;
using client::JoinPath;
using client::NewManagedIstreamDataReader;

static char kExpectedFileContents[2048];

class IstreamReaderTestFixture : public testing::Test {
 public:
  static void SetUpTestCase() {
    for (int i = 0; i < ARRAYSIZE(kExpectedFileContents); ++i) {
      kExpectedFileContents[i] = i & 0xff;
    }
    string path = JoinPath(GetTestingTempDir(), "data");
ASSERT_TRUE(
      File::WritePath(
         path,
         string(kExpectedFileContents, sizeof(kExpectedFileContents))).ok());
  }

  std::istream* NewStream(const string& file) {
    string path = JoinPath(GetTestingTempDir(), file);
    return new std::ifstream(
        path.c_str(), std::ifstream::in | std::ifstream::binary);
  }
};

TEST_F(IstreamReaderTestFixture, InvalidFile) {
  std::istream* stream = NewStream("invalid");
  std::unique_ptr<DataReader> reader(
      NewManagedIstreamDataReader(stream, DeletePointerClosure(stream)));
  char buffer[100];
  EXPECT_EQ(0, reader->ReadToBuffer(1, buffer));
  EXPECT_TRUE(reader->done());
  EXPECT_TRUE(reader->error());
  EXPECT_FALSE(reader->ok());
}

TEST_F(IstreamReaderTestFixture, ReadInOneBlock) {
  std::istream* stream = NewStream("data");
  std::unique_ptr<DataReader> reader(
      NewManagedIstreamDataReader(stream, DeletePointerClosure(stream)));
  EXPECT_FALSE(reader->done());
  EXPECT_FALSE(reader->error());
  EXPECT_TRUE(reader->ok());

  const StringPiece kExpect(
      kExpectedFileContents, sizeof(kExpectedFileContents));

  char buffer[sizeof(kExpectedFileContents)];
  int64 read = reader->ReadToBuffer(sizeof(kExpectedFileContents), buffer);
  EXPECT_EQ(read, kExpect.size());
  EXPECT_EQ(kExpect, StringPiece(buffer, read));
  // Reader may or may not know it is done so dont test it.
  EXPECT_TRUE(reader->ok());
  EXPECT_FALSE(reader->error());
  EXPECT_EQ(read, reader->offset());

  EXPECT_EQ(0, reader->ReadToBuffer(1, buffer));
  // By now we should know we are done since we couldnt satisfy the read.
  EXPECT_TRUE(reader->done());
  EXPECT_TRUE(reader->ok());
  EXPECT_FALSE(reader->error());
  EXPECT_EQ(read, reader->offset());

  // Verify we can reset the file and read again.
  EXPECT_TRUE(reader->Reset());
  EXPECT_EQ(0, reader->offset());
  read = reader->ReadToBuffer(sizeof(kExpectedFileContents), buffer);
  EXPECT_EQ(read, kExpect.size());
  EXPECT_EQ(kExpect, StringPiece(buffer, read));
}

TEST_F(IstreamReaderTestFixture, ReadInMultipleBlocks) {
  std::istream* stream = NewStream("data");
  std::unique_ptr<DataReader> reader(
      NewManagedIstreamDataReader(stream, DeletePointerClosure(stream)));
  EXPECT_FALSE(reader->done());
  EXPECT_TRUE(reader->ok());
  EXPECT_FALSE(reader->error());

  const StringPiece kExpect(
      kExpectedFileContents, sizeof(kExpectedFileContents));

  const int64 kMaxLen = sizeof(kExpectedFileContents) / 3 + 1;
  char buffer[sizeof(kExpectedFileContents)];
  char* storage = buffer;
  for (int i = 0; i < 3; ++i) {
    int64 starting_offset = reader->offset();
    int64 read = reader->ReadToBuffer(kMaxLen, storage);
    EXPECT_LT(0, read);
    EXPECT_GE(kMaxLen, read);
    EXPECT_EQ(starting_offset + read, reader->offset());
    EXPECT_EQ(kExpect.substr(starting_offset, read),
              StringPiece(storage, read));
    storage += read;
    if (i < 2) {
      // When i==2 there is no more data, but reader doesnt necessarily know
      // that.
      EXPECT_FALSE(reader->done());
    }
    EXPECT_TRUE(reader->ok());
    EXPECT_FALSE(reader->error());
  }

  if (!reader->done()) {
    EXPECT_EQ(0, reader->ReadToBuffer(1, storage));
    EXPECT_TRUE(reader->done());
  }
  EXPECT_EQ(kExpect, StringPiece(buffer, kExpect.size()));
}

}  // namespace googleapis

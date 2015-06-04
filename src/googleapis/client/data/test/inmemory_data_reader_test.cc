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


#include <memory>
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/strings/stringpiece.h"
#include <gtest/gtest.h>

namespace googleapis {

using client::DataReader;
using client::NewManagedInMemoryDataReader;
using client::NewUnmanagedInMemoryDataReader;

enum ExpectedStorage {
  TEST_IN_MEMORY,     // test pointers are references to original memory
  TEST_COPIED_MEMORY  // test pointers are not references to original memory
};

enum ExpectedFragmentation {
  TEST_UNFRAGMENTED,  // test data is in continguous memory
  TEST_FRAGMENTED     // no assumptions about memory being contiguous.
};

class InMemoryDataReaderTestFixture : public testing::Test {
 public:
  void TestDataReaderHelper(const StringPiece kExpect, DataReader* reader) {
    EXPECT_FALSE(reader->done());
    EXPECT_EQ(kExpect.size(), reader->TotalLengthIfKnown());

    char buffer[1 << 10];
    EXPECT_EQ(3, reader->ReadToBuffer(3, &buffer[0]));
    EXPECT_EQ(kExpect.substr(0, 3), StringPiece(buffer, 3));
    EXPECT_EQ(3, reader->offset());
    EXPECT_FALSE(reader->done());
    EXPECT_FALSE(reader->error());

    EXPECT_EQ(kExpect.size() - 3,
              reader->ReadToBuffer(sizeof(buffer) - 3, &buffer[3]));
    EXPECT_EQ(kExpect, StringPiece(buffer, kExpect.size()));

    EXPECT_EQ(kExpect.size(), reader->offset());
    EXPECT_TRUE(reader->done());
    EXPECT_FALSE(reader->error());

    // Check additional reads have no effect.
    EXPECT_EQ(0, reader->ReadToBuffer(100, buffer));
    EXPECT_EQ(kExpect.size(), reader->offset());
    EXPECT_TRUE(reader->done());
    EXPECT_FALSE(reader->error());

    // Test resetting after read.
    EXPECT_TRUE(reader->Reset());
    EXPECT_FALSE(reader->done());
    EXPECT_FALSE(reader->error());
    EXPECT_EQ(0, reader->offset());

    int64 read = reader->ReadToBuffer(kExpect.size(), buffer);
    EXPECT_GE(kExpect.size(), read);
    EXPECT_LE(0, read);
    EXPECT_EQ(kExpect.substr(0, read), StringPiece(buffer, read));
    EXPECT_EQ(read, reader->offset());

    // This doesn't need to be the case since we did not attempt to read past,
    // but our implementation has chosen to have this aggressive done property.
    EXPECT_TRUE(reader->done());
  }
};

TEST_F(InMemoryDataReaderTestFixture, InMemoryStringPiece) {
  StringPiece  kExpect("Hello World!");
  std::unique_ptr<DataReader> reader(NewUnmanagedInMemoryDataReader(kExpect));
  TestDataReaderHelper(kExpect, reader.get());
}

TEST_F(InMemoryDataReaderTestFixture, InMemoryCopiedString) {
  StringPiece  kExpect("Hello World!");
  string str(kExpect.data());
  std::unique_ptr<DataReader> reader(NewManagedInMemoryDataReader(str));
  str.clear();
  TestDataReaderHelper(kExpect, reader.get());
}

TEST_F(InMemoryDataReaderTestFixture, InMemoryTransferedString) {
  StringPiece kExpect("Hello World!");
  string* storage = new string(kExpect.data());
  std::unique_ptr<DataReader> reader(
      NewManagedInMemoryDataReader(
          StringPiece(*storage), DeletePointerClosure(storage)));
  TestDataReaderHelper(kExpect, reader.get());
}

}  // namespace googleapis

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
#include <vector>
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/strings/stringpiece.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace googleapis {

using client::DataReader;
using client::NewManagedCompositeDataReader;
using client::NewUnmanagedCompositeDataReader;
using client::NewUnmanagedInMemoryDataReader;
using client::StatusUnknown;

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;

static StringPiece kExpect("Hello, World!");

class MockDataReader : public DataReader {
 public:
  MockDataReader() : DataReader(NULL) {}
  explicit MockDataReader(Closure* deleter) : DataReader(deleter) {}

  virtual ~MockDataReader() {
  }

  void ReadToBufferHelper(const string& data, int64 len, char* storage) {
    CHECK_LE(data.size(), len);
    memcpy(storage, data.data(), data.size());
  }

  MOCK_METHOD1(DoSetOffset, int64(int64 offset));
  MOCK_METHOD2(
      DoReadToBuffer, int64(int64 max_bytes, char* storage));

  void poke_status(util::Status status) { set_status(status); }
  void poke_done(bool done) { set_done(done); }
};

class CompositeDataReaderTestFixture : public testing::Test {
 protected:
  std::vector<DataReader*>* MakeReaderList() {
    // Segment what we expected into 3 parts
    std::vector<DataReader*>* list = new std::vector<DataReader*>;

    list->push_back(
        NewUnmanagedInMemoryDataReader(kExpect.substr(0, 5)));
    list->push_back(
        NewUnmanagedInMemoryDataReader(kExpect.substr(5, 2)));
    list->push_back(
        NewUnmanagedInMemoryDataReader(kExpect.substr(7)));

    return list;
  }

  DataReader* MakeManagedTestReader() {
    std::vector<DataReader*>* list = MakeReaderList();
    return NewManagedCompositeDataReader(
        *list, NewCompositeReaderListAndContainerDeleter(list));
  }
};

TEST_F(CompositeDataReaderTestFixture, Unmanaged) {
  std::vector<DataReader*>* list(MakeReaderList());
  {
    std::unique_ptr<DataReader> reader(NewUnmanagedCompositeDataReader(*list));

    EXPECT_EQ(kExpect.size(), reader->TotalLengthIfKnown());
    EXPECT_FALSE(reader->done());
    EXPECT_FALSE(reader->error());
    EXPECT_TRUE(reader->ok());
  }

  for (vector<DataReader*>::iterator it = list->begin();
       it != list->end();
       ++it) {
    delete *it;
  }
  delete list;
}

TEST_F(CompositeDataReaderTestFixture, CompositeStringAttributes) {
  std::unique_ptr<DataReader> reader(MakeManagedTestReader());

  EXPECT_EQ(kExpect.size(), reader->TotalLengthIfKnown());
  EXPECT_FALSE(reader->done());
  EXPECT_FALSE(reader->error());
  EXPECT_TRUE(reader->ok());
}

TEST_F(CompositeDataReaderTestFixture, CompositeStringToBuffer) {
  std::unique_ptr<DataReader> reader(MakeManagedTestReader());

  char got[100];
  EXPECT_EQ(kExpect.size(),
            reader->ReadToBuffer(sizeof(got), got));
  EXPECT_EQ(kExpect, StringPiece(got, kExpect.size()));
  EXPECT_EQ(reader->offset(), kExpect.size());
  EXPECT_TRUE(reader->done());
  EXPECT_FALSE(reader->error());
  EXPECT_TRUE(reader->ok());
}

TEST_F(CompositeDataReaderTestFixture, CompositeStringReset) {
  std::unique_ptr<DataReader> reader(MakeManagedTestReader());

  string s;
  EXPECT_EQ(kExpect.size(), reader->ReadToString(kExpect.size(), &s));
  EXPECT_EQ(kExpect, s);

  // Test Reset
  EXPECT_TRUE(reader->Reset());
  EXPECT_EQ(0, reader->offset());
  EXPECT_FALSE(reader->done());
}

TEST_F(CompositeDataReaderTestFixture, CompositeResetFailure) {
  std::vector<DataReader*>* list = MakeReaderList();

  // We're going to mock out the middle element keeping the same
  // data as normal, but it will fail to reset instead.
  typedef Callback2<int64, char*> ReadToBufferClosure;
  string str;
  list->at(1)->ReadToString(kint64max, &str);

  MockDataReader* mock_reader(new MockDataReader());
  std::unique_ptr<ReadToBufferClosure> read_closure(
      NewPermanentCallback(mock_reader, &MockDataReader::ReadToBufferHelper,
                           str));
  std::unique_ptr<Closure> poke_done(
      NewPermanentCallback(mock_reader, &MockDataReader::poke_done, true));

  EXPECT_CALL(*mock_reader, DoReadToBuffer(_, _))
      .WillOnce(
          DoAll(
              Invoke(read_closure.get(), &ReadToBufferClosure::Run),
              InvokeWithoutArgs(poke_done.get(), &Closure::Run),
              Return(str.size())));

  delete list->at(1);
  list->at(1) = mock_reader;

  std::unique_ptr<DataReader> reader(NewManagedCompositeDataReader(
      *list, NewCompositeReaderListAndContainerDeleter(list)));
  string s;
  EXPECT_EQ(kExpect.size(), reader->ReadToString(kExpect.size(), &s));
  EXPECT_EQ(kExpect, s);

  // Test Reset
  googleapis::util::Status failure_status = StatusUnknown("Test Reset Failure");
  std::unique_ptr<Closure> poke_status(
      NewPermanentCallback(mock_reader,
                           &MockDataReader::poke_status, failure_status));
  EXPECT_CALL(*mock_reader, DoSetOffset(0)).WillOnce(
      DoAll(InvokeWithoutArgs(poke_status.get(), &Closure::Run),
            Return(-1)));

  EXPECT_EQ(kExpect.size(), reader->offset());
  EXPECT_FALSE(reader->Reset());
  EXPECT_EQ(-1, reader->offset());
  EXPECT_TRUE(reader->error());
  EXPECT_TRUE(reader->done());

  EXPECT_EQ(0, reader->ReadToString(kExpect.size(), &s));
  EXPECT_TRUE(reader->error());
  EXPECT_TRUE(reader->done());

  // Verify that we can recover from the error after a future seek.
  EXPECT_CALL(*mock_reader, DoSetOffset(0)).WillOnce(Return(0));
  EXPECT_EQ(0, reader->SetOffset(0));
  EXPECT_FALSE(reader->error());
  EXPECT_FALSE(reader->done());

  EXPECT_CALL(*mock_reader, DoReadToBuffer(_, _))
      .WillOnce(
          DoAll(
              Invoke(read_closure.get(), &ReadToBufferClosure::Run),
              InvokeWithoutArgs(poke_done.get(), &Closure::Run),
              Return(str.size())));
  s.clear();
  EXPECT_EQ(kExpect.size(), reader->ReadToString(kExpect.size(), &s));
  EXPECT_EQ(kExpect, s);
}

TEST_F(CompositeDataReaderTestFixture, CompositeFragmentedString) {
  std::unique_ptr<DataReader> reader(MakeManagedTestReader());

  // Test ReadToBuffer across fragments
  char buffer[100];
  char* storage = buffer;
  EXPECT_EQ(5, reader->ReadToBuffer(5, storage));
  EXPECT_EQ(kExpect.substr(0, 5), StringPiece(storage, 5));
  storage += 5;
  EXPECT_EQ(5, reader->offset());
  EXPECT_FALSE(reader->done());

  EXPECT_EQ(2, reader->ReadToBuffer(2, storage));
  EXPECT_EQ(kExpect.substr(5, 2), StringPiece(storage, 2));
  storage += 2;
  EXPECT_EQ(7, reader->offset());
  EXPECT_FALSE(reader->done());

  EXPECT_EQ(
      kExpect.size() - 7, reader->ReadToBuffer(sizeof(buffer) - 7, storage));
  EXPECT_EQ(kExpect.substr(7), StringPiece(storage, kExpect.size() - 7));
  EXPECT_EQ(kExpect.size(), reader->offset());
  EXPECT_TRUE(reader->done());
  EXPECT_FALSE(reader->error());
  EXPECT_TRUE(reader->ok());

  EXPECT_EQ(kExpect, StringPiece(buffer, kExpect.size()));
}

TEST_F(CompositeDataReaderTestFixture, CompositeStringErrors) {
  MockDataReader* mock_reader = new MockDataReader;
  std::vector<DataReader*>* list = MakeReaderList();
  list->push_back(mock_reader);
  googleapis::util::Status status(StatusUnknown("Test Error"));
  std::unique_ptr<DataReader> reader(
      NewManagedCompositeDataReader(
          *list, NewCompositeReaderListAndContainerDeleter(list)));
  std::unique_ptr<Closure> poke_status(
      NewPermanentCallback(mock_reader, &MockDataReader::poke_status, status));
  EXPECT_CALL(*mock_reader, DoReadToBuffer(_, _))
      .WillOnce(
          DoAll(
              InvokeWithoutArgs(poke_status.get(), &Closure::Run),
              Return(0)));

  char got[100];
  EXPECT_EQ(kExpect.size(),
            reader->ReadToBuffer(sizeof(got), got));
  EXPECT_EQ(kExpect, StringPiece(got, kExpect.size()));
  EXPECT_EQ(reader->offset(), kExpect.size());
  EXPECT_TRUE(reader->done());
  EXPECT_TRUE(reader->error());
  EXPECT_FALSE(reader->ok());
  EXPECT_EQ(status.ToString(), reader->status().ToString());
}

}  // namespace googleapis

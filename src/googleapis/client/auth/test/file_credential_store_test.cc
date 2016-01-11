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


#include "googleapis/client/auth/file_credential_store.h"

#include <sys/stat.h>
#include <stdlib.h>

#include <memory>
#include <string>
using std::string;

#include "googleapis/client/auth/credential_store.h"
#include "googleapis/client/data/codec.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/util/file_utils.h"
#include "googleapis/client/util/test/googleapis_gtest.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"
#include <gmock/gmock.h>
#include "googleapis/util/canonical_errors.h"
#include "googleapis/util/status_test_util.h"

namespace googleapis {

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;

using client::AuthorizationCredential;
using client::Codec;
using client::CodecFactory;
using client::DataReader;
using client::CredentialStore;
using client::GetTestingTempDir;
using client::HttpRequest;
using client::JoinPath;
using client::FileCredentialStoreFactory;
using client::NewManagedInMemoryDataReader;
using client::SensitiveFileUtils;
using client::StatusOk;

class MockAuthorizationCredential : public AuthorizationCredential {
 public:
  MOCK_CONST_METHOD0(type, const string());
  MOCK_METHOD1(AuthorizeRequest, googleapis::util::Status(HttpRequest* request));
  MOCK_METHOD0(Refresh, googleapis::util::Status());
  MOCK_METHOD1(RefreshAsync, void(Callback1<util::Status>* callback));
  MOCK_METHOD1(Load, googleapis::util::Status(DataReader* reader));
  MOCK_CONST_METHOD0(MakeDataReader, DataReader*());
};

class MockCodec : public Codec {
 public:
  MOCK_METHOD3(NewManagedEncodingReader,
               DataReader*(DataReader*, Closure*, googleapis::util::Status*));
  MOCK_METHOD3(NewManagedDecodingReader,
               DataReader*(DataReader*, Closure*, googleapis::util::Status*));
};

class MockCodecFactory : public CodecFactory {
 public:
  MOCK_METHOD1(New, Codec*(util::Status*));
};

class FileCredentialStoreFixture : public testing::Test {
 public:
  void ValidateReaderData(const string& expect, DataReader* reader) {
    string got = reader->RemainderToString();
    EXPECT_EQ(expect, got);
  }

  void SetStatus(const googleapis::util::Status& value, googleapis::util::Status* status) {
    *status = value;
  }

  DataReader* NewTransformReader(
      const string& expect,
      const string& provide,
      DataReader* reader, Closure* deleter, googleapis::util::Status* status) {
    *status = StatusOk();
    EXPECT_EQ(expect, reader->RemainderToString());
    return NewManagedInMemoryDataReader(provide, deleter);
  }

  void TransformString(
      const string& value, const string&, string* target) {
    *target = value;
  }
};

TEST_F(FileCredentialStoreFixture, TestCreateDir) {
  const string kRoot = StrCat(GetTestingTempDir(), "/test_create_dir");
  const string kClientId = "test_client_id";
  File::Delete(kRoot);
  ASSERT_TRUE(util::IsNotFound(file::Exists(kRoot, file::Defaults())));

  googleapis::util::Status status;
  FileCredentialStoreFactory factory(kRoot);
  std::unique_ptr<CredentialStore> store(
      factory.NewCredentialStore(kClientId, &status));
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_TRUE(store.get() != NULL);
  status = SensitiveFileUtils::VerifyIsSecureDirectory(kRoot);
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_TRUE(util::IsNotFound(
      file::Exists(JoinPath(kRoot, kClientId), file::Defaults())));

  store.reset(factory.NewCredentialStore(kClientId, &status));
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_TRUE(store.get() != NULL);
  EXPECT_TRUE(SensitiveFileUtils::VerifyIsSecureDirectory(kRoot).ok());
  EXPECT_TRUE(util::IsNotFound(
      file::Exists(JoinPath(kRoot, kClientId), file::Defaults())));

  File::Delete(kRoot);
}

#ifndef _MSC_VER
TEST_F(FileCredentialStoreFixture, TestInvalidDir) {
  const string kRoot = StrCat(GetTestingTempDir(), "/test_invalid_dir");
  const string kClientId = "test_client_id";
  File::Delete(kRoot);
  ASSERT_TRUE(util::IsNotFound(file::Exists(kRoot, file::Defaults())));

  const mode_t bad_permissions = S_IRUSR | S_IWUSR | S_IXGRP;
  EXPECT_TRUE(
    File::RecursivelyCreateDirWithPermissions(
        kRoot.c_str(), bad_permissions).ok());

  googleapis::util::Status status;
  FileCredentialStoreFactory factory(kRoot);
  std::unique_ptr<CredentialStore> store(
      factory.NewCredentialStore(kClientId, &status));
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(NULL, store.get());
  File::Delete(kRoot);
}

TEST_F(FileCredentialStoreFixture, TestStoreFile) {
  const string kRoot = StrCat(GetTestingTempDir(), "/test_store");
  const string kClientId = "test_client_id";
  const string kKey = "file";
  const string kFullPath = JoinPath(JoinPath(kRoot, kKey), kClientId);

  googleapis::util::Status status;
  FileCredentialStoreFactory factory(kRoot);
  std::unique_ptr<CredentialStore> store(
      factory.NewCredentialStore(kClientId, &status));
  EXPECT_TRUE(store->Delete(kKey).ok());  // delete entry if it already exists.

  const string kReaderData = "TestCredentialData";
  MockAuthorizationCredential mock_credential;
  EXPECT_CALL(mock_credential, MakeDataReader())
      .WillOnce(Return(NewManagedInMemoryDataReader(kReaderData)));

  status = store->Store(kKey, mock_credential);
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_TRUE(util::IsNotFound(
      file::Exists(JoinPath(kRoot, kClientId), file::Defaults())));
  EXPECT_TRUE(SensitiveFileUtils::VerifyIsSecureDirectory(kRoot).ok());
  EXPECT_TRUE(SensitiveFileUtils::VerifyIsSecureFile(kFullPath, true).ok());

  typedef Callback1<DataReader*> ValidateReaderCallback;
  std::unique_ptr<ValidateReaderCallback> closure(
      NewPermanentCallback(
          this, &FileCredentialStoreFixture::ValidateReaderData,
          kReaderData));

  EXPECT_CALL(mock_credential, Load(_))
      .WillOnce(
          DoAll(
              Invoke(closure.get(), &ValidateReaderCallback::Run),
              Return(StatusOk())));
  status = store->InitCredential(kKey, &mock_credential);
  EXPECT_TRUE(status.ok()) << status.ToString();

  status = store->Delete(kKey);
  EXPECT_TRUE(util::IsNotFound(file::Exists(kFullPath, file::Defaults())));
}
#else
TEST_F(FileCredentialStoreFixture, DISABLED_TestInvalidDir) {
  EXPECT_TRUE(false) << "Not implemented because ACLs are not yet supported";
}
#endif

TEST_F(FileCredentialStoreFixture, TestStoreEncodedFile) {
  MockCodec* mock_codec = new MockCodec;
  MockCodecFactory* codec_factory = new MockCodecFactory;

  // The factory will return our mock_codec, which we'll setup
  // below.
  typedef Callback1< googleapis::util::Status*> SetStatusCallback;
  std::unique_ptr<SetStatusCallback> set_status(NewPermanentCallback(
      this, &FileCredentialStoreFixture::SetStatus, StatusOk()));
  EXPECT_CALL(*codec_factory, New(_))
      .WillOnce(
          DoAll(
              Invoke(set_status.get(), &SetStatusCallback::Run),
              Return(mock_codec)));

  // Set up the store factory with our codec_factory.
  const string kRoot = StrCat(GetTestingTempDir(), "/test_encoded");
  const string kClientId = "test_client_id";
  const string kKey = "file";
  const string kFullPath = JoinPath(JoinPath(kRoot, kKey), kClientId);

  googleapis::util::Status status;
  FileCredentialStoreFactory factory(kRoot);
  factory.set_codec_factory(codec_factory);
  std::unique_ptr<CredentialStore> store(
      factory.NewCredentialStore(kClientId, &status));

  // The encoder expects the kOriginalString and encode it to the
  // kExpectEncode string.
  const string kOriginalString("StringToStore");
  const string kExpectEncode("TheEncodedValue");

  typedef ResultCallback3<DataReader*, DataReader*, Closure*, googleapis::util::Status*>
      NewTransformReaderCallback;
  std::unique_ptr<NewTransformReaderCallback> encode_reader(
      NewPermanentCallback(
          this, &FileCredentialStoreFixture::NewTransformReader,
          kOriginalString, kExpectEncode));
  EXPECT_CALL(*mock_codec, NewManagedEncodingReader(_, _, _))
              .WillOnce(
                  Invoke(encode_reader.get(),
                         &NewTransformReaderCallback::Run));

  // The credential will encode itself to the expected unencoded string.
  // So the string going into the store should be the encoded one.
  MockAuthorizationCredential mock_credential;
  EXPECT_CALL(mock_credential, MakeDataReader())
      .WillOnce(Return(NewManagedInMemoryDataReader(kOriginalString)));
  status = store->Store(kKey, mock_credential);
  EXPECT_TRUE(status.ok()) << status.ToString();


  // When we load the credential from the store, we should run the
  // stored encoded string back through the decoder to get the
  // original string back.
  std::unique_ptr<NewTransformReaderCallback> decode_reader(
      NewPermanentCallback(
          this, &FileCredentialStoreFixture::NewTransformReader,
          kExpectEncode, kOriginalString));
  EXPECT_CALL(*mock_codec, NewManagedDecodingReader(_, _, _))
              .WillOnce(
                  Invoke(decode_reader.get(),
                         &NewTransformReaderCallback::Run));

  typedef Callback1<DataReader*> ValidateReaderCallback;
  std::unique_ptr<ValidateReaderCallback> closure(
      NewPermanentCallback(
          this, &FileCredentialStoreFixture::ValidateReaderData,
          kOriginalString));
  EXPECT_CALL(mock_credential, Load(_))
      .WillOnce(
          DoAll(
              Invoke(closure.get(), &ValidateReaderCallback::Run),
              Return(StatusOk())));
  status = store->InitCredential(kKey, &mock_credential);
  EXPECT_TRUE(status.ok()) << status.ToString();
}

TEST_F(FileCredentialStoreFixture, TestHomeDir) {
  const char* kExtraPath;
  const string kRoot = StrCat(GetTestingTempDir(), "/home_test");
#ifndef _MSC_VER
  ASSERT_EQ(0, setenv("HOME", kRoot.c_str(), true));
  kExtraPath = ".googleapis/credentials";
#else
  ASSERT_EQ(0, putenv(StrCat("APPDATA=", kRoot.c_str()).c_str()));
  kExtraPath = "googleapis/credentials";
#endif
  string expect = JoinPath(kRoot, kExtraPath);
  string home_path;
  EXPECT_TRUE(
      FileCredentialStoreFactory::GetSystemHomeDirectoryStorePath(&home_path)
      .ok());
  EXPECT_EQ(expect, home_path);
}

}  // namespace googleapis

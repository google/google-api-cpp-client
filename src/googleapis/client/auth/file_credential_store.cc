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

#include <memory>
#include <string>
using std::string;

#include "googleapis/client/data/codec.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/util/file_utils.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include <glog/logging.h>
#include "googleapis/base/macros.h"
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

using client::FileCredentialStoreFactory;
using client::StatusOk;
using client::StatusUnimplemented;
using client::StatusUnknown;

namespace client {

class FileCredentialStore : public CredentialStore {
 public:
  FileCredentialStore(const string& root_path, const string& client_id)
      : root_path_(root_path), client_id_(client_id) {
  }
  virtual ~FileCredentialStore() {}

  googleapis::util::Status InitCredential(
       const string& user, AuthorizationCredential* credential) {
    const string path = UserToPath(user);
    googleapis::util::Status status =
          SensitiveFileUtils::VerifyIsSecureFile(path, true);
    if (!status.ok()) return status;

    std::unique_ptr<DataReader> file_reader(NewUnmanagedFileDataReader(path));
    std::unique_ptr<DataReader> decoder(
        EncodedToDecodingReader(file_reader.release(), &status));
    if (!status.ok()) return status;

    return credential->Load(decoder.get());
  }

  googleapis::util::Status Store(
       const string& user, const AuthorizationCredential& credential) {
    std::unique_ptr<DataReader> credential_reader(credential.MakeDataReader());
    string dir_path = UserToDir(user);
    googleapis::util::Status status =
          SensitiveFileUtils::CreateSecureDirectoryRecursively(dir_path);
    if (!status.ok()) return status;

    std::unique_ptr<DataReader> encoder(
        DecodedToEncodingReader(credential_reader.release(), &status));
    if (!status.ok()) return status;

    string serialized = encoder->RemainderToString();
    if (!encoder->ok()) {
      return StatusUnknown(
          StrCat("Cant serialize credential: ",
                 encoder->status().error_message()));
    }

    const string file_path = UserToPath(user);
    return SensitiveFileUtils::WriteSensitiveStringToFile(
        serialized, file_path, true);
  }

  googleapis::util::Status Delete(const string& user) {
    return SensitiveFileUtils::DeleteSensitiveFile(UserToPath(user));
  }

 private:
  const string root_path_;
  const string client_id_;

  // Credentials are stored in file with the client_id name in the
  // user directory. This function returns the directory for a user that
  // contains all the credentials. It is a helper function for creating it.
  string UserToDir(const string& user) const {
    return JoinPath(root_path_, user);
  }

  // The path is <root>/<user>/<client_id>
  string UserToPath(const string& user) const {
    return JoinPath(JoinPath(root_path_, user), client_id_);
  }
  DISALLOW_COPY_AND_ASSIGN(FileCredentialStore);
};

// static
util::Status FileCredentialStoreFactory::GetSystemHomeDirectoryStorePath(
    string* path) {
#ifdef _MSC_VER
  const char kVariableName[] = "APPDATA";
  const char kDirPath[] = "googleapis/credentials";
#else
  const char kVariableName[] = "HOME";
  const char kDirPath[] = ".googleapis/credentials";
#endif

  const char* home = getenv(kVariableName);
  if (home == NULL) {
    googleapis::util::Status status =  StatusInternalError(
        StrCat(kVariableName, " environment variable is not defined"));
    LOG(WARNING) << status.error_message();
    return status;
  }
#ifdef _MSC_VER
  *path = JoinPath(FromWindowsPath(home), kDirPath);
#else
  *path = JoinPath(home, kDirPath);
#endif
  return StatusOk();
}

// Construct the factory.
FileCredentialStoreFactory::FileCredentialStoreFactory(const string& root_path)
  : root_path_(root_path) {
  if (root_path_.empty()) {
    LOG(WARNING) << "Base path for file credential store is empty";
  }
}

FileCredentialStoreFactory::~FileCredentialStoreFactory() {
}

CredentialStore* FileCredentialStoreFactory::NewCredentialStore(
    const string& client_id, googleapis::util::Status* status) const {
  *status = SensitiveFileUtils::CreateSecureDirectoryRecursively(root_path_);
  if (status->ok()) {
    std::unique_ptr<FileCredentialStore> store(
        new FileCredentialStore(root_path_, client_id));
    CodecFactory* factory = codec_factory();
    if (factory) {
      store->set_codec(factory->New(status));
      if (!status->ok()) return NULL;
    }
    return store.release();
  }
  return NULL;
}

}  // namespace client

}  // namespace googleapis

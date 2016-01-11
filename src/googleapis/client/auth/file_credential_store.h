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

#ifndef GOOGLEAPIS_AUTH_FILE_CREDENTIAL_STORE_H_
#define GOOGLEAPIS_AUTH_FILE_CREDENTIAL_STORE_H_

#include <string>
using std::string;
#include "googleapis/client/auth/credential_store.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {

/*
 * A concrete CredentialStore that writes to files.
 * @ingroup AuthSupport
 *
 * Stores credentials as individual files using the user_name name.
 * The root_path is the root directory of the store.
 * It must be user-read/writable only. If the path does not exist, then it
 * will be created with user-only read/write/examine permissions.
 *
 * @note  The store is in the structure [root]/[user name]/[client id]
 * which is not quite how the API feels but might make more sense to manage.
 *
 * @warning This factory stores plain text files to the given path.
 *          It only permits user read/write permissions on the files
 *          and directories, however is still dangerous because if the
 *          files are compromised, the refresh tokens will be insecure.
 *
 * @warning You can encrypt/decrypt the files by binding a
 *          CodecFactory that performs encryption/decryption
 *          to this factory. However, such a factory is not included with
 *          the SDK at this time. You will need to write one yourself.
 *
 * @warning The user_name used here is not verified in any way. It corresponds
 *          to the cloud user, not the user on the local device. A given
 *          device user may have multiple cloud user names. The expectation
 *          is that the provided user_name is the cloud name, but it is not
 *          enforced. This could lead to accidents if the files are compromised
 *          or the program provides a name to the store different than the
 *          name with which it received the credentials being stored.
 *
 * @see Codec
 */
class FileCredentialStoreFactory : public CredentialStoreFactory {
 public:
  /*
   * Standard constructor.
   * @param[in] root_path It is expected, but not required, that root_path
   *            is the result of GetSystemHomeDirectoryStorePath().
   */
  explicit FileCredentialStoreFactory(const string& root_path);

  /*
   * Standard destructor.
   */
  ~FileCredentialStoreFactory();

  /*
   * Returns the root path the store was constructed with.
   */
  const string& root_path() const { return root_path_; }

  /*
   * Creates a new store for the given client id.
   *
   * @param[in] client_id The client this store is for is used as the filename.
   * @param[out] status Set with the reason for failure if NULL is returned.
   *
   * @return NULL is returned on failure.
   */
  virtual CredentialStore* NewCredentialStore(
      const string& client_id, googleapis::util::Status* status) const;

  /*
   * Returns the path in the $HOME directory for the googleapis store.
   *
   * This user is the local OS user, not the googleapis cloud user.
   * The cloud user data will be stored within this local OS user.
   *
   * @param[out] path The home directory path for storing credentials.
   * @return ok or reason for directory could not be determined.
   */
  static googleapis::util::Status GetSystemHomeDirectoryStorePath(string* path);

 private:
  const string root_path_;
  DISALLOW_COPY_AND_ASSIGN(FileCredentialStoreFactory);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_AUTH_FILE_CREDENTIAL_STORE_H_

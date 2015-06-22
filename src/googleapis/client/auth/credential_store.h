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

/*
 * @defgroup AuthSupport Auth Support - Generic Components
 *
 * The Authentication and Authorization Support module includes some
 * components that may be of use to libraries and applications independent
 * of the explicit OAuth 2.0 specific support.
 *
 * It does not implement a concrete authorization mechanism itself.
 * The OAuth 2.0 module is built using this abstraction to provide OAuth 2.0
 * support. Separating the abstraction from the mechanism provides the
 * decoupling desired in the design of the HTTP transport layer and can
 * extend that to other consumers that only care about credentials not not
 * necessarily OAuth 2.0 in particular.
 */
#ifndef GOOGLEAPIS_AUTH_CREDENTIAL_STORE_H_
#define GOOGLEAPIS_AUTH_CREDENTIAL_STORE_H_

#include <memory>
#include <string>
using std::string;
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {

class Codec;
class CodecFactory;
class DataReader;

/*
 * Base class for a data store of persisted credentials.
 * @ingroup AuthSupport
 *
 * This interface is in terms of the DataReader that the abstract
 * AuthorizationCredential uses. Therefore it is suitable for any
 * type of credential derived from AuthorizationCredential, including
 * the OAuth2Credential introduced in the OAuth 2.0 module.
 *
 * @warning
 * The library does not currently provide encryption. However, for security
 * you are encouraged to encrypt the data streams if possible. This will
 * prevent authorization and refresh tokens from being readable should the
 * persisted store become compromised. The refresh token still requires the
 * client secret to turn into an access token.
 *
 * Although no encryption mechanism is provided at this time, the
 * CredentialStore will accomodate one injected using a Codec that you can
 * write.
 *
 * @see Codec
 * @see AuthorizationCredential
 */
class CredentialStore {
 public:
  /*
   * Standard constructor.
   */
  CredentialStore();

  /*
   * Standard destructor.
   */
  virtual ~CredentialStore();

  /*
   * Sets the Codec that this store should use for re-encoding and decoding
   * data streams.
   *
   * The intention here is to encrypt and decrypt but the codec can be used
   * for any purpose.
   *
   * @param[in] codec Ownership is passsed to the store. NULL is permitted
   *                  to mean do not perform any encryption or decryption.
   */
  void set_codec(Codec* codec);

  /*
   * Returns the codec for this store.
   *
   * @return NULL if the store is not encrypted.
   */
  Codec* codec() const  { return codec_.get(); }

  /*
   * Restore a credential for the given user name.
   *
   * @param[in] user_name The key to store from
   * @param[out] credential The credential to load into.
   *
   * @return success if the credential could be restored. A successful result
   *         requires that a credential had been stored at some earlier time.
   */
  virtual googleapis::util::Status InitCredential(
       const string& user_name, AuthorizationCredential* credential) = 0;

  /*
   * Stores the credential under the given user_name.
   *
   * This will replace any previously stored credential for the user_name.
   *
   * @param[in] user_name The key to store the credential under.
   * @param[in] credential The credential to store.
   *
   * @returns success if the credential could be stored successfully.
   */
  virtual googleapis::util::Status Store(
       const string& user_name,
       const AuthorizationCredential& credential) = 0;

  /*
   * Deletes the credential with the given user_name.
   *
   * @param[in] user_name The key to remove.
   * @return success if the key no longer exists in the store.
   */
  virtual googleapis::util::Status Delete(const string& user_name) = 0;

 protected:
  /*
   * Applies the codec (if any) to decode a reader.
   *
   * @param[in] reader The caller passes ownership to the reader.
   * @param[out] status Success if the reader could be decoded.
   * @return The decoded stream. Ownership is passed back to the caller.
   *         This will always return a reader, though it may be an
   *         InvalidDataReader if there is an error.
   */
  DataReader* DecodedToEncodingReader(
      DataReader* reader, googleapis::util::Status* status);

  /*
   * Applies the codec (if any) to encode a reader.
   *
   * @param[in] reader The caller passes ownership to the reader.
   * @param[out] status Success if the reader could be encoded.
   * @return The encoded stream. Ownership is passed back to the caller.
   *         This will always return a reader, though it may be an
   *         InvalidDataReader if there is an error.
   */
  DataReader* EncodedToDecodingReader(
      DataReader* reader, googleapis::util::Status* status);

 private:
  std::unique_ptr<Codec> codec_;

  DISALLOW_COPY_AND_ASSIGN(CredentialStore);
};

/*
 * Creates a CredentialStore.
 * @ingroup AuthSupport
 *
 * Implements a Factory Pattern to create a credential store.
 * This is used to inject a credential store where lazy initialization might be
 * required.
 *
 * CredentialStore class instances are scoped to an individual client_id.
 */
class CredentialStoreFactory {
 public:
  /*
   * Standard constructor.
   */
  CredentialStoreFactory();

  /*
   * Standard destructor.
   */
  virtual ~CredentialStoreFactory();

  /*
   * Sets a factory for creating the codec to assign to new instances.
   *
   * @param codec_factory NULL means do not set a codec on new instances.
   *        otherwise the factory is used to create new codec instances to
   *        bind to new store instances created by this factory.
   */
  void set_codec_factory(CodecFactory* codec_factory);

  /*
   * Returns the codec_factory for this factory.
   */
  CodecFactory* codec_factory() const  { return codec_factory_.get(); }

  /*
   * Creates a new CredentialStore instance.
   *
   * @param[in] client_id The client ID to scope the store to.
   * @param[out] status The reason for failure if NULL is returned.
   * @return new store instance on success or NULL on failure.
   */
  virtual CredentialStore* NewCredentialStore(
      const string& client_id, googleapis::util::Status* status) const = 0;

 private:
  std::unique_ptr<CodecFactory> codec_factory_;

  DISALLOW_COPY_AND_ASSIGN(CredentialStoreFactory);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_AUTH_CREDENTIAL_STORE_H_

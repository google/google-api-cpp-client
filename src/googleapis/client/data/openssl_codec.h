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


#ifndef GOOGLEAPIS_DATA_OPENSSL_CODEC_H_
#define GOOGLEAPIS_DATA_OPENSSL_CODEC_H_

#include <string>
using std::string;

#include "googleapis/client/data/codec.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/base/macros.h"
#include <openssl/ossl_typ.h>
namespace googleapis {


class StringPiece;

namespace client {

/*
 * Provdes a codec for encrypting and decrypting reader streams using OpenSSL.
 * @ingroup DataLayerCodec
 *
 * @see OpenSslCodecFactory
 */
class OpenSslCodec : public Codec {
 public:
  /*
   * Standard constructor.
   * @see Init
   */
  OpenSslCodec();

  /*
   * Standard destructor.
   */
  virtual ~OpenSslCodec();

  /*
   * Sets the chunk size to use when encoding/decoding.
   *
   * @param[in] chunk_size Must be > 0
   *                       and should be a multiple of the cipher block size.
   *
   * There is probably no need for changing this, but helps testing.
   */
  void set_chunk_size(int chunk_size) {
    CHECK_LT(0, chunk_size_);
    chunk_size_ = chunk_size;
  }

  /*
   * Initializes with the cipher type, key, and initialization vector.
   *
   * @param[in] cipher_type  OpenSsl cipher type.
   * @param[in] key          The cipher key.
   * @param[in] iv           The cipher initialization vector.
   * @return ok or reason for failure.
   */
  googleapis::util::Status Init(
       const EVP_CIPHER* cipher_type, const string& key, const string& iv);

  /*
   * Returns a reader that will encode another reader using this codec.
   *
   * @param[in] reader The caller maintain ownership.
   * @param[in] deleter The managed deleter may be used to delete the
   *                    reader. NULL indicates an unmanaged reader.
   * @param[out] status Will indicate ok or reason for failure.
   *
   * @return The reader may be an InvalidDataReader on failure but will
   *         not be NULL.
   */
  virtual DataReader* NewManagedEncodingReader(
      DataReader* reader, Closure* deleter, googleapis::util::Status* status);

  /*
   * Returns a reader that will decode another reader using this codec.
   *
   * @param[in] reader The caller maintain ownership.
   * @param[in] deleter The managed deleter may be used to delete the
   *                    reader. NULL indicates an unmanaged reader.
   * @param[out] status Will indicate ok or reason for failure.
   *
   * @return The reader may be an InvalidDataReader on failure but will
   *         not be NULL.
   */
  virtual DataReader* NewManagedDecodingReader(
      DataReader* reader, Closure* deleter, googleapis::util::Status* status);

 private:
  const EVP_CIPHER* cipher_type_;
  string key_;
  string iv_;
  int chunk_size_;

  DISALLOW_COPY_AND_ASSIGN(OpenSslCodec);
};

/*
 * CodecFactory for creating and configuring OpenSslCodecs.
 * @ingroup DataLayerCodec
 *
 * To configure creted ciphers, set the type, md, and aalt.
 */
class OpenSslCodecFactory : public CodecFactory {
 public:
  /*
   * Standard constructor.
   */
  OpenSslCodecFactory();

  /*
   * Standard destructor.
   */
  ~OpenSslCodecFactory();

  /*
   * Sets the cipher_type for new codecs.
   *
   * This interface does not currently provide access to EVP_CIPHER_CTX_ctrl
   * so RC5 and RC2 might not be viable options without further enhacement.
   *
   * @param[in] type The OpenSsl cipher type.
   */
  void set_cipher_type(const EVP_CIPHER* type) { cipher_type_ = type; }

  /*
   * Sets the message digest algorithm for new codecs.
   *
   * @param[in] md The OpenSsl message digest algorithm.
   */
  void set_md(EVP_MD* md) { md_ = md; }

  /*
   * Sets the salt value to configure the algorithms with.
   *
   * @param[in] data The salt value to use should be exactly 8 bytes.
   */
  void set_salt(const string& data) { salt_ = data; }

  /*
   * Sets the chunk size to use when encoding/decoding.
   *
   * @param[in] chunk_size Must be > 0
   *                       and should be a multiple of the cipher block size.
   *
   * There is probably no need for changing this, but helps testing.
   */
  void set_chunk_size(int chunk_size) {
    CHECK_LT(0, chunk_size_);
    chunk_size_ = chunk_size;
  }

  /*
   * Computes the key and initialization vector from a passphrase.
   *
   * @param[in] passphrase The passphrase to use.
   * @return ok or reason for failure.
   */
  googleapis::util::Status SetPassphrase(const StringPiece& passphrase);

  /*
   * Constructs and configures a new codec instance.
   *
   * @param[out] status ok or reason for failure.
   * @return NULL on failure.
   */
  virtual Codec* New(util::Status* status);


 private:
  const EVP_CIPHER* cipher_type_;
  const EVP_MD* md_;
  string key_;
  string iv_;
  string salt_;
  int chunk_size_;
  int iterations_;

  DISALLOW_COPY_AND_ASSIGN(OpenSslCodecFactory);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_OPENSSL_CODEC_H_

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


#ifndef GOOGLEAPIS_DATA_BASE64_CODEC_H_
#define GOOGLEAPIS_DATA_BASE64_CODEC_H_

#include <string>
using std::string;
#include "googleapis/client/data/codec.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {

/*
 * Provdes a codec for encoding and decoding reader streams using Base64.
 * @ingroup DataLayerCodec
 *
 * Base64 is specified in http://tools.ietf.org/html/rfc4648
 *
 * @see Base64CodecFactory
 */
class Base64Codec : public Codec {
 public:
  /*
   * Standard constructor.
   *
   * param[in] chunk_size The desired chunk size to use when
   *                      encoding / decoding streams might be adjusted
   *                      slightly internally for comaptability with base-64.
   * param[in] websafe If true then use the websafe base64 encoding.
   */
  Base64Codec(int chunk_size, bool websafe);

  /*
   * Standard destructor.
   */
  virtual ~Base64Codec();

  /*
   * Returns a reader that will encode another reader using this codec.
   *
   * @param[in] reader The caller maintain ownership.
   * @param[in] deleter The managed deleter may be used to delete the
   *                    reader. NULL indicates an unmanaged reader.
   * @param[out] status Will indicate ok or reason for failure.
   *
   * @return The reader  will be an InvalidDataReader on failure, not NULL.
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
   * @return The reader will be an InvalidDataReader on failure, not NULL.
   */
  virtual DataReader* NewManagedDecodingReader(
      DataReader* reader, Closure* deleter, googleapis::util::Status* status);

 private:
  int chunk_size_;
  bool websafe_;

  DISALLOW_COPY_AND_ASSIGN(Base64Codec);
};

/*
 * CodecFactory for creating and configuring Base64Codecs.
 * @ingroup DataLayerCodec
 */
class Base64CodecFactory : public CodecFactory {
 public:
  /*
   * Standard constructor.
   *
   * This will construct standard (non-websafe) base64 encodings by default.
   */
  Base64CodecFactory();

  /*
   * Sets the desired chunk size for codecs.
   *
   * @param[in] size The desired size used to configure new Codec instances.
   */
  void set_chunk_size(int size) { chunk_size_ = size; }

  /*
   * Returns the desired chunk size.
   */
  int chunk_size() const { return chunk_size_; }

  /*
   * Whether to construct encoders with the websafe encoding.
   */
  void set_websafe(bool websafe) { websafe_ = websafe; }

  /*
   * Returns whether to construct encoders with the websafe encoding.
   */
  bool websafe() const { return websafe_; }

  /*
   * Standard destructor.
   */
  ~Base64CodecFactory();

  /*
   * Constructs and configures a new codec instance.
   *
   * @param[out] status ok or reason for failure.
   * @return NULL on failure.
   */
  virtual Codec* New(util::Status* status);


 private:
  int chunk_size_;
  bool websafe_;

  DISALLOW_COPY_AND_ASSIGN(Base64CodecFactory);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_BASE64_CODEC_H_

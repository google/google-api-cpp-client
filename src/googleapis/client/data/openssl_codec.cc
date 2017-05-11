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


#include <string>
using std::string;
#include "googleapis/strings/stringpiece.h"
#include <glog/logging.h>
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/openssl_codec.h"
#include "googleapis/client/util/status.h"

#include "openssl/ossl_typ.h"
#include "openssl/evp.h"
#include "openssl/err.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace {
using client::StatusOk;
using client::StatusUnknown;
using client::StatusFailedPrecondition;
using client::CodecReader;
using client::DataReader;

util::Status OpenSslErrorToStatus(const StringPiece& what) {
  char reason[256];
  ERR_error_string_n(ERR_get_error(), reason, sizeof(reason));
  return StatusUnknown(StrCat(what, ": ", reason));
}

class OpenSslReader : public CodecReader {
 public:
  OpenSslReader(
      Closure* deleter,
      DataReader* source,
      const EVP_CIPHER* type,
      const StringPiece& key,
      const StringPiece& iv,
      int chunk_size,
      bool encrypting)
      : CodecReader(source, deleter,
                    chunk_size, chunk_size + EVP_MAX_BLOCK_LENGTH,
                    encrypting),
        cipher_type_(type),
        key_(key.as_string()),
        iv_(iv.as_string()) {
    CHECK_LT(0, chunk_size);
    EVP_CIPHER_CTX_init(ctx_);
  }

  ~OpenSslReader() {
    EVP_CIPHER_CTX_cleanup(ctx_);
  }

  googleapis::util::Status Init() {
    EVP_CipherInit_ex(ctx_, cipher_type_, NULL, NULL, NULL, encoding());
    EVP_CIPHER_CTX_set_key_length(ctx_, key_.size());
    EVP_CipherInit_ex(
        ctx_,
        NULL, NULL,
        reinterpret_cast<const unsigned char*>(key_.data()),
        reinterpret_cast<const unsigned char*>(iv_.c_str()),
        encoding());
    return CodecReader::Init();
  }

 protected:
  virtual googleapis::util::Status EncodeChunk(
      const StringPiece& chunk, bool is_final_chunk,
      char* to, int64* to_length) {
    return EncodeDecodeChunk(chunk, is_final_chunk, to, to_length);
  }


  virtual googleapis::util::Status DecodeChunk(
      const StringPiece& chunk, bool is_final_chunk,
      char* to, int64* to_length) {
    return EncodeDecodeChunk(chunk, is_final_chunk, to, to_length);
  }

 private:
  EVP_CIPHER_CTX* ctx_;
  const EVP_CIPHER* cipher_type_;
  string key_;
  string iv_;

 private:
  googleapis::util::Status EncodeDecodeChunk(
       const StringPiece& chunk, bool is_final_chunk,
       char* to, int64* to_length) {
    int out_len = 0;
    int extra_len = 0;
    const unsigned char* data =
        reinterpret_cast<const unsigned char*>(chunk.data());
    unsigned char* base_address = reinterpret_cast<unsigned char*>(to);
    int read = chunk.size();
    if (read && !EVP_CipherUpdate(ctx_, base_address, &out_len,
                                  data, read)) {
      *to_length = 0;
      return OpenSslErrorToStatus("CipherUpdate failed");
    }
    if (is_final_chunk) {
      if (!EVP_CipherFinal_ex(
              ctx_, base_address + out_len, &extra_len)) {
        *to_length = 0;
        return OpenSslErrorToStatus("CipherFinal failed");
      }
    }
    *to_length = out_len + extra_len;
    return StatusOk();
  }

  DISALLOW_COPY_AND_ASSIGN(OpenSslReader);
};

}  // anonymous namespace

namespace client {

OpenSslCodec::OpenSslCodec() : cipher_type_(NULL), chunk_size_(1024) {
}

OpenSslCodec::~OpenSslCodec() {}

util::Status OpenSslCodec::Init(
     const EVP_CIPHER* cipher_type, const string& key, const string& iv) {
  if (cipher_type == NULL) {
    return StatusInvalidArgument("cipher_type not set");
  } else if (key.empty() || iv.empty()) {
    return StatusInvalidArgument("Passphrase not set");
  }

  cipher_type_ = cipher_type;
  key_ = key;
  iv_ = iv;
  return StatusOk();
}


DataReader* OpenSslCodec::NewManagedEncodingReader(
    DataReader* reader, Closure* deleter, googleapis::util::Status* status) {
  if (key_.empty() || iv_.empty() || !cipher_type_) {
    *status = StatusFailedPrecondition("Init not called");
    return NewManagedInvalidDataReader(*status, deleter);
  }
  std::unique_ptr<OpenSslReader> open_reader(
      new OpenSslReader(deleter, reader, cipher_type_, key_, iv_,
                        chunk_size_, true));
  *status = open_reader->Init();
  if (status->ok()) {
    return open_reader.release();
  }
  return NULL;
}

DataReader* OpenSslCodec::NewManagedDecodingReader(
    DataReader* reader, Closure* deleter, googleapis::util::Status* status) {
  if (key_.empty() || iv_.empty() || !cipher_type_) {
    *status = StatusFailedPrecondition("Init not called");
    return NewManagedInvalidDataReader(*status, deleter);
  }

  std::unique_ptr<OpenSslReader> open_reader(
      new OpenSslReader(deleter, reader, cipher_type_, key_, iv_,
                        chunk_size_, false));
  *status = open_reader->Init();
  if (status->ok()) {
    return open_reader.release();
  }
  return NULL;
}

OpenSslCodecFactory::OpenSslCodecFactory()
    : cipher_type_(EVP_aes_128_cbc()), md_(EVP_sha1()),
      chunk_size_(1024), iterations_(16) {
  // Initialize salt with 8 bytes of 0's.
  const int64 salt = 0;
  salt_.assign(reinterpret_cast<const char *>(&salt), 8);
}

OpenSslCodecFactory::~OpenSslCodecFactory() {}

util::Status OpenSslCodecFactory::SetPassphrase(
     const StringPiece& passphrase) {
  if (salt_.size() != 8) {
    return StatusInvalidArgument("Salt must be exactly 8 bytes.");
  }

  unsigned char key_data[EVP_MAX_KEY_LENGTH];
  unsigned char iv_data[EVP_MAX_IV_LENGTH];
  int size = EVP_BytesToKey(
      cipher_type_, md_,
      reinterpret_cast<const unsigned char*>(salt_.c_str()),
      reinterpret_cast<const unsigned char*>(passphrase.data()),
      passphrase.size(), iterations_,
      key_data, iv_data);
  if (size <= 0) {
    return OpenSslErrorToStatus("EVP_BytesToKey failed");
  }
  key_ = string(reinterpret_cast<char*>(key_data), size);
  iv_ = string(reinterpret_cast<char*>(iv_data),
               EVP_CIPHER_iv_length(cipher_type_));
  return StatusOk();
}

Codec* OpenSslCodecFactory::New(util::Status* status) {
  OpenSslCodec* codec = NULL;
  if (cipher_type_ == NULL) {
    *status = StatusInvalidArgument("cipher_type not set");
  } else if (key_.empty() || iv_.empty()) {
    *status = StatusInvalidArgument("Passphrase not set");
  } else {
    codec = new OpenSslCodec;
    *status = codec->Init(cipher_type_, key_, iv_);
    codec->set_chunk_size(chunk_size_);
    if (!status->ok()) {
      delete codec;
      codec = NULL;
    }
  }
  return codec;
}

}  // namespace client

}  // namespace googleapis

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
#include <glog/logging.h>
#include <memory>
#include <string>

using std::string;
#include "googleapis/client/auth/jwt_builder.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/strcat.h"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/stack.h>
#include <openssl/x509.h>

namespace googleapis {

namespace client {

util::Status JwtBuilder::LoadPrivateKeyFromPkcs12Path(
    const string& path, string* result_key) {
  googleapis::util::Status status;
  result_key->clear();
  OpenSSL_add_all_algorithms();
  BIO* bio = BIO_new(BIO_s_mem());
  EVP_PKEY* pkey = LoadPkeyFromP12Path(path.c_str());
  if (!pkey) {
    status = StatusUnknown(
        StrCat("OpenSSL failed parsing PKCS#12 error=", ERR_get_error()));
  } else if (PEM_write_bio_PrivateKey(
      bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) < 0) {
    status = StatusUnknown("OpenSSL Failed writing BIO memory output");
  }

  if (status.ok()) {
    BUF_MEM *mem_ptr = nullptr;
    BIO_get_mem_ptr(bio, &mem_ptr);
    result_key->assign(mem_ptr->data, mem_ptr->length);  // copies data out
  }

  BIO_free(bio);
  EVP_PKEY_free(pkey);
  return status;
}

void JwtBuilder::AppendAsBase64(const char* cdata, size_t size, string* to) {
  // RFC 4648 section 5
  static const char map[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  const unsigned char* const data =
      reinterpret_cast<const unsigned char*>(cdata);
  size_t const last_group_size = size % 3;
  size_t const end = size - last_group_size;
  size_t i = 0;
  for (i = 0; i < end; i += 3) {
    // Pack 3 octets into group.
    size_t group = data[i] << 16 | data[i + 1] << 8 | data[i + 2];
    // Unpack 4 sextets from the group.
    for (int shift = 18; shift >= 0; shift -= 6) {
      to->push_back(map[(group >> shift) & 0x3f]);
    }
  }
  if (last_group_size == 2) {
    // Pack 2 octets into group.
    size_t group = data[i] << 16 | data[i + 1] << 8;
    // Unpack 3 sextets from the group.
    for (int shift = 18; shift >= 6; shift -= 6) {
      to->push_back(map[(group >> shift) & 0x3f]);
    }
  } else if (last_group_size == 1) {
    // Pack 1 octet into group.
    size_t group = data[i] << 16;
    // Unpack 2 sextets from the group.
    for (int shift = 18; shift >= 12; shift -= 6) {
      to->push_back(map[(group >> shift) & 0x3f]);
    }
  }
}

void JwtBuilder::AppendAsBase64(const string& from, string* to) {
  AppendAsBase64(from.data(), from.size(), to);
}

EVP_PKEY* JwtBuilder::LoadPkeyFromData(const StringPiece data) {
  BIO* bio = BIO_new_mem_buf(const_cast<char*>(data.data()), data.size());
  EVP_PKEY* pkey = PEM_read_bio_PrivateKey(
      bio, nullptr, nullptr, const_cast<char*>("notasecret"));
  if (pkey == nullptr) {
    char buffer[128];
    ERR_error_string(ERR_get_error(), buffer);
    LOG(ERROR) << "OpenSslError reading private key: " << buffer;
  }
  BIO_free(bio);
  return pkey;
}

EVP_PKEY* JwtBuilder::LoadPkeyFromP12Path(const char* pkcs12_key_path) {
  //    OpenSSL_add_all_algorithms();
  X509 *cert = nullptr;
  STACK_OF(X509) *ca = nullptr;
  PKCS12 *p12;

  FILE* fp = fopen(pkcs12_key_path, "rb");
  CHECK(fp != nullptr);
  p12 = d2i_PKCS12_fp(fp, nullptr);
  fclose(fp);
  if (!p12) {
    googleapis::util::Status status = StatusUnknown(
        StrCat("OpenSSL failed reading PKCS#12 error=", ERR_get_error()));
    LOG(ERROR) << status.error_message();
    return nullptr;
  }

  EVP_PKEY* pkey = nullptr;
  int ok = PKCS12_parse(p12, "notasecret", &pkey, &cert, &ca);
  PKCS12_free(p12);
  if (cert) {
    X509_free(cert);
  }
  if (ca) {
    sk_X509_pop_free(ca, X509_free);
  }

  CHECK(ok);
  return pkey;
}

util::Status JwtBuilder::MakeJwtUsingEvp(
    const string& claims, EVP_PKEY* pkey, string* jwt) {
  const char* plain_header = "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
  string data_to_sign;
  AppendAsBase64(plain_header, &data_to_sign);
  data_to_sign.append(".");
  AppendAsBase64(claims, &data_to_sign);

  googleapis::util::Status status;
  std::unique_ptr<EVP_MD_CTX, void(*)(EVP_MD_CTX*)>
                  ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  EVP_SignInit(ctx.get(), EVP_sha256());
  EVP_SignUpdate(ctx.get(), data_to_sign.c_str(), data_to_sign.size());

  unsigned int buffer_size = EVP_PKEY_size(pkey);
  std::unique_ptr<char[]> buffer(new char[buffer_size]);

  if (EVP_SignFinal(
          ctx.get(),
          reinterpret_cast<unsigned char*>(buffer.get()),
          &buffer_size,
          pkey) == 0) {
    status = StatusInternalError(
        StrCat("Failed signing JWT. error=", ERR_get_error()));
  }

  if (!status.ok()) return status;

  jwt->swap(data_to_sign);
  jwt->append(".");
  AppendAsBase64(buffer.get(), buffer_size, jwt);
  return StatusOk();
}

}  // namespace client

}  // namespace googleapis

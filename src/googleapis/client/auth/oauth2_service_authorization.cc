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

#include "googleapis/config.h"
#include "googleapis/client/auth/oauth2_service_authorization.h"

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/file_utils.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/strings/escaping.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/util/file.h"
#include <openssl/ossl_typ.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

namespace googleapis {

namespace client {

/*
 * A helper class used to create the JWT we pass to the OAuth2.0 server.
 *
 * This class could be broken out into its own module if it is needed elsewhere
 * or to test more explicitly.
 */
class JwtBuilder {
 public:
  static googleapis::util::Status LoadPrivateKeyFromPkcs12Path(
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
        bio, pkey, NULL, NULL, 0, NULL, NULL) < 0) {
      status = StatusUnknown("OpenSSL Failed writing BIO memory output");
    }

    if (status.ok()) {
      BUF_MEM *mem_ptr = NULL;
      BIO_get_mem_ptr(bio, &mem_ptr);
      result_key->assign(mem_ptr->data, mem_ptr->length);  // copies data out
    }

    BIO_free(bio);
    EVP_PKEY_free(pkey);
    return status;
  }

  static void AppendAsBase64(const StringPiece& from, string* to) {
    string encoded;
    strings::WebSafeBase64Escape(
        reinterpret_cast<const unsigned char*>(from.data()),
        from.size(), &encoded, false);
    to->append(encoded);
  }

  static EVP_PKEY* LoadPkeyFromData(const StringPiece& data) {
    BIO* bio = BIO_new_mem_buf(const_cast<char*>(data.data()), data.size());
    EVP_PKEY* pkey =
        PEM_read_bio_PrivateKey(bio, NULL, 0, const_cast<char*>("notasecret"));
    if (pkey == NULL) {
      char buffer[128];
      ERR_error_string(ERR_get_error(), buffer);
      LOG(ERROR) << "OpenSslError reading private key: " << buffer;
    }
    BIO_free(bio);
    return pkey;
  }

  static EVP_PKEY* LoadPkeyFromP12Path(const char* pkcs12_key_path) {
    //    OpenSSL_add_all_algorithms();
    X509 *cert = NULL;
    STACK_OF(X509) *ca = NULL;
    PKCS12 *p12;

    FILE* fp = fopen(pkcs12_key_path, "rb");
    CHECK(fp != NULL);
    p12 = d2i_PKCS12_fp(fp, NULL);
    fclose(fp);
    if (!p12) {
      googleapis::util::Status status = StatusUnknown(
          StrCat("OpenSSL failed reading PKCS#12 error=", ERR_get_error()));
      LOG(ERROR) << status.error_message();
      return NULL;
    }

    EVP_PKEY* pkey = NULL;
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

  static googleapis::util::Status MakeJwtUsingEvp(
      const string& claims, EVP_PKEY* pkey, string* jwt) {
    const char* plain_header = "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
    string data_to_sign;
    AppendAsBase64(plain_header, &data_to_sign);
    data_to_sign.append(".");
    AppendAsBase64(claims, &data_to_sign);

    googleapis::util::Status status;
    EVP_MD_CTX ctx;
    EVP_SignInit(&ctx, EVP_sha256());
    EVP_SignUpdate(&ctx, data_to_sign.c_str(), data_to_sign.size());

    unsigned int buffer_size = EVP_PKEY_size(pkey);
    std::unique_ptr<char[]> buffer(new char[buffer_size]);

    if (EVP_SignFinal(
            &ctx,
            reinterpret_cast<unsigned char*>(buffer.get()),
            &buffer_size,
            pkey) == 0) {
      status = StatusInternalError(
          StrCat("Failed signing JWT. error=", ERR_get_error()));
    }

    EVP_MD_CTX_cleanup(&ctx);

    if (!status.ok()) return status;

    jwt->swap(data_to_sign);
    jwt->append(".");
    AppendAsBase64(StringPiece(buffer.get(), buffer_size), jwt);
    return StatusOk();
  }
};

OAuth2ServiceAccountFlow::OAuth2ServiceAccountFlow(
    HttpTransport* transport)
    : OAuth2AuthorizationFlow(transport) {
}

OAuth2ServiceAccountFlow::~OAuth2ServiceAccountFlow() {
}

void OAuth2ServiceAccountFlow::set_private_key(const string& key) {
  DCHECK(p12_path_.empty());
  private_key_ = key;
}

util::Status OAuth2ServiceAccountFlow::SetPrivateKeyPkcs12Path(
    const string& path) {
  DCHECK(private_key_.empty());
  p12_path_.clear();
  googleapis::util::Status status = SensitiveFileUtils::VerifyIsSecureFile(path, false);
  if (!status.ok()) return status;
  p12_path_ = path;
  return StatusOk();
}

util::Status OAuth2ServiceAccountFlow::InitFromJsonData(
    const SimpleJsonData* data) {
  googleapis::util::Status status = OAuth2AuthorizationFlow::InitFromJsonData(data);
  if (!status.ok()) return status;

  if (!GetStringAttribute(data, "client_email", &client_email_)) {
    return StatusInvalidArgument(StrCat("Missing client_email attribute"));
  }

  return StatusOk();
}

util::Status OAuth2ServiceAccountFlow::PerformRefreshToken(
    const OAuth2RequestOptions& options, OAuth2Credential* credential) {
  string claims = MakeJwtClaims(options);
  string jwt;

  googleapis::util::Status status = MakeJwt(claims, &jwt);
  if (!status.ok()) return status;

  string grant_type = "urn:ietf:params:oauth:grant-type:jwt-bearer";
  string content =
      StrCat("grant_type=", EscapeForUrl(grant_type), "&assertion=", jwt);

  std::unique_ptr<HttpRequest> request(
      transport()->NewHttpRequest(HttpRequest::POST));
  if (options.timeout_ms > 0) {
    request->mutable_options()->set_timeout_ms(options.timeout_ms);
  }
  request->set_url(client_spec().token_uri());
  request->set_content_type(HttpRequest::ContentType_FORM_URL_ENCODED);
  request->set_content_reader(NewUnmanagedInMemoryDataReader(content));

  status = request->Execute();
  if (status.ok()) {
    status = credential->Update(request->response()->body_reader());
  } else {
    VLOG(1) << "Failed to update credential";
  }
  return status;
}

string OAuth2ServiceAccountFlow::MakeJwtClaims(
    const OAuth2RequestOptions& options) const {
  time_t now = DateTime().ToEpochTime();
  int duration_secs = 60 * 60;  // 1 hour
  const string* scopes = &options.scopes;

  if (scopes->empty()) {
    scopes = &default_scopes();
    if (scopes->empty()) {
      LOG(WARNING) << "Making claims without any scopes";
    }
  }

  string claims = "{";
  const string sep(",");
  if (!options.email.empty()) {
    AppendJsonStringAttribute(&claims, sep, "prn", options.email);
  }

  AppendJsonStringAttribute(&claims, "", "scope", *scopes);
  AppendJsonStringAttribute(&claims, sep, "iss", client_email_);
  AppendJsonStringAttribute(&claims, sep, "aud", client_spec().token_uri());
  AppendJsonScalarAttribute(&claims, sep, "exp", now + duration_secs);
  AppendJsonScalarAttribute(&claims, sep, "iat", now);
  StrAppend(&claims, "}");

  return claims;
}

util::Status OAuth2ServiceAccountFlow::ConstructSignedJwt(
    const StringPiece& plain_claims, string* result) const {
  return MakeJwt(plain_claims.as_string(), result);
}

util::Status OAuth2ServiceAccountFlow::MakeJwt(
    const string& claims, string* jwt) const {
  EVP_PKEY* pkey = NULL;
  if (!p12_path_.empty()) {
    DCHECK(private_key_.empty());
    VLOG(1) << "Loading private key from " << p12_path_;
    pkey = JwtBuilder::LoadPkeyFromP12Path(p12_path_.c_str());
  } else if (!private_key_.empty()) {
    pkey = JwtBuilder::LoadPkeyFromData(private_key_);
  } else {
    return StatusInternalError("PrivateKey not set");
  }

  if (!pkey) {
    return StatusInternalError("Could not load pkey");
  }
  googleapis::util::Status status = JwtBuilder::MakeJwtUsingEvp(claims, pkey, jwt);
  EVP_PKEY_free(pkey);
  return status;
}

}  // namespace client

}  // namespace googleapis

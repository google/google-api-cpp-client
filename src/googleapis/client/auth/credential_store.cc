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
#include "googleapis/client/auth/credential_store.h"
#include "googleapis/client/data/codec.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"

namespace googleapis {

namespace client {

CredentialStoreFactory::CredentialStoreFactory() {}
CredentialStoreFactory::~CredentialStoreFactory() {}

void CredentialStoreFactory::set_codec_factory(CodecFactory* codec_factory) {
  codec_factory_.reset(codec_factory);
}

CredentialStore::CredentialStore() {}
CredentialStore::~CredentialStore() {}

void CredentialStore::set_codec(
    Codec* codec) {
  codec_.reset(codec);
}

static DataReader* WrapReader(DataReader* input_reader, Codec* codec,
                              bool with_encoder, googleapis::util::Status* status) {
  if (!codec) {
    *status = StatusOk();
    return input_reader;  // passes ownership back
  }

  Closure* closure = DeletePointerClosure(input_reader);
  return with_encoder
      ? codec->NewManagedEncodingReader(input_reader, closure, status)
      : codec->NewManagedDecodingReader(input_reader, closure, status);
}

DataReader* CredentialStore::DecodedToEncodingReader(
    DataReader* input_reader, googleapis::util::Status* status) {
  return WrapReader(input_reader, codec_.get(), true, status);
}

DataReader* CredentialStore::EncodedToDecodingReader(
    DataReader* input_reader, googleapis::util::Status* status) {
  return WrapReader(input_reader, codec_.get(), false, status);
}

}  // namespace client

}  // namespace googleapis

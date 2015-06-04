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
#include <string>
using std::string;
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/openssl_codec.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include <gtest/gtest.h>

namespace googleapis {

using client::Codec;
using client::DataReader;
using client::OpenSslCodec;
using client::OpenSslCodecFactory;
using client::NewUnmanagedInMemoryDataReader;

const char kPassphraseX[] = "PassphraseX";

// FWIW, 24 chars is the size of the client_secret we'll use in practice.
const char kPassphraseY[] = "abcdefghijklmnopqrstuvwx";

class OpenSslCodecTestFixture : public testing::Test {
};

TEST_F(OpenSslCodecTestFixture, TestEncryptDecrypt) {
  googleapis::util::Status status;
  const StringPiece kPlainText = "Hello, World!";

  OpenSslCodecFactory factoryX;
  status = factoryX.SetPassphrase(kPassphraseX);
  ASSERT_TRUE(status.ok()) << status.error_message();

  std::unique_ptr<Codec> codecX(factoryX.New(&status));
  ASSERT_TRUE(status.ok()) << status.error_message();

  string encryptedX;
  status = codecX->Encode(kPlainText, &encryptedX);
  EXPECT_TRUE(status.ok()) << status.error_message();

  OpenSslCodecFactory factoryY;
  EXPECT_TRUE(factoryY.SetPassphrase(kPassphraseY).ok());
  std::unique_ptr<Codec> codecY(factoryY.New(&status));
  EXPECT_TRUE(status.ok()) << status.error_message();

  string encryptedY;
  status = codecY->Encode(kPlainText, &encryptedY);
  EXPECT_TRUE(status.ok()) << status.error_message();

  EXPECT_NE(encryptedX, encryptedY);

  string decryptedX;
  status = codecX->Decode(encryptedX, &decryptedX);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(kPlainText, decryptedX);

  string decryptedY;
  status = codecY->Decode(encryptedY, &decryptedY);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(kPlainText, decryptedY);
}

TEST_F(OpenSslCodecTestFixture, TestEncryptingReader) {
  googleapis::util::Status status;
  const StringPiece kPlainText = "Hello, World!";
  OpenSslCodecFactory factory;
  EXPECT_TRUE(factory.SetPassphrase(kPassphraseX).ok());

  std::unique_ptr<Codec> codec(factory.New(&status));
  ASSERT_TRUE(status.ok());

  std::unique_ptr<DataReader> plain_reader(
      NewUnmanagedInMemoryDataReader(kPlainText));

  std::unique_ptr<DataReader> encrypting_reader(
      codec->NewUnmanagedEncodingReader(plain_reader.get(), &status));
  ASSERT_TRUE(status.ok()) << status.error_message();

  string got;
  while (!encrypting_reader->done()) {
    char c;
    int64 read = encrypting_reader->ReadToBuffer(1, &c);
    EXPECT_EQ(1, read);
    got.push_back(c);
  }
  EXPECT_TRUE(encrypting_reader->ok());

  EXPECT_EQ(0, encrypting_reader->SetOffset(0));
  string another_got = encrypting_reader->RemainderToString();

  string plain;
  status = codec->Decode(got, &plain);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(kPlainText, plain);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kPlainText, plain);

  EXPECT_EQ(got, another_got);
  status = codec->Decode(another_got, &plain);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(kPlainText, plain);
}

TEST_F(OpenSslCodecTestFixture, TestDecryptingReader) {
  googleapis::util::Status status;
  const StringPiece kPlainText = "Hello, World!";
  OpenSslCodecFactory factory;
  EXPECT_TRUE(factory.SetPassphrase(kPassphraseX).ok());
  std::unique_ptr<Codec> codec(factory.New(&status));
  ASSERT_TRUE(status.ok());

  string encoded;
  status = codec->Encode(kPlainText, &encoded);
  ASSERT_TRUE(status.ok());

  std::unique_ptr<DataReader> encoded_reader(
      NewUnmanagedInMemoryDataReader(encoded));

  std::unique_ptr<DataReader> decrypting_reader(
      codec->NewUnmanagedDecodingReader(encoded_reader.get(), &status));
  ASSERT_TRUE(status.ok()) << status.error_message();

  string got;
  while (!decrypting_reader->done()) {
    char c;
    int64 read = decrypting_reader->ReadToBuffer(1, &c);
    if (read == 0 && decrypting_reader->done()) {
      break;
    }
    EXPECT_EQ(1, read);
    got.push_back(c);
  }
  EXPECT_TRUE(decrypting_reader->ok());
  EXPECT_EQ(kPlainText, got);

  EXPECT_EQ(0, decrypting_reader->SetOffset(0));
  EXPECT_EQ(kPlainText, decrypting_reader->RemainderToString());
}


TEST_F(OpenSslCodecTestFixture, TestSeekDecryptingReader) {
  string plain_text;
  for (int i = 0; i < 200; ++i) {
    plain_text.push_back(i);
  }

  googleapis::util::Status status;
  OpenSslCodecFactory factory;
  factory.set_chunk_size(32);
  EXPECT_TRUE(factory.SetPassphrase(kPassphraseX).ok());
  std::unique_ptr<Codec> codec(factory.New(&status));
  ASSERT_TRUE(status.ok()) << status.error_message();

  string encoded;
  status = codec->Encode(plain_text, &encoded);
  ASSERT_TRUE(status.ok()) << status.error_message();

  std::unique_ptr<DataReader> encoded_reader(
      NewUnmanagedInMemoryDataReader(encoded));

  std::unique_ptr<DataReader> decrypting_reader(
      codec->NewUnmanagedDecodingReader(encoded_reader.get(), &status));
  ASSERT_TRUE(status.ok()) << status.error_message();

  // Read the second half then the first half, just for the sake for seeking
  // around at non-block offsets.
  EXPECT_EQ(100, decrypting_reader->SetOffset(100));
  string back_half = decrypting_reader->RemainderToString();

  EXPECT_EQ(1, decrypting_reader->SetOffset(1));
  string front_part;
  EXPECT_EQ(99, decrypting_reader->ReadToString(99, &front_part));

  EXPECT_EQ(0, decrypting_reader->SetOffset(0));
  char first_char;
  EXPECT_EQ(1, decrypting_reader->ReadToBuffer(1, &first_char));

  string got = StrCat(StringPiece(&first_char, 1), front_part, back_half);
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(i, static_cast<unsigned char>(got[i]));
  }
}

}  // namespace googleapis

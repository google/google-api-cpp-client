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
#include "googleapis/client/data/base64_codec.h"
#include "googleapis/client/util/escaping.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include <gtest/gtest.h>

namespace googleapis {

using client::Codec;
using client::DataReader;
using client::Base64Codec;
using client::Base64CodecFactory;
using client::NewUnmanagedInMemoryDataReader;

class Base64CodecTestFixture : public testing::Test {
};

TEST_F(Base64CodecTestFixture, TestSimple) {
  Base64CodecFactory factory;
  googleapis::util::Status status;
  std::unique_ptr<Codec> codec(factory.New(&status));
  ASSERT_TRUE(status.ok()) << status.error_message();

  const StringPiece kPlain = "Hello, World!";
  const StringPiece kEncoded = "SGVsbG8sIFdvcmxkIQ==";

  string got;
  status = codec->Encode(kPlain, &got);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kEncoded, got);

  status = codec->Decode(kEncoded, &got);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(kPlain, got);

  std::unique_ptr<DataReader> plain_reader(
      NewUnmanagedInMemoryDataReader(kPlain));
  std::unique_ptr<DataReader> encoding_reader(
      codec->NewUnmanagedEncodingReader(plain_reader.get(), &status));
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(kEncoded, encoding_reader->RemainderToString());

  encoding_reader.reset(NewUnmanagedInMemoryDataReader(kEncoded));
  plain_reader.reset(
      codec->NewUnmanagedDecodingReader(encoding_reader.get(), &status));
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(kPlain, plain_reader->RemainderToString());
}

TEST_F(Base64CodecTestFixture, TestEncodeDecode) {
  string plain_text;
  for (int i = 0; i < 200; ++i) {
    plain_text.push_back(i);
  }

  for (int chunk_size = 31; chunk_size < 35; ++chunk_size) {
    for (int data_size = 190; data_size < 200; ++data_size) {
      const StringPiece kPlainText =
          StringPiece(plain_text).substr(0, data_size);

      googleapis::util::Status status;
      Base64CodecFactory factory;
      factory.set_chunk_size(chunk_size);
      std::unique_ptr<Codec> codec(factory.New(&status));
      ASSERT_TRUE(status.ok()) << status.error_message();

      string encoded;
      status = codec->Encode(kPlainText, &encoded);
      EXPECT_TRUE(status.ok())
          << status.error_message()
          << "chunk_size=" << chunk_size
          << " data_size=" << data_size;

      string decoded;
      status = codec->Decode(encoded, &decoded);
      EXPECT_TRUE(status.ok())
          << status.error_message()
          << "chunk_size=" << chunk_size
          << " data_size=" << data_size;
      EXPECT_TRUE(kPlainText == decoded)
          << "chunk_size=" << chunk_size
          << " data_size=" << data_size;
    }
  }
}

TEST_F(Base64CodecTestFixture, TestEncodingReader) {
  googleapis::util::Status status;
  const StringPiece kPlainText = "Hello, World!";
  Base64CodecFactory factory;
  std::unique_ptr<Codec> codec(factory.New(&status));
  ASSERT_TRUE(status.ok());

  std::unique_ptr<DataReader> plain_reader(
      NewUnmanagedInMemoryDataReader(kPlainText));

  std::unique_ptr<DataReader> encoding_reader(
      codec->NewUnmanagedEncodingReader(plain_reader.get(), &status));
  ASSERT_TRUE(status.ok()) << status.error_message();

  string got;
  while (!encoding_reader->done()) {
    char c;
    int64 read = encoding_reader->ReadToBuffer(1, &c);
    EXPECT_EQ(1, read);
    got.push_back(c);
  }
  EXPECT_TRUE(encoding_reader->ok());

  EXPECT_EQ(0, encoding_reader->SetOffset(0));
  string another_got = encoding_reader->RemainderToString();

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

TEST_F(Base64CodecTestFixture, TestDecodeingReader) {
  googleapis::util::Status status;
  const StringPiece kPlainText = "Hello, World!";
  Base64CodecFactory factory;
  std::unique_ptr<Codec> codec(factory.New(&status));
  ASSERT_TRUE(status.ok());

  string encoded;
  status = codec->Encode(kPlainText, &encoded);
  ASSERT_TRUE(status.ok());

  std::unique_ptr<DataReader> encoded_reader(
      NewUnmanagedInMemoryDataReader(encoded));

  std::unique_ptr<DataReader> decoding_reader(
      codec->NewUnmanagedDecodingReader(encoded_reader.get(), &status));
  ASSERT_TRUE(status.ok()) << status.error_message();

  string got;
  while (!decoding_reader->done()) {
    char c;
    int64 read = decoding_reader->ReadToBuffer(1, &c);
    if (read == 0 && decoding_reader->done()) {
      break;
    }
    EXPECT_EQ(1, read);
    got.push_back(c);
  }
  EXPECT_TRUE(decoding_reader->ok());
  EXPECT_EQ(kPlainText, got);

  EXPECT_EQ(0, decoding_reader->SetOffset(0));
  EXPECT_EQ(kPlainText, decoding_reader->RemainderToString());
}


TEST_F(Base64CodecTestFixture, TestSeekDecodingReader) {
  string plain_text;
  for (int i = 0; i < 200; ++i) {
    plain_text.push_back(i);
  }

  googleapis::util::Status status;
  Base64CodecFactory factory;
  factory.set_chunk_size(32);
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

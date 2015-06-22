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
#include <algorithm>
#include "googleapis/client/data/codec.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

namespace client {

Codec::Codec() {
}

Codec::~Codec() {
}

util::Status Codec::Encode(const StringPiece& plain, string* encoded) {
  std::unique_ptr<DataReader> source(NewUnmanagedInMemoryDataReader(plain));
  googleapis::util::Status status;
  std::unique_ptr<DataReader> reader(
      NewUnmanagedEncodingReader(source.get(), &status));
  if (status.ok()) {
    *encoded = reader->RemainderToString();
    status = reader->status();
  } else {
    encoded->clear();
  }
  return status;
}

util::Status Codec::Decode(const StringPiece& encoded, string* plain) {
  std::unique_ptr<DataReader> source(NewUnmanagedInMemoryDataReader(encoded));
  googleapis::util::Status status;
  std::unique_ptr<DataReader> reader(
      NewUnmanagedDecodingReader(source.get(), &status));
  if (status.ok()) {
    *plain = reader->RemainderToString();
    status = reader->status();
  } else {
    plain->clear();
  }
  return status;
}


CodecFactory::CodecFactory() {}
CodecFactory::~CodecFactory() {}

struct CodecReader::Buffer {
  explicit Buffer(int size)
      : allocated(size),
        storage(new char[size]),
        ptr(storage.get()), end(ptr) {
  }
  void Clear() {
    ptr = end = storage.get();
  }

  int allocated;
  std::unique_ptr<char[]> storage;
  const char* ptr;
  const char* end;
};

CodecReader::CodecReader(
    DataReader* source, Closure* deleter,
    int64 chunk_size, int64 buffer_size, bool encoding)
    : DataReader(deleter), source_(source),
      chunk_size_(chunk_size), encoding_(encoding), read_final_(false) {
  CHECK_LT(0, chunk_size);
  CHECK_LT(0, buffer_size);

  chunk_.reset(new char[chunk_size_]);
  buffer_.reset(new Buffer(buffer_size));
}

CodecReader::~CodecReader() {
}

util::Status CodecReader::Init() {
  buffer_->Clear();
  read_final_ = false;
  return googleapis::util::Status();
}

int CodecReader::MaybeFetchNextChunk() {
  if (buffer_->ptr >= buffer_->end) {
    char* out = reinterpret_cast<char *>(chunk_.get());
    int64 read = source_->ReadToBuffer(chunk_size_, out);
    if (source_->error()) {
      set_status(source_->status());
      return 0;
    }

    if (read > 0 || (source_->done() && !read_final_)) {
      googleapis::util::Status status;
      int64 transformed_len = buffer_->allocated;
      bool final_chunk = source_->done();
      if (encoding_) {
        status = EncodeChunk(StringPiece(chunk_.get(), read),
                             final_chunk,
                             buffer_->storage.get(), &transformed_len);
      } else {
        status = DecodeChunk(StringPiece(chunk_.get(), read),
                             final_chunk,
                             buffer_->storage.get(), &transformed_len);
      }
      CHECK_LE(transformed_len, buffer_->allocated);
      read_final_ = final_chunk;
      if (!status.ok()) {
        set_status(status);
        return 0;
      }
      buffer_->ptr = buffer_->storage.get();
      buffer_->end = buffer_->ptr + transformed_len;
    }
  }
  return buffer_->end - buffer_->ptr;
}

int64 CodecReader::DoReadToBuffer(int64 max_bytes, char* storage) {
  int64 have = MaybeFetchNextChunk();
  if (have > max_bytes) {
    have = max_bytes;
  }
  memcpy(storage, buffer_->ptr, have);
  buffer_->ptr += have;
  if (buffer_->ptr == buffer_->end) {
    set_done(source_->done());
  }
  return have;
}

int64 CodecReader::DoSetOffset(int64 to_offset) {
  int64 rel_bytes = to_offset - offset();
  if (rel_bytes <= buffer_->end - buffer_->ptr
      && rel_bytes >= buffer_->ptr - buffer_->storage.get()) {
    // Seek within our current chunk.
    buffer_->ptr += rel_bytes;
    return to_offset;
  }

  int position = 0;
  if (rel_bytes < 0) {
    // We have to seek back to the beginning then walk forward to
    // the position we are reading. Depending on the codec
    // we might be able to seek to intermediate chunk boundaries but we'll
    // generalize for worst case algorithm for simplicity.
    if (source_->SetOffset(0) < 0) {
      return -1;
    }
    if (!Init().ok()) {
      return -1;
    }
  } else if (buffer_->end > buffer_->ptr) {
    // Advance to the end of this chunk because loop below starts at next.
    position = offset() + buffer_->end - buffer_->ptr;
    buffer_->ptr = buffer_->end;
  }

  while (position < to_offset) {
    // Seek forward by grabbing each chunk so we go through the codec.
    int have = MaybeFetchNextChunk();
    int delta = std::min(have, static_cast<int>(to_offset - position));
    position += delta;
    buffer_->ptr += delta;
    if (done()) break;
  }

  return position;
}

}  // namespace client

}  // namespace googleapis

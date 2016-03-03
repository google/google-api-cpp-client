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
 * @defgroup DataLayerCodec Data Layer - Data Encoder/Decoders
 *
 * The raw data encoders/decoders are components in the data layer that are
 * responsible for transforming data. Typical examples are encoding,
 * encryption and compression. Transformations can be performed on entire
 * byte sequences or on individual chunks.
 */

#ifndef GOOGLEAPIS_DATA_CODEC_H_
#define GOOGLEAPIS_DATA_CODEC_H_

#include <memory>
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/macros.h"
namespace googleapis {

class StringPiece;

namespace client {

/*
 * Provides an interface for encoding and decoding data.
 * @ingroup DataLayerCodec
 *
 * This is a pure abstract class. It needs to be subclassed to provide
 * specific encodings and decodings, including encryption or compression.
 */
class Codec {
 public:
  /*
   * Standard constructor.
   */
  Codec();

  /*
   * Standard destructor.
   */
  virtual ~Codec();

  /*
   * Encodes a string.
   *
   * @param[in] decoded The unencoded string to encode is treated as binary
   *            data, not a null-terminated c-string.
   * @param[out] encoded The encoded string should be treated as binary data,
   *             not a null-terminated c-string. Use data(), not c_state()
   *             to get at the bytes if needed.
   * @return ok if the string could be encoded.
   *
   * @see Decode
   */
  virtual googleapis::util::Status Encode(
      const StringPiece& decoded, string* encoded);

  /*
   * Decodes a string.
   *
   * @param[in] encoded The encoded string to encode is treated as binary
   *            data, not a null-terminated c-string.
   * @param[out] unencoded The decoded string should be treated as binary data,
   *             not a null-terminated c-string. Use data(), not c_state()
   *             to get at the bytes if needed.
   * @return ok if the string could be decoded.
   *
   * @see Encode
   */
  virtual googleapis::util::Status Decode(
      const StringPiece& encoded, string* unencoded);

  /*
   * Creates a reader that encodes all the output of another reader.
   *
   * The default method returns an InMemory reader using the Decode method
   * on the input read into one monolithic string.
   *
   * Specialized encoder/decoders should override this to encode as the reader
   * streams if possible.
   *
   * @param[in] reader  The caller maintains ownership of reader to encode.
   *                    However, they may pass ownership to the deleter.
   * @param[in] deleter If not NULL then it will be Run() when the reader is
   *            destroyed.
   * @param[out] status ok on success, otherwise the reason for error.
   * @return ownership of a new reader that will return the decoded data.
   *         This will never be null, though may be an InvalidDataReader
   *         returning the same status on failure.
   *
   * @see NewManagedDecodingReader
   */
  virtual DataReader* NewManagedEncodingReader(
      DataReader* reader, Closure* deleter, googleapis::util::Status* status) = 0;

  /*
   * Creates a reader that decodes all the output of another reader.
   *
   * The default method returns an InMemory reader using the Decode method
   * on the input read into one monolithic string.
   *
   * Specialized decoder/encoders should override this to decode as the reader
   * streams if possible.
   *
   * @param[in] reader  The caller maintains ownership of reader to decode.
   *                    However, they may pass ownership to the deleter.
   * @param[in] deleter If not NULL then it will be Run() when the reader is
   *            destroyed.
   * @param[out] status ok on success, otherwise the reason for error.
   * @return ownership of a new reader that will return the encoded data.
   *         This will never be null, though may be an InvalidDataReader
   *         returning the same status on failure.
   *
   * @see NewManagedEnccodingReader
   */
  virtual DataReader* NewManagedDecodingReader(
      DataReader* reader, Closure* deleter, googleapis::util::Status* status) = 0;

  /*
   * Creates a reader that encodes the output of another reader.
   *
   * @param[in] reader The caller maintains ownership of the raeder to encode.
   * @param[out] status ok on success, otherwise the reason for error.
   *
   * @see NewManagedEncodingReader
   */
  DataReader* NewUnmanagedEncodingReader(
      DataReader* reader, googleapis::util::Status* status) {
    return NewManagedEncodingReader(reader, NULL, status);
  }

  /*
   * Creates a reader that decodes the output of another reader.
   *
   * @param[in] reader The caller maintains ownership of the raeder to decode.
   * @param[out] status ok on success, otherwise the reason for error.
   *
   * @see NewManagedDecodingReader
   */
  DataReader* NewUnmanagedDecodingReader(
      DataReader* reader, googleapis::util::Status* status) {
    return NewManagedDecodingReader(reader, NULL, status);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(Codec);
};

/*
 * A factory for creating Codec instances.
 * @ingroup DataLayerCodec
 *
 * This factory must be subclassed for a particualr concrete
 * <code>Codec</code>. The subclasses may provide additional configuration
 * parameters particular to the encoding/decoding scheme they implement.
 *
 * @see Codec
 */
class CodecFactory {
 public:
  /*
   * Standard constructor.
   */
  CodecFactory();

  /*
   * Standard destructor.
   */
  virtual ~CodecFactory();

  /*
   * The factory method that creates a new instance.
   *
   * @param[out] status ok on success, otherwise the reason for failure.
   * @return Ownership of the new instance is passed back to the caller.
   */
  virtual Codec* New(util::Status* status) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(CodecFactory);
};

/*
 * A helper class for implementing CodecReaders.
 *
 * This class assumes bounded transformation sizes for a given input size.
 */
class CodecReader : public DataReader {
 public:
  /*
   * Standard constructor.
   *
   * @param[in] source The untarnsformed stream the codec is processing.
   * @param[in] deleter The deleter if this is to be a managed stream.
   * @param[in] chunk_size The size of source data to accumulate for each
   *            transform (until the very last one)
   * @param[in] buffer_size The size to allocate for transformed data.
   * @param[in] encoding Whether this reader is encoding or decoding
   *            determines whether we'll EncodeChunk or DecodeChunk.
   */
  CodecReader(
      DataReader* source, Closure* deleter,
      int64 chunk_size, int64 buffer_size,
      bool encoding);

  /*
   * Standard destructor.
   */
  virtual ~CodecReader();

  /*
   * Called when resetting readers (seeking to start)
   */
  virtual googleapis::util::Status Init();

 protected:
  /*
   * Returns whether the reader was constructed for encoding or decoding.
   */
  bool encoding() const { return encoding_; }

  /*
   * Reads from internal transformed buffer, replentishing it as necessary.
   *
   * This method is compliant with the base DataReader::DoReadToBuffer
   * semantics. In particular, results are never non-negative and 0 does
   * not have any special meaning with regard to errors or being done.
   *
   * @param[in] max_bytes Max bytes to copy from buffer.
   * @param[in] storage The storage to read into.
   * @return Number of bytes read will never be more than the
   *         constructed buffer_size, at least for the base class.
   */
  virtual int64 DoReadToBuffer(int64 max_bytes, char* storage);

  /*
   * Implements seeking through the transformed stream.
   *
   * This method is compliant with the base DataReader::DoSetOffset semantics.
   */
  virtual int64 DoSetOffset(int64 to_offset);

  /*
   * Hook for codecs to encode a chunk.
   *
   * @param[in] from The raw chunk from the source stream.
   * @param[in] final True if this is the final chunk.
   * @param[in] to The target buffer must be allocated big enough.
   * @param[in,out] to_len The size of the 'to' buffer on input.
   *                       The amount of data written into 'to' on output.
   */
  virtual googleapis::util::Status EncodeChunk(
      const StringPiece& from, bool final, char* to, int64* to_len) = 0;

  /*
   * Hook for codecs to decode a chunk.
   *
   * @param[in] from The raw chunk from the source stream.
   * @param[in] final True if this is the final chunk.
   * @param[in] to The target buffer must be allocated big enough.
   * @param[in,out] to_len The size of the 'to' buffer on input.
   *                       The amount of data written into 'to' on output.
   */
  virtual googleapis::util::Status DecodeChunk(
      const StringPiece& from, bool final, char* to, int64* to_len) = 0;

 private:
  struct Buffer;
  DataReader* source_;
  int64 chunk_size_;
  std::unique_ptr<char[]> chunk_;
  std::unique_ptr<Buffer> buffer_;  // Waiting to read
  bool encoding_;
  bool read_final_;

  /*
   * Returns how much buffered data is available.
   */
  int MaybeFetchNextChunk();
  DISALLOW_COPY_AND_ASSIGN(CodecReader);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_CODEC_H_

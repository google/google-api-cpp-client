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
 * @defgroup DataLayerJson Data Layer - JSON Support
 *
 * The JSON Support module provides helper classes for using
 * <a href='http://tools.ietf.org/html/rfc4627'>RFC 4627 JSON</a>
 * as an encoding type.
 *
 * As far as dependencies go, the JSON Support module lives in the Data Layer.
 */

#ifndef GOOGLEAPIS_DATA_SERIALIZABLE_JSON_H_
#define GOOGLEAPIS_DATA_SERIALIZABLE_JSON_H_
#include <istream>  // NOLINT
#include <ostream>  // NOLINT
#include <string>
using std::string;

#include "googleapis/client/util/status.h"
namespace googleapis {

namespace client {
class DataReader;

/*
 * An abstract interface for data objects used to denote JSON compatability.
 * @ingroup DataLayerJson
 *
 * This is just an interface for a data object. At least at this time.
 * For a concrete class, see JsonCppData in jsoncpp_data.h.
 * The intent of making this an interface is to allow other underlying
 * implementations for experimentation without impacting much code that
 * just passes through data or converts to/from json wire protocol.
 */
class SerializableJson {
 public:
  /*
   * Standard destructor
   */
  virtual ~SerializableJson();

  /*
   * Clear the instance data back to default state.
   */
  virtual void Clear() = 0;

  /*
   * Initialize instance from a reader.
   *
   * @param [in] reader JSON-encoded byte stream.
   */
  virtual googleapis::util::Status LoadFromJsonReader(DataReader* reader) = 0;

  /*
   * Creates a reader that contains the serialized json for this object.
   *
   * @return A JSON-encoded byte stream with this object's state.
   *         If there is an error, the details will be in the reader status().
   */
  virtual DataReader* MakeJsonReader() const = 0;

  /*
   * Initializes this instance from a standard C++ istream.
   *
   * @param[in] stream JSON-encoded istream
   *
   * The default implementation creates an IstreamDataReader and calls
   * the Load() method.
   */
  virtual googleapis::util::Status LoadFromJsonStream(std::istream* stream);

  /*
   * Serialize the instance as a JSON document to an ostream.
   *
   * @param[in] stream The output stream to write to.
   *
   * The default implementation calls MakeJsonReader() and writes the
   * byte-stream to the output stream.
   */
  virtual googleapis::util::Status StoreToJsonStream(std::ostream* stream) const;
};

/*
 * Syntactic sugar for data.LoadFromStream(&stream).
 *
 * <pre>
 *   ostringstream output;
 *   output << data;
 * </pre>
 */
// NOLINT
inline std::istream& operator >>(
    std::istream &stream, SerializableJson& data) {
  data.LoadFromJsonStream(&stream).IgnoreError();
  return stream;
}

/*
 * Syntactic sugar for data.StoreToStream(&stream)
 *
 * <pre>
 *   string json = ...;
 *   istringstream input(json);
 *   data << input;
 * </pre>
 */
inline std::ostream& operator <<(
    std::ostream &stream, const SerializableJson& data) {
  data.StoreToJsonStream(&stream).IgnoreError();
  return stream;
}

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_SERIALIZABLE_JSON_H_

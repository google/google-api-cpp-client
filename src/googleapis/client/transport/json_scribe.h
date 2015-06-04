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


#ifndef GOOGLEAPIS_TRANSPORT_JSON_SCRIBE_H_
#define GOOGLEAPIS_TRANSPORT_JSON_SCRIBE_H_

#include <memory>
#include <string>
using std::string;

#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/base/integral_types.h"
#include <json/value.h>
#include <json/writer.h>
namespace googleapis {

namespace client {
class DataWriter;

/*
 * Specialized HttpScribe that produces JSON transcripts.
 * @ingroup TransportLayerTesting
 *
 * The scribe writes JSON directly to the writer. It does not proovide
 * an interactive interface to interpret the JSON it is writing. In order
 * to see the events, you must look at the writer.
 *
 * The implementation of this class may stream directly to the writer, in
 * which case it may not be well-formed if it did not finish properly
 * (e.g. it is still scribing or the process crashed). If that is the case,
 * you may need to  append the closing brackets and braces to make it well
 * formed if you wish to read it back as json.
 *
 * The transcript has the following JSON structure:
 *    {
 *       kStartTime : datetime
 *       kMaxSnippet: int64
 *       kMessages : [
 *         {
 *           kUrl : string
 *           kHttpCode : int           # none or 0 if no response
 *           kStatusCode : int         # transport error, if any
 *           kStatusMessage : string   # transport error, if any
 *           kRequest : {
 *             kHeaders : [
 *               string : string     # name, value pairs
 *               string : string
 *             ]
 *             kBytes : string
 *             kSize  : int64
 *             kCensored  : bool
 *           }
 *           kResponse : {
 *              (same as request but for response data)
 *           }
 *       ]
 *       kEndTime   : datetime
 *    }
 *
 * The individual fields are documented as class constants. Each constant also
 * documents the JSON format for the values.
 */
class JsonScribe : public HttpEntryScribe {
 public:
  /*
   * Constructor.
   *
   * @param[in] censor The caller retains ownership.
   * @param[in] writer Ownership is passed to the scribe. This writer will
   *            store the transcript.
   * @param[in] compact If truen then write compact json, otherwise stylized.
   */
  JsonScribe(
      HttpScribeCensor* censor, DataWriter* writer, bool compact = true);

  /*
   * Standard destructor.
   *
   * This will finish the JSON and flush the writer to make a well formed
   * JSON document transcript.
   */
  virtual ~JsonScribe();

  /*
   * Implements base Checkpoint method.
   */
  virtual void Checkpoint();

  //-------------------------  JSON Tag Constants  -------------------------

  static const char kStartTime[];  //< The transcript start time (date).
  static const char kMaxSnippet[];  //< The max_snippet size used (int64).
  static const char kEndTime[];    //< The transcript end time   (date).
  /*
   * The JSON object tag for the message sequence.
   *
   * The message sequence is a JSON array object where each element in the
   * array is a flat dictionary containing the per-request attributes.
   */
  static const char kMessages[];

  static const char kMethod[];     //< Request HTTP method type (string).
  static const char kUrl[];        //< Request url (string).

  /*
   * The HTTP response code, if any (int).
   *
   * If this tag is not present, or the value is 0, then a response was not
   * received. This could be because of a transport error in which case
   * the kStatusCode and kStatusMessage fields should be present. Otherwise
   * it means that a response was not yet recorded for whatever reason
   * including the transcript was finished before the request completed.
   */
  static const char kHttpCode[];

  /*
   * The transport googleapis::util::Status::error_code value (int).
   *
   * An error here indicates a transport error when sending the request.
   * The response was either never received or the request was never sent.
   *
   * Note that the status codes that specific errors produce is not yet
   * well defined so may change from release to release. The StatusMessage
   * will be of more value if you are physically inspecting these messages.
   *
   * A value of 0 indicates everything was ok. This is the only well defined
   * value that will not change in future releases. If this field is not
   * present, then the transport status is assumed to be OK.
   *
   * @see HttpRequestState::transport_status
   */
  static const char kStatusCode[];

  /*
   * The transport googleapis::util::Status::error_message value (string).
   *
   * If this is present and not empty then it gives an explanation for the
   * transport status. This is usually an error but not necessarily. The
   * kStatusCode field will indicate whether it is ok or not.
   *
   * The message values are intended to be human readable and may change from
   * release to release.
   */
  static const char kStatusMessage[];

  /*
   * Timestamp request was sent in microseconds since the epoch (int64).
   */
  static const char kSendMicros[];

  /*
   * Timestamp response was received in microseconds since the epoch (int64).
   */
  static const char kResponseMicros[];

  /*
   * Timestamp of transport error in microseconds since the epoch (int64).
   */
  static const char kErrorMicros[];

  static const char kRequest[];    //< Message request (dict).
  static const char kResponse[];   //< Message response (dict).

  // These are fields within the request and response dictionaries.
  static const char kPayload[];        //< Message payload data.
  static const char kPayloadSize[];    //< Real request payload size (int64).
  static const char kPayloadCensored[];  //< True if censored (bool).
  static const char kHeaders[];          //< Headers (dict).
  static const char kBatched[];          //< Requests for HttpBatchRequest

 protected:
  /*
   * Returns an entry that produces the individual JSON transcript for
   * the request.
   */
  virtual Entry* NewEntry(const HttpRequest* request);
  virtual Entry* NewBatchEntry(const HttpRequestBatch* batch);

 private:
  std::unique_ptr<DataWriter> writer_;
  std::unique_ptr<Json::Writer> json_writer_;
  int64 last_checkpoint_;
  bool  started_;
  Json::Value json_;
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_JSON_SCRIBE_H_

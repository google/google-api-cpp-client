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


#ifndef GOOGLEAPIS_TRANSPORT_JSON_PLAYBACK_TRANSPORT_H_
#define GOOGLEAPIS_TRANSPORT_JSON_PLAYBACK_TRANSPORT_H_

#include <memory>

#include "googleapis/client/transport/http_transport.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace util {
class Status;
}  // namespace util

namespace client {
class DataReader;
class JsonPlaybackTranscript;
class HttpScribeCensor;

/*
 * A fake transport that plays back from a JSON scribed transcript.
 * @ingroup TransportLayerTesting
 *
 * This transport implementation can be used like any other, but it does
 * not connect to a real backend. Instead it acts as a _fake_ and will
 * attempt to pair requests with those in JSON transcript and use the
 * transcript to complete the request with a response or transport error.
 *
 * This is primarily intended to facilitate testing the application layer
 * without requiring the overhead, latency, and potential nondeterminissim
 * of using real-world servers. Plus you can hand code the transcript in whole
 * or part to inject errors or certain scenarios that might be difficult to
 * produce on demand for a test.
 *
 * To produce the transcript, you can inject a JsonScript into another
 * transport (such as CurlHttpTransport) and make the requests you wish to
 * playback later using this transport.
 *
 * To playback the transport use an instance of this class, or the
 * JsonPlaybackTransportFactory. For example if can set the default global
 * transport factory to a JsonPlaybackTransportFactory. Or you can give
 * a particular object (such as a ClientService instance) a
 * JsonPlaybackTransport. Load the JSON transcript into the trasnport (or
 * factory) then proceed as normal.
 *
 * @warning
 * For best recall and fidelity, you should set the HttpCensor on this
 * transport to one configured the same way as the JsonScribe when you
 * recorded the transcript. Otherwise the uncensored requests you are making
 * will not match the censored data in the transcript.
 *
 * @see JsonScribe
 */
class JsonPlaybackTransport : public HttpTransport {
 public:
  /*
   * Constructor with standard transport options.
   *
   * @param[in] options The standard options to configure the transport with.
   */
  explicit JsonPlaybackTransport(const HttpTransportOptions& options);

  /*
   * Standard destructor
   */
  virtual ~JsonPlaybackTransport();

  /*
   * Implements the request factory method required by HttpTransport.
   *
   * @param[in] method The HTTP method to use for the request.
   *
   * @returns A new empty request that will use this fake transport when
   *          executing. Use this request as if it were real.
   */
  virtual HttpRequest* NewHttpRequest(const HttpRequest::HttpMethod& method);

  /*
   * Loads the transcript from the reader.
   *
   * Alternatively you could use set_transport if you are sharing a transcript
   * across multiple transport instances.
   *
   * This method is not thread-safe, but you should only be performing it
   * once before you start executing messages.
   *
   * The transport instance will keep ownership of this transport instance
   * util another is loaded or the instance is deleted.
   *
   * @param[in] reader The reader to load the transcript from.
   * @return Whether the transcript could be loaded successfully.
   *
   * @see set_transport
   */
  googleapis::util::Status LoadTranscript(DataReader* reader);

  /*
   * Sets the current transcript to the one provdied.
   *
   * This will override the transcript that was loaded
   * and owned by this instance.
   *
   * The transcript is thread-safe so can be shared across multiple
   * instaneces acting as a global transcript from a message stream
   * aggregated across all the transports sharing it.
   *
   * @param[in] t The caller maintains ownership of the transport.
   */
  void set_transcript(JsonPlaybackTranscript* t) { transcript_ = t; }

  /*
   * Returns the current transcript.
   *
   * @return The transcript may be the private one loaded into this instance
   *         or might be a shared one that was explicitly set. NULL is returned
   *         if there is no transcript bound yet.
   */
  JsonPlaybackTranscript* transcript() const  { return transcript_; }

  /*
   * Sets the censor to use when resolving requests.
   *
   * It is important that this censors requests consistent to how they
   * were censored in the transcript or else the requests will not resolve
   * properly.
   *
   * @param[in] censor The caller retains ownership.
   */
  void set_censor(HttpScribeCensor* censor)  { censor_ = censor; }

  /*
   * Returns the censor, if any.
   *
   * @return NULL if there is no censor.
   */
  HttpScribeCensor* censor() const  { return censor_; }

  /*
   * The default id() attribute value identifying curl transport instances.
   */
  static const char kTransportIdentifier[];

 private:
  std::unique_ptr<JsonPlaybackTranscript> transcript_storage_;
  JsonPlaybackTranscript* transcript_;
  HttpScribeCensor* censor_;
  DISALLOW_COPY_AND_ASSIGN(JsonPlaybackTransport);
};

/*
 * A transport factory for creating JsonPlaybackTransport.
 * @ingroup TransportLayerTesting
 *
 * This is a standard HttpTransportFactory that can be used in place of
 * any other.
 *
 * You will need to configure it by loading a transcript and binding an
 * HttpScribeCensor. See the JsonPlaybackTransport for details regarding
 * the censor to use here for best results.
 *
 * @warning
 * The factory must remain valid over the lifetime of the instances it
 * creates because it owns the censor and transcript that those instances
 * are using. If you replace the censor and transcript on the instances
 * then you can destroy the fatory while those instances are still in use.
 */
class JsonPlaybackTransportFactory : public HttpTransportFactory {
 public:
  /*
   * The default consructor will use the default transport options.
   */
  JsonPlaybackTransportFactory();

  /*
   * Standard constructor.
   */
  explicit JsonPlaybackTransportFactory(
      const HttpTransportLayerConfig* config);

  /*
   * Standard destructor.
   */
  virtual ~JsonPlaybackTransportFactory();

  /*
   * Loads the transcript to be shared among all instances created.
   *
   * The factory will maintain ownership of this
   * @param[in] reader The reader containing the data output by a JsonScribe.
   * @return Whether the transcript could be loaded or not.
   */
  googleapis::util::Status LoadTranscript(DataReader* reader);

  /*
   * Changes the censor used by this factory.
   *
   * @param censor Ownership is passed to the factory.
   *
   * @warning
   * The factory owns the censor given to instances it creates so this
   * method will invalidate the censor used by any outstanding instances unless
   * they had been explicitly replaced.
   */
  void ResetCensor(HttpScribeCensor* censor);

  /*
   * Returns the censor currently owned by the factory.
   */
  HttpScribeCensor* censor() { return censor_.get(); }

 protected:
  /*
   * Creates a new JsonPlaybackTransport instance configured with the
   * playback attributes specified in this factory.
   */
  virtual HttpTransport* DoAlloc(const HttpTransportOptions& options);

 private:
  std::unique_ptr<JsonPlaybackTranscript> transcript_;
  std::unique_ptr<HttpScribeCensor> censor_;
  DISALLOW_COPY_AND_ASSIGN(JsonPlaybackTransportFactory);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_JSON_PLAYBACK_TRANSPORT_H_

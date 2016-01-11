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


#ifndef GOOGLEAPIS_TRANSPORT_HTML_SCRIBE_H_
#define GOOGLEAPIS_TRANSPORT_HTML_SCRIBE_H_

#include <memory>
#include <string>
using std::string;

#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/base/thread_annotations.h"
#include "googleapis/base/integral_types.h"
namespace googleapis {

namespace client {
class DataWriter;
class HttpRequest;

/*
 * Specialized HttpScribe that produces HTML transcripts.
 * @ingroup TransportLayerTesting
 *
 * The PresentationFlags enumeration allows you to control the structure of
 * the HTML produced. Depending on how you plan on browsing the HTML, the
 * choice can make it easier or harder due to the abstractions they control.
 *
 * If you want to copy and paste sequences of requests then you might want to
 * turn the all off. If you want to browse the sequence and only look at
 * header or playload details then you should just set those flags.
 *
 * You can use the base class scribe's max_snippet attribute to limit how
 * much request/response data you store for each request. Since everything
 * is going to be journaled into a single HTML document, this could be a good
 * idea if you are performing large media transfers!
 *
 * The implementation of this class may stream directly to the writer, in
 * which case it may not be well-formed HTML if it did not finish properly
 * (e.g. it is still scribing or the process crashed). If that is the case,
 * you may need to append the closing tags to make it well formed if you
 * need to have well formed HTML.
 */
class HtmlScribe : public HttpEntryScribe {
 public:
  /*
   * Constructor.
   *
   * @param[in] censor The censor to use for scrubbing sensitive data.
   *                   The caller passes ownership.
   * @param[in] title For the HTML document title.
   * @param[in] writer Ownership is passed to the scribe. This writer will
   *            store the transcript.
   */
  HtmlScribe(
      HttpScribeCensor* censor, const string& title, DataWriter* writer);

  /*
   * Standard destructor.
   *
   * This will finish out the HTML and flush the writer to make it a
   * well-formed document.
   */
  virtual ~HtmlScribe();

  /*
   * Flushes the writer, but does not "finish out" the HTML to make
   * it well formed.
   */
  virtual void Checkpoint();

  enum PresentationFlags {
    EXPANDABLE_REQUEST = 0x1,
    EXPANDABLE_HEADERS = 0x2,
    EXPANDABLE_REQUEST_CONTENT = 0x4,
    EXPANDABLE_RESPONSE_BODY = 0x8,
    COLORIZE = 0x10,
    ALL = EXPANDABLE_REQUEST
          | EXPANDABLE_HEADERS
          | EXPANDABLE_REQUEST_CONTENT
          | EXPANDABLE_RESPONSE_BODY
          | COLORIZE
  };

  /*
   * Controls features in HTML output.
   *
   * @param[in] flags Bitwise-or of PresentationFlags enum values.
   */
  void set_presentation_flags(int flags) { presentation_ = flags; }

  /*
   * Returns the presentation flags.
   *
   * @return bitwise or value of individual enum values.
   *
   * @see PresentationFlags
   */
  int presentation_flags() const { return presentation_; }

  /*
   * Returns true if requests are expandable in the HTML.
   */
  bool expand_request() const {
    return presentation_ & EXPANDABLE_REQUEST;
  }

  /*
   * Returns true if headers are expandable in the HTML.
   */
  bool expand_headers() const  {
    return presentation_ & EXPANDABLE_HEADERS;
  }

  /*
   * Returns true if the request content is expandable in the HTML.
   */
  bool expand_request_content() const  {
    return presentation_ & EXPANDABLE_REQUEST_CONTENT;
  }

  /*
   * Returns true if the response body is expandable in the HTML.
   */
  bool expand_response_body() const {
    return presentation_ & EXPANDABLE_RESPONSE_BODY;
  }

 protected:
  /*
   * Returns an entry that produces the individual HTML transcript for
   * the request.
   */
  virtual Entry* NewEntry(const HttpRequest* request);
  virtual Entry* NewBatchEntry(const HttpRequestBatch* batch);

 private:
  int64 sequence_number_;
  std::unique_ptr<DataWriter> writer_;
  string last_netloc_;
  int presentation_;
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_HTML_SCRIBE_H_

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


#include <iostream>
using std::cout;
using std::endl;
using std::ostream;
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/html_scribe.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/strings/ascii_ctype.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {
namespace {

#ifndef _WIN32
using std::snprintf;
#endif

const StringPiece kToggleControl("");
const StringPiece kBinarySymbol(".");

string Magnitude(int64 value, int64 base) {
  int64 ten_times = value / (base / 10);
  return StrCat(ten_times / 10, ".", ten_times % 10);
}

template<typename INT_TYPE>
string ToHex(INT_TYPE x) {
  std::stringstream temp;
  temp << std::hex << x;
  return temp.str();
}

void EscapeAndAppendString(const StringPiece& from, string* out) {
  const char* end = from.data() + from.size();
  for (const char *data = from.data(); data < end; ++data) {
    switch (*data) {
      case '<': out->append("&lt;"); break;
      case '>': out->append("&gt;"); break;
      case '\'': out->append("&apos;"); break;
      case '\"': out->append("&quot;"); break;
      case '&': out->append("&amp;"); break;
      default:
        if (!ascii_isascii(*data)) {
          StrAppend(out, kBinarySymbol);
        } else {
          out->push_back(*data);
        }
    }
  }
}

void InitializeHtml(const string& title, DataWriter* writer) {
  const StringPiece javascript =
      "<script type='text/javascript'>\n"
      "function toggle_visibility(id) {\n"
      "  var e = document.getElementById(id);\n"
      "  if (e.style.display == 'block')\n"
      "    e.style.display = 'none';\n"
      "  else\n"
      "    e.style.display = 'block';\n"
      " }\n"
      "</script>\n";

  const StringPiece css =
      "<style>\n"
      " body { font-size:10pt }\n"
      " table { font-size:8pt;border-width:none;"
      "border-spacing:0px;border-color:#F8F8F8;border-style:solid }\n"
      " th, td { padding:2px;vertical-align:top;"
      "border-width:1px;border-color:#F8F8F8;border-style:solid; }\n"
      " th { font-weight:bold;text-align:left;font-family:times;"
      "background:#F8F8F8;color:#202020 }\n"
      " td { color:#000000; background-color:#FFFFFF }\n"
      " td.meta, th.meta { background-color:#F8F8F8 }\n"
      " td.request, th.request { background-color:#FEFEFE }\n"
      " td.response_err, th.response_err { background-color:#FF99CC }\n"
      " td.response_ok, th.response_ok { background-color:#00FF99 }\n"
      " a, a.toggle:link, a.toggle:visited { "
      "background-color:#FFFFFF;color:#000099 }\n"
      " a.toggle:hover, a.toggle:active { "
      "color:#FFFFFF;background-color:#000099 }\n"
      " div { display:none;margin-left:1em; }\n"
      " div.data { font-family:monospace;font-size:8pt;"
      "background-color:#FFFFCC }\n"
      " *.error { background-color:#FFEEEE; color:#990033 }\n"
      "</style>\n";

  string html;
  EscapeAndAppendString(title, &html);
  writer->Write(
      StrCat("<html><head>", javascript, css,
             "<title>", html, "</title>",
             "</head><body>")).IgnoreError();
}

// An html entry contains separate HTML strings for the request information and
// for batch abstraction if it is a batch request. If there is a batch request
// then the HTML request will render inside it as an attribute.
class HtmlEntry : public HttpEntryScribe::Entry {
 public:
  // Start the request HTML by opening a toggle table so the whole thing
  // can collapse down. We'll close the table at the end when we write out
  // the final HTML.
  void InitRequestHtml(HtmlScribe* scribe) {
    bool censored;
    string url = scribe->censor()->GetCensoredUrl(*request(), &censored);
    ParsedUrl parsed_url(url);
    AppendStartToggleTable(
        &request_html_, scribe->expand_request(), request_id_, "request",
        StrCat(parsed_url.netloc(), parsed_url.path()));
    StrAppend(&request_html_, "<tr><th class='meta'>Time<td class='meta'>",
              DateTime(timeval()).ToString());
    AppendRequest(&request_html_, scribe, request(), url, request_id_);
  }

  // Start the batch HTML by rendering all the individual requests.
  // We'll also render a toggle table start but keep it separate. We'll
  // use it at the end when we write the results out so that we can inject
  // the request HTML as the initial attribute under the toggle, followed
  // by the batch_html we start here.
  void InitBatchHtml(HtmlScribe* scribe) {
    bool censored;
    string url = scribe->censor()->GetCensoredUrl(*request(), &censored);
    ParsedUrl parsed_url(url);
    AppendStartToggleTable(
        &begin_batch_html_, scribe->expand_request(), batch_id_, "request",
        StrCat("Batch to ", parsed_url.netloc(), parsed_url.path()));
  }

  HtmlEntry(
      HtmlScribe* scribe,
      const HttpRequest* request,
      DataWriter* writer,
      int64 id)
      : HttpEntryScribe::Entry(scribe, request),
        scribe_(scribe), writer_(writer),
        request_id_(ToHex(id)),
        title_code_("UNK") {
    InitRequestHtml(scribe);
  }

  HtmlEntry(
      HtmlScribe* scribe,
      const HttpRequestBatch* batch,
      DataWriter* writer,
      int64 id)
      : HttpEntryScribe::Entry(scribe, batch),
        scribe_(scribe), writer_(writer),
        request_id_(ToHex(id)),
        batch_id_(StrCat("b", ToHex(id))),  // just to be distinct
        title_code_("UNK") {
    InitRequestHtml(scribe);
    InitBatchHtml(scribe);
  }

  ~HtmlEntry() {}

  virtual void FlushAndDestroy() {
    StringPiece begin_span;
    StringPiece end_span;
    if (scribe_->presentation_flags() & HtmlScribe::COLORIZE) {
      HttpResponse* response = request()->response();
      if (response && (!response->http_code() || !response->ok())) {
        begin_span = "<span class='error'>";
        end_span = "</span>";
      }
    }
    // Finish the toggle table we started in the Init method.
    AppendEndToggleTable(&request_html_);

    // Start the entry with the HTTP response code
    writer_->Write(StrCat(begin_span, title_code_, end_span, " "))
        .IgnoreError();

    // If this is a batch request, start the batch toggle over this whole entry
    if (is_batch()) {
      const char* detail_heading =
          "<tr><td colspan='2'><b>HTTP Request Detail</b><br/>\n";
      writer_->Write(begin_batch_html_).IgnoreError();
      writer_->Write(detail_heading).IgnoreError();
    }

    // Write the request HTML. If this is batch then it will be at the start.
    // Since this has a toggle, the attribute should be toggleable within the
    // batch. Otherwise it is the entire entry.
    writer_->Write(request_html_).IgnoreError();

    if (is_batch()) {
      AppendEndToggleTable(&batch_html_);
      writer_->Write(batch_html_).IgnoreError();
    }
    delete this;
  }

  static void AppendRequest(
      string* html, HtmlScribe* scribe,
      const HttpRequest* request,
      const string& url, const string& id) {
    bool censored;
    StrAppend(html, "<tr><th class='meta'>Method<td class='meta'>",
              request->http_method());
    StrAppend(html, "<tr><th class='meta'>URL<td class='meta'>", url);
    StrAppend(html, "<tr><td colspan='2'>");
    AppendRequestHeaders(html, scribe, request, true, StrCat("H", id));
    StrAppend(html, "<tr><td colspan='2'>");

    int64 original_size;
    string payload_title;
    string snippet;

    snippet = scribe->censor()->GetCensoredRequestContent(
        *request, scribe->max_snippet(), &original_size,  &censored);
    payload_title = StrCat(censored ? "Censored " : "", "Content Payload");

    AppendPayloadData(
        html,
        scribe->expand_request_content(),
        StrCat("C", id),
        payload_title,
        original_size,
        true,
        snippet);
  }

  virtual void Sent(const HttpRequest* request) {
    StrAppend(&request_html_, "<tr><td>",
              TimeOffsetToString(MicrosElapsed()), "<td>sent\n");
  }
  virtual void SentBatch(const HttpRequestBatch* batch) {
    // log with request
  }

  void Received(const HttpRequest* request) {
    HttpResponse* response = request->response();
    title_code_ = StrCat("", response->http_code());
    StrAppend(&request_html_, "<tr><td>",
              TimeOffsetToString(MicrosElapsed()),
              "<td>received HTTP ",
              response->http_code(), "\n");
    AppendResponse(&request_html_, scribe_, request, request_id_);
  }

  void ReceivedBatch(const HttpRequestBatch* batch) {
    AppendResponseBatch(&batch_html_, scribe_, batch, batch_id_);
  }

  static void AppendResponse(
      string* html, HtmlScribe* scribe,
      const HttpRequest* request, const string& id) {
    StrAppend(html, "<tr><td colspan='2'>");
    AppendResponseHeaders(html, scribe, request, true, StrCat("Z", id));
    StrAppend(html, "<tr><td colspan='2'>");

    bool censored;
    int64 original_size;
    string payload_title;
    string snippet;

    snippet = scribe->censor()->GetCensoredResponseBody(
        *request, scribe->max_snippet(), &original_size, &censored);
    payload_title = StrCat(censored ? "Censored " : "", "Response Body");

    AppendPayloadData(
        html,
        scribe->expand_response_body(),
        StrCat("B", id),
        payload_title,
        original_size,
        true,
        snippet);
  }

  static void AppendResponseBatch(
      string* html, HtmlScribe* scribe,
      const HttpRequestBatch* batch, const string& id) {
    string snippet = BuildRequestBatchDetail(scribe, batch, id);
    StrAppend(html, "<tr><td colspan='2'>");
    AppendPayloadDataWithoutSize(
        html,
        scribe->expand_response_body(),
        StrCat("B", id),
        "Batched Requests",
        false,
        snippet);
  }

  void Failed(const HttpRequest* request, const googleapis::util::Status& status) {
    StrAppend(&request_html_,
              "<tr><td class='error'>",
              TimeOffsetToString(MicrosElapsed()),
              "<td class='error'>failure: ");
    EscapeAndAppendString(status.error_message(), &request_html_);
    title_code_ = "Err";
  }

  void FailedBatch(const HttpRequestBatch* batch, const googleapis::util::Status& status) {
    string snippet = BuildRequestBatchDetail(scribe_, batch, batch_id_);
    StrAppend(&batch_html_, "<tr><td colspan=2>", snippet);
    StrAppend(&batch_html_,
              "<tr><td class='error'>",
              TimeOffsetToString(MicrosElapsed()),
              "<td class='error'>failure: ");
    EscapeAndAppendString(status.error_message(), &batch_html_);
    title_code_ = "Err";
  }

 private:
  static void AppendStartToggleTable(
      string* html,
      bool can_toggle,
      const StringPiece& id,
      const StringPiece& css,
      const StringPiece& title) {
    // No href. We are just using it as a toggle with mouse over styles.
    string display;
    if (can_toggle) {
      StrAppend(html,
                "<a class='toggle' onclick='toggle_visibility(\"",
                id, "\");'>");
      StrAppend(html, kToggleControl, title);
      StrAppend(html, "</a><br/>");
    } else {
      display = " style='display:block'";
      StrAppend(html, "<b>", title, "</b><br/>\n");
    }
    StrAppend(html, "<div class='", css, "' id='", id, "'", display, ">");
    StrAppend(html, "<table>\n");
  }

  static void AppendEndToggleTable(string* html) {
    StrAppend(html, "</table></div>\n");
  }

  static void AppendRequestHeaders(
      string* html,
      HtmlScribe* scribe,
      const HttpRequest* request,
      bool respect_restrictions,
      const StringPiece& id) {
    if (respect_restrictions &&
        (request->scribe_restrictions()
         & HttpScribe::FLAG_NO_REQUEST_HEADERS)) {
      html->append("<i>Request is hiding request headers</i><br/>\n");
      return;
    }

    const HttpHeaderMap& headers = request->headers();
    const StringPiece css = "request";
    AppendStartToggleTable(
        html, scribe->expand_headers(),
        id, css, StrCat(headers.size(), " Request Headers"));
    for (HttpHeaderMap::const_iterator
             it = headers.begin();
         it != headers.end();
         ++it) {
      bool censored;
      StrAppend(html, "\n<tr><th>");
      EscapeAndAppendString(it->first, html);
      StrAppend(html, "<td>");
      EscapeAndAppendString(
          scribe->censor()->GetCensoredRequestHeaderValue(
              *request, it->first, it->second, &censored),
          html);
      if (censored) {
        html->append(" <i>(censored)</i>");
      }
    }
    AppendEndToggleTable(html);
  }

  static void AppendResponseHeaders(
      string* html, HtmlScribe* scribe,
      const HttpRequest* request,
      bool respect_restrictions,
      const StringPiece& id) {
    if (respect_restrictions &&
        (request->scribe_restrictions()
         & HttpScribe::FLAG_NO_RESPONSE_HEADERS)) {
      html->append("<i>Request is hiding response headers</i><br/>\n");
      return;
    }
    StringPiece css =
        request->response()->ok() ? "response_ok" : "response_err";
    const HttpHeaderMultiMap& headers = request->response()->headers();
    AppendStartToggleTable(
        html,
        scribe->expand_headers(),
        id, css, StrCat(headers.size(), " Response Headers"));
    for (HttpHeaderMultiMap::const_iterator it =
             headers.begin();
         it != headers.end();
         ++it) {
      StrAppend(html, "\n<tr><th>");
      EscapeAndAppendString(it->first, html);
      StrAppend(html, "<td>");
      EscapeAndAppendString(it->second, html);
    }
    AppendEndToggleTable(html);
  }

  static void AppendPayloadData(string* html,
                                bool can_toggle,
                                const StringPiece& id,
                                const StringPiece& thing_name,
                                int64 original_size,
                                bool escape_snippet,
                                const StringPiece& snippet) {
    if (original_size == 0) {
      StrAppend(html, "<i>Empty ", thing_name, "</i><br/>\n");
      return;
    }

    string payload_size;
    const int64 kKiB = 1 * 1000;
    const int64 kMiB = kKiB * 1000;
    const int64 kGiB = kMiB * 1000;

    if (original_size < 0) {
      payload_size = "UNKNOWN";
    } else if (original_size < kKiB) {
      payload_size = StrCat(original_size, "b");
    } else if (original_size < kMiB) {
      payload_size = StrCat(Magnitude(original_size, kKiB), "kiB");
    } else if (original_size < kGiB) {
      payload_size = StrCat(Magnitude(original_size, kMiB), "MiB");
    } else {
      payload_size = StrCat(Magnitude(original_size, kGiB), "GiB");
    }
    if (snippet.empty()) {
      StrAppend(html,
                "<i>Stripped all ", payload_size, " from ", thing_name,
                "</i><br/>\n");
      return;
    }

    AppendPayloadDataWithoutSize(
        html, can_toggle, id, StrCat(payload_size, " ", thing_name),
        escape_snippet, snippet);
  }

  static void AppendPayloadDataWithoutSize(
      string* html,
      bool can_toggle,
      const StringPiece& id,
      const StringPiece& title,
      bool escape_snippet,
      const StringPiece& snippet) {
    // No href. We are just using it as a toggle with mouse over styles.
    string display;
    if (can_toggle) {
      StrAppend(html,
                "<a class='toggle' onclick='toggle_visibility(\"",
                id, "\");'>", kToggleControl, title, "</a><br/>\n");
    } else {
      display = " style='display:block'";
      StrAppend(html, "<b>", title, "</b>\n");
    }
    StrAppend(html, "<div id=\"", id, "\" class='data'", display, ">\n");
    if (escape_snippet) {
      EscapeAndAppendString(snippet, html);
    } else {
      StrAppend(html, snippet);
    }
    StrAppend(html, "</div>\n");
  }

  static string TimeOffsetToString(int64 delta_us) {
    DateTime now;
    struct timeval now_timeval;
    now.GetTimeval(&now_timeval);

    double secs = delta_us * 0.0000001;
    char tmp[30];
    if (secs >= 1.0) {
      // big values in s precision.
      snprintf(tmp, sizeof(tmp), "%.1fs", secs);
      return tmp;
    } else if (secs >= 0.1) {
      // small values in ms precision.
      snprintf(tmp, sizeof(tmp), "%.3fs", secs);
      return tmp;
    } else {
      // tiny values in us precision.
      snprintf(tmp, sizeof(tmp), "%.6fs", secs);
      return tmp;
    }
  }

  static string BuildRequestBatchDetail(
      HtmlScribe* scribe,
      const HttpRequestBatch* batch,
      const string& id) {
    const HttpRequestBatch::BatchedRequestList& list = batch->requests();
    string html = "<table>\n";
    int i = 0;
    for (HttpRequestBatch::BatchedRequestList::const_iterator it
             = list.begin();
         it != list.end();
         ++it, ++i) {
      const string sub_id = StrCat(id, ".", i);
      const HttpRequest* sub_request = *it;
      bool censored;
      string url = scribe->censor()->GetCensoredUrl(*sub_request, &censored);
      ParsedUrl parsed_url(url);
      html.append("<tr><td colspan=2>");
      AppendStartToggleTable(
          &html, scribe->expand_request(), sub_id, "request",
          StrCat("# ", i, ": ", parsed_url.netloc(), parsed_url.path()));
      AppendRequest(&html, scribe, sub_request, url, sub_id);
      AppendResponse(&html, scribe, sub_request, sub_id);
      AppendEndToggleTable(&html);
      html.append("\n");
    }
    html.append("</table>\n");
    return html;
  }

  HtmlScribe* scribe_;
  DataWriter* writer_;
  string request_id_;
  string request_html_;
  string batch_id_;
  string batch_html_;
  string begin_batch_html_;
  string title_code_;
};

}  // anonymous namespace

HtmlScribe::HtmlScribe(
    HttpScribeCensor* censor, const string& title, DataWriter* writer)
    : HttpEntryScribe(censor), sequence_number_(0), writer_(writer),
      presentation_(EXPANDABLE_REQUEST | COLORIZE) {
  InitializeHtml(title, writer);
  writer->Write(StrCat("Starting at ", DateTime().ToString(), "<br/>\n"))
      .IgnoreError();
}

HtmlScribe::~HtmlScribe() {
  writer_->Write(StrCat("<br/>Finished at ", DateTime().ToString()))
      .IgnoreError();
  DiscardQueue();
  writer_->Write(StrCat("</body></html>\n")).IgnoreError();
}

HttpEntryScribe::Entry* HtmlScribe::NewEntry(const HttpRequest* request) {
  ++sequence_number_;
  return new HtmlEntry(this, request, writer_.get(), sequence_number_);
}

HttpEntryScribe::Entry* HtmlScribe::NewBatchEntry(
    const HttpRequestBatch* batch) {
  ++sequence_number_;
  return new HtmlEntry(this, batch, writer_.get(), sequence_number_);
}

void HtmlScribe::Checkpoint() {
  LOG(WARNING) << "HTML checkpointing is not implemented";
}

}  // namespace client

}  // namespace googleapis

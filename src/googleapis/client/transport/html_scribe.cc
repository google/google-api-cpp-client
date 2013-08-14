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


#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/html_scribe.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/strings/ascii_ctype.h"
#include "googleapis/strings/numbers.h"
#include "googleapis/base/stringprintf.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace {

using client::DataReader;
using client::DataWriter;
using client::DateTime;
using client::HtmlScribe;
using client::HttpEntryScribe;
using client::HttpHeaderMap;
using client::HttpHeaderMultiMap;
using client::HttpRequest;
using client::HttpResponse;
using client::ParsedUrl;

const StringPiece kToggleControl("");
const StringPiece kBinarySymbol(".");

string Magnitude(int64 value, int64 base) {
  int64 ten_times = value / (base / 10);
  return StrCat(ten_times / 10, ".", ten_times % 10);
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

void InitializeHtml(const StringPiece& title, DataWriter* writer) {
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
      " td { color:#000000; }\n"
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

class HtmlEntry : public HttpEntryScribe::Entry {
 public:
  HtmlEntry(
      HtmlScribe* scribe,
      const HttpRequest* request,
      DataWriter* writer,
      int64 id)
      : HttpEntryScribe::Entry(scribe, request),
        scribe_(scribe), writer_(writer), id_(id),
        title_code_("UNK") {
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
    writer_->Write(StrCat(begin_span, title_code(), end_span, " "))
        .IgnoreError();
    AppendEndToggleTable();
    writer_->Write(html_).IgnoreError();
    delete this;
  }

  void ConstructRequestHtml() {
    bool censored;
    const HttpRequest* request = this->request();
    string url = scribe_->censor()->GetCensoredUrl(*request, &censored);
    ParsedUrl parsed_url(url);
    AppendStartToggleTable(
        scribe_->expand_request(),
        SimpleItoa(id_), "request",
        StrCat(parsed_url.netloc(), parsed_url.path()));
    StrAppend(&html_, "<tr><th class='meta'>Time<td class='meta'>",
              DateTime(timeval()).ToString());
    StrAppend(&html_, "<tr><th class='meta'>Method<td class='meta'>",
              request->http_method());
    StrAppend(&html_, "<tr><th class='meta'>URL<td class='meta'>",
              parsed_url.url());
    StrAppend(&html_, "<tr><td colspan='2'>");
    AppendRequestHeaders(request, StrCat("H", id_));
    StrAppend(&html_, "<tr><td colspan='2'>");

    int64 original_size;
    string censored_decorator;
    string snippet = scribe_->censor()->GetCensoredRequestContent(
        *request, scribe_->max_snippet(), &original_size,  &censored);
    if (censored) {
      censored_decorator = "Censored ";
    }

    AppendPayloadData(
        scribe_->expand_request_content(),
        StrCat("C", id_),
        StrCat(censored_decorator, "Content Payload"),
        original_size,
        snippet);
  }

  virtual void Sent(const HttpRequest* request) {
    ConstructRequestHtml();
    StrAppend(&html_, "<tr><td>", TimeOffset(), "<td>sent\n");
  }

  void Received(const HttpRequest* request) {
    HttpResponse* response = request->response();
    title_code_ = StrCat("", response->http_code());
    StrAppend(&html_, "<tr><td>", TimeOffset(), "<td>received HTTP ",
              response->http_code(), "\n");
    StrAppend(&html_, "<tr><td colspan='2'>");
    AppendResponseHeaders(request, StrCat("Z", id_));
    StrAppend(&html_, "<tr><td colspan='2'>");

    bool censored;
    int64 original_size;
    string censored_decorator;
    string snippet = scribe_->censor()->GetCensoredResponseBody(
        *request, scribe_->max_snippet(), &original_size, &censored);
    if (censored) {
      censored_decorator = "Censored ";
    }

    AppendPayloadData(
        scribe_->expand_response_body(),
        StrCat("B", id_),
        StrCat(censored_decorator, "Response Body"),
        original_size,
        snippet);
  }

  void Failed(const HttpRequest*, const util::Status& status) {
    if (html_.empty()) {
      ConstructRequestHtml();
    }
    StrAppend(&html_,
              "<tr><td class='error'>", TimeOffset(),
              "<td class='error'>failure: ");
    EscapeAndAppendString(status.error_message(), &html_);
    title_code_ = "Err";
  }

  const string& title_code() const  { return title_code_; }
  const string& html() const        { return html_; }

 private:
  void AppendStartToggleTable(
      bool can_toggle,
      const StringPiece& id,
      const StringPiece& css,
      const StringPiece& title) {
    // No href. We are just using it as a toggle with mouse over styles.
    string display;
    if (can_toggle) {
      StrAppend(&html_,
                "<a class='toggle' onclick='toggle_visibility(\"",
                id, "\");'>");
      StrAppend(&html_, kToggleControl, title);
      StrAppend(&html_, "</a><br/>");
    } else {
      display = " style='display:block'";
      StrAppend(&html_, "<b>", title, "</b><br/>\n");
    }
    StrAppend(&html_, "<div class='", css, "' id='", id, "'", display, ">");
    StrAppend(&html_, "<table>\n");
  }

  void AppendEndToggleTable() {
    StrAppend(&html_, "</table></div>\n");
  }

  void AppendRequestHeaders(
      const HttpRequest* request, const StringPiece& id) {
    const HttpHeaderMap& headers = request->headers();
    const StringPiece css = "request";
    AppendStartToggleTable(
        scribe_->expand_headers(),
        id, css, StrCat(headers.size(), " Request Headers"));
    for (HttpHeaderMap::const_iterator
             it = headers.begin();
         it != headers.end();
         ++it) {
      bool censored;
      StrAppend(&html_, "\n<tr><th>");
      EscapeAndAppendString(it->first, &html_);
      StrAppend(&html_, "<td>");
      EscapeAndAppendString(
          scribe_->censor()->GetCensoredRequestHeaderValue(
              *request, it->first, it->second, &censored),
          &html_);
      if (censored) {
        html_.append(" <i>(censored)</i>");
      }
    }
    AppendEndToggleTable();
  }

  void AppendResponseHeaders(
      const HttpRequest* request, const StringPiece& id) {
    StringPiece css =
        request->response()->ok() ? "response_ok" : "response_err";
    const HttpHeaderMultiMap& headers = request->response()->headers();
    AppendStartToggleTable(
        scribe_->expand_headers(),
        id, css, StrCat(headers.size(), " Response Headers"));
    for (HttpHeaderMultiMap::const_iterator it =
           headers.begin();
         it != headers.end();
         ++it) {
      StrAppend(&html_, "\n<tr><th>");
      EscapeAndAppendString(it->first, &html_);
      StrAppend(&html_, "<td>");
      EscapeAndAppendString(it->second, &html_);
    }
    AppendEndToggleTable();
  }

  void AppendPayloadData(bool can_toggle,
                         const StringPiece& id,
                         const StringPiece& thing_name,
                         int64 original_size,
                         const StringPiece& snippet) {
    if (original_size == 0) {
      StrAppend(&html_, "<i>Empty ", thing_name, "</i><br/>\n");
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
      StrAppend(&html_,
                "<i>Stripped all ", payload_size, " from ", thing_name,
                "</i><br/>\n");
      return;
    }

    // No href. We are just using it as a toggle with mouse over styles.
    string display;
    if (can_toggle) {
      StrAppend(&html_,
                "<a class='toggle' onclick='toggle_visibility(\"",
                id, "\");'>");
      StrAppend(&html_,
                kToggleControl, payload_size, " ", thing_name,
                "</a><br/>\n");
    } else {
      display = " style='display:block'";
      StrAppend(&html_, "<b>", payload_size, " ", thing_name, "</b>\n");
    }
    StrAppend(&html_, "<div id=\"", id, "\" class='data'", display, ">\n");
    EscapeAndAppendString(snippet, &html_);
    StrAppend(&html_, "</div>\n");
  }

  string TimeOffset() {
    DateTime now;
    struct timeval now_timeval;
    now.GetTimeval(&now_timeval);
    int64 delta_us = MicrosElapsed();

    double secs = delta_us * 0.0000001;
    if (secs >= 1.0) {
      return StringPrintf("%.1fs", secs);  // big values in s precision.
    } else if (secs >= 0.1) {
      return StringPrintf("%.3fs", secs);  // small values in ms precision.
    } else {
      return StringPrintf("%.6fs", secs);  // tiny values in us precision.
    }
  }

  HtmlScribe* scribe_;
  DataWriter* writer_;
  int64 id_;
  string html_;
  string title_code_;
};

}  // anonymous namespace

namespace client {

HtmlScribe::HtmlScribe(
    HttpScribeCensor* censor, const StringPiece& title, DataWriter* writer)
    : HttpEntryScribe(censor), sequence_number_(0), writer_(writer),
      presentation_(EXPANDABLE_REQUEST | COLORIZE) {
  InitializeHtml(title, writer);
  writer->Write(StrCat("Starting at ", DateTime().ToString(), "<br/>"))
      .IgnoreError();
}

HtmlScribe::~HtmlScribe() {
  writer_->Write(StrCat("<br/>Finished at ", DateTime().ToString()))
      .IgnoreError();
  DiscardQueue();
  writer_->Write(StrCat("</body></html>\n")).IgnoreError();
}

HttpEntryScribe::Entry* HtmlScribe::NewEntry(const HttpRequest* request) {
  return new HtmlEntry(this, request, writer_.get(), ++sequence_number_);
}

void HtmlScribe::Checkpoint() {
  LOG(WARNING) << "HTML checkpointing is not implemented";
}

}  // namespace client

} // namespace googleapis
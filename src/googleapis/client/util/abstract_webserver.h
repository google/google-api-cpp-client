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
 * @defgroup PlatformLayerWebServer Platform Layer - Embedded Web Server
 *
 * The embedded webserver module is provided by the Plaform Layer rather
 * than the Trnasport Layer were you might otherwise expect it. This is
 * because we are not really embracing it as a core product feature. It is
 * only here to support providing interfaces to iteractt with embedded
 * HTTP servers and for writing tests.
 *
 * The request/response abstraction in this module is distinctly different
 * (and not compatible with) the HttpRequest interface core to the
 * Transport Layer. The transport layer is designed around the needs of
 * clients. The embedded web server is for servers. Because the focal point
 * of the SDK is strictly for clients, we are keeping the focus and viewpoints
 * more stictly separated.
 */

#ifndef GOOGLEAPIS_UTIL_ABSTRACT_WEBSERVER_H_
#define GOOGLEAPIS_UTIL_ABSTRACT_WEBSERVER_H_

#include <memory>
#include <string>
using std::string;
#include <utility>
#include <vector>

#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {

/*
 * Abstract class for responses to WebServerRequests into the
 * AbstractWebServer
 * @ingroup PlatformLayerWebServer
 *
 * This is different from the HttpResponse class in the transport layer
 * which are client-side responses. These are server side responses.
 *
 * Responses are owned and created by WebServerRequest.
 */
class WebServerResponse {
 public:
  WebServerResponse() {}
  virtual ~WebServerResponse() {}

  /*
   * Respond with a text/html content type and body.
   *
   * @param[in] http_code The HTTP status code to send.
   * @param[in] body The payload can be empty.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status SendHtml(int http_code, const string& body) {
    return SendReply("text/html", http_code, body);
  }

  /*
   * Respond with a text/plain content type and body.
   *
   * @param[in] http_code The HTTP status code to send.
   * @param[in] body The payload can be empty.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status SendText(int http_code, const string& body) {
    return SendReply("text/plain", http_code, body);
  }

  /*
   * Respond with a redirect to another url.
   *
   * @param[in] http_code The HTTP status code to send (e.g. 307).
   * @param[in] url The url to redirect to.
   *
   * @return ok or reason for failure.
   */
  virtual googleapis::util::Status SendRedirect(int http_code, const string& url);

  /*
   * Respond with an specified content type and body.
   *
   * @param[in] content_type The MIME content type of the body.
   * @param[in] http_code The HTTP status code to send.
   * @param[in] body The payload can be empty.
   *
   * @return ok or reason for failure.
   */
  virtual googleapis::util::Status SendReply(
      const string& content_type,
      int http_code,
      const string& body) = 0;

  /*
   * Adds a custom header to the repsonse.
   *
   * Content-Type, Content-Length and Location headers are automatically added.
   * This will not check the header names or values.
   *
   * @param[in] name The name of header to add.
   * @param[in] value The value of the header.
   */
  virtual googleapis::util::Status AddHeader(
      const string& name, const string& value) = 0;

  /*
   * Adds a custom cookie to the repsonse.
   *
   * This will not check the cookie names or values.
   *
   * @param[in] name The name of header to add.
   * @param[in] value The value of the header.
   */
  virtual googleapis::util::Status AddCookie(
      const string& name, const string& value) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebServerResponse);
};

/*
 * Abstract class for invocations into the AbstractWebServer
 * @ingroup PlatformLayerWebServer
 *
 * This is different from the HttpRequest class in the transport layer
 * which are client-side requests. These are server side requests.
 *
 * Requests are created by the AbstractWebServer when it receives an
 * invocation.
 */
class WebServerRequest {
 public:
  /*
   * Standard constructor.
   *
   * @param[in] method The HTTP method called (e.g. GET).
   * @param[in] url The url that was invoked.
   * @param[in] response_storage The repsonse object to bind to the request.
   */
  WebServerRequest(
      const string& method,
      const string& url,
      WebServerResponse* response_storage);

  /*
   * Standard destructor.
   */
  virtual ~WebServerRequest();

  const string& method() const { return method_; }
  const ParsedUrl& parsed_url() const { return parsed_url_; }
  WebServerResponse* response() const { return response_.get(); }

  virtual bool GetCookieValue(const char* key, string* value) const = 0;
  virtual bool GetHeaderValue(const char* key, string* value) const = 0;

 private:
  string method_;
  ParsedUrl parsed_url_;
  std::unique_ptr<WebServerResponse> response_;
};

/*
 * A minimal abstract interface for embedded webservers.
 * @ingroup PlatformLayerWebServer
 *
 * This is only an abstract interface. You must supply
 * your own implementation and use this class to adapt it. The interface
 * is only intended to provide some library code and sample code that integrate
 * with an embedded web server without explicitly depending on any particular
 * implementation.
 *
 * Note that this interface does not accomodate POST requests at this time,
 * but the library does not need it as a client -- this abstractionis not
 * intended to be used for implementing cloud services.
 */
class AbstractWebServer {
 public:
  /*
   * Used to register a callback on particular URIs or trees.
   *
   * @param[in] request for the request being processed.
   * @return ok or reason for failure.
   */
  typedef ResultCallback1< googleapis::util::Status, WebServerRequest*> PathHandler;

  /*
   * Constructs an http server on the given port
   *
   * @param[in] port Should be non-0.
   */
  explicit AbstractWebServer(int port);

  /*
   * Standard destructor.
   */
  virtual ~AbstractWebServer();

  /*
   * Returns the port bound in the constructor.
   */
  int port() const { return port_; }

  /*
   * Starts the server.
   *
   * @return ok or reason for error.
   */
  googleapis::util::Status Startup();

  /*
   * Stops the server.
   *
   * @return ok or reason for error.
   */
  void Shutdown();

  /*
   * Returns URL into this server for the given path.
   *
   * @param[in] use_localhost If true use 'localhost' rather than the hostname.
   * @param[in] path The path part of the url to build.
   */
  string MakeEndpointUrl(bool use_localhost, const string& path) const;

  /*
   * Inject handler for path.
   *
   * @param[in] path The path to intercept with this handler.
   * @param[in] handler A repeatable callback. Ownership is passed.
   *
   * This is called by the default DoHandleUrl method.
   */
  void AddPathHandler(const string& path, PathHandler* handler);

  /*
   * Looks up added PathHandler that matches path.
   *
   * Searches in the order they were added.
   *
   * @param[in] request The request to lookup.
   * @return path handler or NULL if one could not be found.
   */
  PathHandler* FindPathHandler(WebServerRequest* request) const;

  /*
   * Returns the protocol part of the url used by this webserver (e.g. 'https')
   */
  virtual string url_protocol() const;

 protected:
  virtual googleapis::util::Status DoStartup() = 0;
  virtual void DoShutdown() = 0;

  /*
   * Handles inbound request.
   *
   * The base class method looks up a registered path handler that matches
   * the url path prefix. It returns a 404 if one isnt found.
   *
   * @param[in] request The request from the web server.
   */
  virtual googleapis::util::Status DoHandleRequest(WebServerRequest* request);

 private:
  int port_;

  typedef std::pair<string, PathHandler*> Hook;
  std::vector<Hook> hooks_;

  DISALLOW_COPY_AND_ASSIGN(AbstractWebServer);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_ABSTRACT_WEBSERVER_H_

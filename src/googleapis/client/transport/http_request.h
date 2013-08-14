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


#ifndef APISERVING_CLIENTS_CPP_TRANSPORT_HTTP_REQUEST_H_
#define APISERVING_CLIENTS_CPP_TRANSPORT_HTTP_REQUEST_H_

#include <string>
using std::string;
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/scoped_ptr.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/util/status.h"
namespace googleapis {

namespace client {
class DataReader;
class HttpRequest;
class HttpResponse;
class HttpTransport;
class AuthorizationCredential;

/*
 * Denotes an HTTP request to be sent to an HTTP server.
 * @ingroup TransportLayerCore
 *
 * Requests are used to send messages to HTTP servers. They are not
 * created directly by consumer code, rather they are created by HttpTransport
 * instances on behalf of requests from consumer code. In practice, this class
 * will be subclassed by transport implementations as needed to store
 * additional private state that the tranaport implementation may need.
 * Consumer code shoudl not subclass requests, however transport
 * implementations are required to.
 *
 * This class is based on a Command Pattern for issueing HTTP requests
 * consistent with <a href='http://www.ietf.org/rfc/rfc2616.txt'>RFC 2616,
 * HTTP/1.1</a>. The request instance can be given the url to invoke,
 * along with the payload and any desired headers. The physical message
 * exchanges with the server happen when Execute() is called.
 *
 * This class is not strictly thread-safe in itself, however it is designed
 * such that it can be safely consumed in a multi-threaded environment.
 * The critical state that is not naturally thread-safe is managed in the
 * thread-safe HttpRequestState class.
 *
 * Although this class is abstract, it is recommended that consumer code
 * use only this class interface so as not to depend on any specific transport
 * implementation. Especially if the code will be compiled into libraries used
 * by multiple applications or will run on multiple platforms.
 *
 * @see HttpResponse
 * @see HttpTransport
 * @see HttpTransport::NewHttpRequest()
 */
class HttpRequest {
 public:
  /*
   * Methods are just a free-form StringPiece typedefed to clarify the API.
   *
   * Some HTTP servers may use extensions or define non-standard methods.
   * This type is a free-form StringPiece to accomodate those values.
   * It is suggested, but not required, you use the standard value constants
   * if you can.
   */
  typedef StringPiece HttpMethod;

  static const HttpMethod DELETE;  // RFC 2616 DELETE
  static const HttpMethod GET;     // RFC 2616 GET
  static const HttpMethod HEAD;    // RFC 2616 HEAD
  static const HttpMethod POST;    // RFC 2616 POST
  static const HttpMethod PUT;     // RFC 2616 PUT
  static const HttpMethod PATCH;   // RFC 2068 PATCH

  // Any content-type is valid. These are just symbolic names for ones
  // explicitly used within the client libraries.

  static const StringPiece ContentType_HTML;               // text/html
  static const StringPiece ContentType_JSON;               // application/json
  static const StringPiece ContentType_TEXT;               // text/plain
  static const StringPiece ContentType_MULTIPART_MIXED;    // multipart/mixed
  static const StringPiece ContentType_MULTIPART_RELATED;  // multipart/related

  // application/x-www-form-urlencoded
  static const StringPiece ContentType_FORM_URL_ENCODED;

  // Any header name is valid, these are just common ones used within
  // the api itself.
  static const StringPiece HttpHeader_AUTHORIZATION;   // Authorization
  static const StringPiece HttpHeader_CONTENT_LENGTH;  // Content-Length
  static const StringPiece HttpHeader_CONTENT_TYPE;    // Content-Type
  static const StringPiece HttpHeader_HOST;            // Host
  static const StringPiece HttpHeader_LOCATION;        // Location
  static const StringPiece HttpHeader_TRANSFER_ENCODING;  // Transfer-Encoding
  static const StringPiece HttpHeader_USER_AGENT;      // User-Agent

  /*
   * Standard destructor.
   *
   * The destructor is public for synchonous requests.
   * However, asynchronous requests should instead use DestroyWhenDone()
   * to avoid internal race conditions.
   */
  virtual ~HttpRequest();

  /*
   * A safer destructor for asynchronous requests.
   *
   * Destroys the request once it is done() and after notifications has been
   * called. if the requst has already finished, the the instance will be
   * destroyed immediately. Otherwise it will self-desruct once it is safe
   * to do so.
   */
  void DestroyWhenDone();

  /*
   * Clears the request data, but not the options.
   */
  virtual void Clear();

  /*
   * Gets a mutable options instance to configure instance-specific options.
   *
   * Options should not be changed once Execute() is called or they will
   * not take effect and can potentially confuse response processing.
   */
  HttpRequestOptions* mutable_options()     { return &options_; }

  /*
   * Gets request options.
   */
  const HttpRequestOptions& options() const { return options_; }

  /*
   * Gets request state instance containing additional attribute values.
   *
   * The request state contains the dynamic attributes requiring thread-safety.
   * These include things like status, http response code, and where in the
   * execution lifecycle the request currently is.
   */
  const HttpRequestState& state() const { return response_->request_state(); }

  /*
   * Returns the object managing the request's response.
   *
   * Although this method is on a const request, the response returned is not
   * const. That is because reading the body affects the stream pointer, which
   * changes its state.
   */
  HttpResponse* response() const   { return response_.get(); }

  /*
   * Returns the URL that this request will invoke when executed.
   */
  const string& url() const        { return url_; }

  /*
   * Sets the URL to invoke when the request is executed.
   *
   * @param[in] url the desired URL.
   */
  void set_url(const string& url)  { url_ = url; }

  /*
   * Adds a 'Content-Type' header with the given value.
   *
   * This replaces any existing 'Content-Type' header.
   * @param[in] type The new content type.
   *
   * @see RemoveHeader()
   */
  void set_content_type(const StringPiece& type) {
    AddHeader(HttpHeader_CONTENT_TYPE, type);
  }

  /*
   * Returns the content_reader specifying this requests's message body.
   *
   * @note When you read from the resulting data reader, you will modify
   * the position in the stream. If the content was already read then it
   * may already be at the end (or in the middle) of the stream. If you are
   * unsure, check the position and attempt to reset it if it is not at 0.
   *
   * @return NULL if there is no message body.
   *
   * @see DataReader::offset()
   */
  DataReader* content_reader() const { return content_reader_.get(); }

  /*
   * Specifies the requests message body using a DataReader.
   *
   * @param[in] reader Ownership is passed to the request instance.
   */
  void set_content_reader(DataReader* reader);

  /*
   * Removes the named header, if it exists.
   *
   * @param[in] name The name of the header to remove is not case-sensitive.
   */
  void RemoveHeader(const StringPiece& name);

  /*
   * Adds a header, or replaces its value if it already exists.
   *
   * @param[in] name Header names are not case sensitive.
   * @param[in] value The value to assign the header.
   *
   * The underlying strings will be copied into this object instance.
   *
   * @todo(ewiseblatt): 20130430
   * http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2
   * says certain types of request headers can be repeated however here
   * we are requiring request headers to be unique. We do permit
   * repeatable response headers.
   */
  void AddHeader(const StringPiece& name, const StringPiece& value);

  /*
   * Get the value of the named header.
   *
   * @return NULL if the named header is not present, otherwise returns a
   *         pointer to the header value.
   *
   * A non-NULL result will only be valid until a header is added or removed
   * (or the object is destroyed).
   */
  const string* FindHeaderValue(const StringPiece& name) const;

  /*
   * Get all the headers explicitly added to the request.
   *
   * @return the set of all explicitly added headers (keyed by name)
   */
  const HttpHeaderMap& headers() const { return header_map_; }

  /*
   * Get the HTTP method for the request.
   *
   * @return The method bound in the constructor.
   */
  HttpMethod http_method() const  { return http_method_; }

  /*
   * Indicate that the method will never Execute.
   *
   * This method is intended for higher level uses where a component
   * may own an HttpRequest but choose not to call it for some reason such
   * as a precondition failure. This method allows the status to be pushed
   * into the request and any asynchronous callback to be notified since
   * the request is now considered a transport-level failrue.
   *
   * @param[in] status The transport_status to give the request
   * @param[in] callback If non-NULL then
   *            run the callback after setting the status.
   */
  void WillNotExecute(
      util::Status status, HttpRequestCallback* callback = NULL);

  /*
   * Synchronously send the request to the designated URL and wait for the
   * response.
   *
   * This method blocks the calling thread until the response has been received
   * and processed. The request will be done() when this call returns. If
   * DestroyWhenDone was called (or option set) then this instance will no
   * longer exist when the call returns. Note that since the instance owns
   * the reponse, the response will no longer be available once this instance
   * is destroyed.
   *
   * @return the response()->status().
   *
   * @see ExecuteAsync
   * @see response()
   * @see state()
   */
  util::Status Execute();

  /*
   * Asynchronously send the request to the designated URL then continue
   * this thread while the server is processing the request.
   *
   * param[in] callback If non-NULL the callback will be run once the
   * request is done() for whatever reason. The caller retains ownership
   * however typically single-use callbacks are used so the callback will
   * self-destruct once run.
   *
   * Note that the callback may or may not have been executed at the time
   * that this call returns. If using a self-destructing instance, you
   * should only look at the state and response in the callback.
   *
   * @see HttpRequestState::WaitUntilDone
   * @see NewCallback()
   * @see Execute
   * @see state()
   * @see response()
   */
  void ExecuteAsync(HttpRequestCallback* callback);

  /*
   * Sets the authorization credential.
   *
   * @param[in] cred Can be NULL. The caller retains ownership.
   */
  void set_credential(AuthorizationCredential* cred) { credential_ = cred; }

  /*
   * Returns the authorization credential.
   *
   * @return NULL if no authorization credential has been set.
   */
  AuthorizationCredential* credential() { return credential_; }

  /*
   * Returns mutable state.
   *
   * Ideally this is protected, but it is needed by MediaUploader
   * and perhaps other situations with higher level APIs that encounter
   * errors before it can send the request.
   */
  HttpRequestState* mutable_state() {
    return response_->mutable_request_state();
  }

  /*
   * Prepares the request instance so that it can be reused again.
   *
   * This method strips sensitive request headers and resets the request state.
   * It also clears the response state. It will leave the other data including
   * url, body, callback, and non-sensitive headers.
   *
   * @return ok if the request could not be prepared. The most common expected
   * reason for failure would be that the content_body could not be reset.
   */
  util::Status PrepareToReuse();

  /*
   * Prepare the request to follow a redirect link.
   *
   * The target for the redirect is obtained from a response header as
   * defined by the HTTP protocol.
   *
   * @param[in] num_redirects_so_far Number of redirects we've followed so
   * far while trying to execute the request.
   *
   * @return ok if the request could be prepared and failure if not.
   * Reasons for failure include exceeding the option limiting the number
   * of redirects that can be followed.
   *
   * @note This method is only intended to implement error handling on
   *       redirects. It allows the request object to change internal state to
   * redirect itself. This should be used with caution. It is biased
   * towards the default error handler and may not be generally applicable.
   */
  util::Status PrepareRedirect(int num_redirects_so_far);

 protected:
  /*
   * Constructs method instance.
   *
   * @param[in] method When choosing a particular HTTP method keep in mind that
   *            the server processing the URL may only support a particular
   *            supset for the given URL.
   * @param[in] transport The transport to bind to the request. Usually
   *            requests are created by transports, so this is normally the
   *            transport that created the request. Conceptually this is the
   *            transport to use when invoking the request.
   *
   * The constructor is only protected since this class is abstract.
   */
  HttpRequest(HttpMethod method, HttpTransport* transport);

  /*
   * Returns the transport instance bound to the request.
   */
  HttpTransport* transport() const { return transport_; }

  /*
   * Initiate the actually messaging for this message with the HTTP server.
   *
   * Concrete classes should override this to work with the transport to
   * actually send the request and receive the response. The method does
   * not return until the request is done().
   *
   * The caller should leave responsibility to the base class for
   * transitioning state and running the callback. The method should set
   * the http_code if a response is received or transport_status if the request
   * could not be sent. It is also responsible for writing the response data
   * into the response body_reader.
   *
   * This method should not finish with both a transport_status.ok()
   * and http_code() == 0.
   *
   * @see HttpResponse::body_writer
   * @see Httpresponse::set_body_reader()
   * @see response()
   */
  virtual void DoExecute(HttpResponse* response) = 0;

 private:
  HttpMethod http_method_;
  HttpRequestOptions options_;
  HttpTransport* transport_;               // Not owned, cannot be NULL.
  AuthorizationCredential* credential_;    // Not owned, might be NULL.
  scoped_ptr<DataReader> content_reader_;  // payload can be NULL
  HttpHeaderMap header_map_;               // headers for request
  scoped_ptr<HttpResponse> response_;      // response and request state
  string url_;                             // url to send request to
  bool busy_;                              // dont destroy it yet.

  /*
   * Adds the builtin headers implied by the framework.
   *
   * These are things like user-agent and content-length.
   * See the implementation for the full list.
   */
  void AddBuiltinHeaders();

  DISALLOW_COPY_AND_ASSIGN(HttpRequest);
};

}  // namespace client

} // namespace googleapis
#endif  // APISERVING_CLIENTS_CPP_HTTP_REQUEST_H_

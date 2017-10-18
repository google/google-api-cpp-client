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
 * @defgroup ClientServiceLayer Client Service Layer
 *
 * The Client Service Layer provides application-level support for interacting
 * with Google Cloud Platform services and endpoints. It makes use of the
 * HTTP Transport Layer for the actual messaging and response handling but
 * hides these low level details with higher level abstractions specialized
 * for specific use cases.
 *
 * Much of this layer is geared around the consumption of services that present
 * REST-style interfaces.
 *
 * Typically programmers use the Google APIs Code Generator to generate a
 * C++ library specific to the service(s) they will be using. The output
 * of the code generator are libraries built ontop of the offerings in this
 * module.
 *
 * The client service layer also includes additional helper classes such as
 * the ServiceRequestPager for paging through large resources using finer
 * granularity requests.
 */
#ifndef GOOGLEAPIS_SERVICE_CLIENT_SERVICE_H_
#define GOOGLEAPIS_SERVICE_CLIENT_SERVICE_H_

#include <memory>
#include <string>
using std::string;
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/macros.h"
#include "googleapis/strings/stringpiece.h"
namespace googleapis {

namespace client {
class AuthorizationCredential;
class HttpRequest;
class HttpRequestBatch;
class MediaUploader;
class SerializableJson;
struct UriTemplateConfig;

class ClientService;

/*
 * Base class for requests made to a ClientService.
 * @ingroup ClientServiceLayer
 *
 * This class is based on a Command Pattern. The instance is given the service
 * endpoint to invoke and is given the arguments to invoke with as expected
 * by that endpoint. When its Execute() method is called, it will invoke
 * the command and wait for the response. The caller can get any response
 * data as well as the overall status from the request instance.
 *
 * When using the code generator to create custom APIs for a given service,
 * the code generator will subclass these requests for each endpoint API.
 * The specialized subclasses will contain higher level methods for setting
 * the various arguments and parameters that are available to use. Those
 * class instances are created using the ClientService instance (i.e. the
 * ClientService acts as a ClientServiceRequest factory. Therefore this class
 * is not typically explicitly instantiated. Lower level code may use this
 * class directly since it is concrete and fully capable.
 *
 * You should not explicitly destroy this class (or derived classes) when
 * making asynchronous requests unless you know that the request has
 * completely finished executing (its underlying HttpRequest is done).
 * It is safest to use DestroyWhenDone() instead. Otherwise code on the stack
 * may still be referencing the instance after it is destroyed, and your
 * program will crash.
 *
 * Requests are given a service and
 * <a href='http://tools.ietf.org/html/rfc6570'>RFC 6570</a>
 * URI Template to invoke within that service. The purpose of the URI
 * template is to ultimately provide the URL within the service. The
 * request instance can resolve the parameters in the template. This is
 * described in more detail with the PrepareUrl() method.
 */
class ClientServiceRequest {
 public:
  /*
   * Construct a new request.
   *
   * @param[in] service The service to send the request to
   * @param[in] credential If not NULL, the credentials to invoke with
   * @param[in] method The HTTP method when making the HTTP request
   * @param[in] uri_template The URI template specifying the url to invoke
   *
   * @see DestroyWhenDone
   * @see Execute
   * @see ExecuteAsync
   */
  ClientServiceRequest(
      const ClientService* service, AuthorizationCredential* credential,
      const HttpRequest::HttpMethod& method, const StringPiece& uri_template);

  /*
   * Standard instance destructor.
   *
   * @pre The HttpRequestState must be HttpRequestState::done()
   * @see DestroyWhenDone
   */
  virtual ~ClientServiceRequest();

  /*
   * Tell instance to self-destruct (destroy itself) once it is safe to.
   *
   * This is the preferred means to destroy instances that are used
   * asynchronously. The instance will remain active until after the
   * callback (if any) has finished running and after any signaling has
   * been performed to HttpResponse::WaitUntilDone().
   *
   * This method will destroy the object immediately if it is safe to do so.
   */
  void DestroyWhenDone();

  /*
   * Converts this instance into an HttpRequest and destroys itself.
   *
   * This method is intended to allow the request to be put into an
   * HttpRequestBatch, though you may execute it directly and treat it as
   * any other HTTP request. That means you would Execute the HttpRequest
   * rather than Execute this instance.
   *
   * @return The request returned will be similar to the http_request attribute
   *         but with the URI templated URL resolved based on the current
   *         configuration of the method's parameters. Ownership of the
   *         HttpRequest is passed back to the caller.
   *
   * @note This ClientServiceRequest instance will be destroyed when the method
   * returns.
   */
  HttpRequest* ConvertToHttpRequestAndDestroy();

  /*
   * Converts this instance into a batched HttpRequest and destroys itself.
   *
   * @param[in] batch The batch to add to will own the result.
   * @param[in] callback If non-null then set the HttpRequest callback for
   *            when this request finishes within the batch.
   * @return The result request will be configured similar to the original
   *         http_request attribute, but the instance might be different.
   *         Ownerhip of this result is passed to the batch parameter.
   *
   * @see ConvertToHttpRequestAndDestroy
   * @see HttpRequestBatch::AddFromGenericRequestAndRetire
   */
  HttpRequest* ConvertIntoHttpRequestBatchAndDestroy(
      HttpRequestBatch* batch, HttpRequestCallback* callback = NULL);

  /*
   * Ask the service to execute the request synchronously.
   *
   * The response data will be in the embedded HttpRequest
   *
   * @return status indicating the overall status of performing the request.
   *
   * @note HTTP failures (e.g. 4xx HTTP codes) are considered errors
   * as are transport level errors (e.g. unknown host). Finer grained status
   * information is available from the underlying HttpRequest.
   *
   * @see ExecuteAsync
   * @see ExecuteAndParseResponse
   * @see mutable_http_request
   * @see http_response
   */
  virtual googleapis::util::Status Execute();

  /*
   * Ask the service to execute the request asynchronously.
   *
   * @param[in] callback  If not NULL then run this once the request is done.
   *
   * The callback will be called once the HttpRequest::done() criteria are
   * satisfied on the underlying HttpRequest. This is always the case, even
   * on fundamental transport errors such as unknown host or if the request
   * is invalid.
   *
   * The callback can check the HttpRequest details, including its HttpResponse
   * to get status and response details. These will be valid while the callback
   * is running, but will no longer be valid once the request is destroyed.
   *
   * @warning The callback might be called from another thread than this one.
   * For normal execution flows where the request can be sent to the service,
   * the callback will be invoked from the bound executor's context. If there
   * is a fundamental problem with the request or it could not be queued then
   * it may (but not necessarily) be called from the current context before
   * the method returns.
   *
   * @warning If DestroyWhenDone() has been called before invoking this method
   * then the instance might complete and thus be destroyed before this method
   * returns. This is ok, but you will have no way of knowing this without
   * recording extra state in your callback.
   *
   * @see Execute
   * @see mutable_http_request
   */
  virtual void ExecuteAsync(HttpRequestCallback* callback);

  /*
   * Provides mutable access to the underlying HttpRequest.
   *
   * The request is set and managed by this instance.
   * The options on the request can be tuned. The request is owned by this
   * instance so you should not set its destroy_when_done attribute. Use
   * DestroyWhenDone on this ClientServiceRequest instance instead if desired.
   *
   * @see ConvertToHttpRequestAndDestroy
   */
  HttpRequest* mutable_http_request()  { return http_request_.get(); }

  /*
   * Returns the underlying HttpRequest.
   *
   * The request is set and managed by this instance.
   * The request provides access to its current HttpRequestState, response
   * status, and the actual response.
   */
  const HttpRequest* http_request() const  { return http_request_.get(); }

  /*
   * Returns the underlying response.
   *
   * The response is set and managed by this instance (actually by the
   * instance's request). It is only valid over the lifetime of this instance
   * so be sure to look at any values before you destroy the request.
   */
  HttpResponse* http_response() { return http_request_->response(); }

  /*
   * Parse the response payload (i.e. its body_reader) as a data instance.
   *
   * @param[in,out] response The response to parse is modified as it is read.
   * @param[out] data The data to parse into
   *
   * @return Failure if the response has no data or cannot be parsed.
   * @see HttpResponse::body_reader()
   */
  static googleapis::util::Status ParseResponse(
      HttpResponse* response, SerializableJson* data);

 protected:
  /*
   * Fills out the mutable_http_request() owned by this instance with the
   * information specified by this request.
   *
   * The base class implementation calls PrepareUrl and sets the url
   * on the underlying request. Specialized classes may have other needs,
   * such as setting the request payload.
   */
  virtual googleapis::util::Status PrepareHttpRequest();

  /*
   * Resolves the templated URL into the actual URL to use.
   *
   * The base class implementation assumes that the content was set in the
   * constructor. It uses UriTemplate to handle URL parameters and expects
   * that specialized subclasses will override the AppendVariable method to
   * resolve the values for the variables that this method finds in the
   * template.
   *
   * @param[in] templated_url An
   * <a href='http://tools.ietf.org/html/rfc6570'>RFC 6570</a> formatted URL.
   * @param[out] prepared_url The templated_url after resolving the variables.
   *
   * @note Conceptually this method is <code>const</code>. However technically
   * it is not because so it can used as a callback method where the mechanism
   * does not permit non-const methods.
   */
  virtual googleapis::util::Status PrepareUrl(
      const StringPiece& templated_url, string* prepared_url);

  /*
   * Appends the variable value to the target string.
   *
   * This method should use UriTemplate for the actual string append once
   * it locally determines what the value should be.
   *
   * @param[in] variable_name The name of the variable to append
   * @param[in] config A pass through parameter needed when asking
   * UriTemplate to append the strings. The value of this parameter is
   * determined internally by the methods within this class that invoke this
   * method.
   * @param[out] target The string to append to.
   */
  virtual googleapis::util::Status AppendVariable(
      const string& variable_name,
      const UriTemplateConfig& config,
      string* target);

  /*
   * Appends the optional query parameters to the url.
   *
   * This method is called by the default PrepareHttpRequest to add the
   * optional parameters that might not be explicitly stated in the URI
   * template that was bound to the request.
   *
   * The base method simply returns success. Specialized requests should
   * add any optional query parameters that have been added into the request.
   */
  virtual googleapis::util::Status AppendOptionalQueryParameters(string* target);

  /*
   * Execute the request synchronously. If the response suggests success then
   * load the response payload into the provided data parameter.
   *
   * @param[out] data The data will be cleared if the Execute was not
   * successful.
   * @return success if the Execute was successful and the response payload
   * could be loaded into the data object. Otherwise it will fail. If you want
   * to distinguish execute failures from response handling failures then you
   * will need to look at the http_response() details.
   *
   * This method is protected since it does not make sense on methods that
   * do not return json data objects. For those that do, their specialized
   * classes can expose this method by adding a public method that delegates
   * to this implementation.
   */
  googleapis::util::Status ExecuteAndParseResponse(SerializableJson* data);

  /*
   * Accessor for the use_media_download attribute.
   *
   * This attribute should only be exposed by methods that support it.
   * @return true if the request should use HTTP media download.
   */
  bool get_use_media_download() const   { return use_media_download_; }

  /*
   * Setter for the use_media_download attribute.
   *
   * This attribute should only be exposed by methods that support it.
   * @param[in] use  True if HTTP should use media download.
   */
  void set_use_media_download(bool use) { use_media_download_ = use; }

  /*
   * A helper method to set the media uploader.
   */
  void ResetMediaUploader(MediaUploader* uploader);

 public:
  /*
   * Returns a MediaUploader for uploading the content of this request.
   *
   * @return A MediaUploader* or NULL if there is no media content in this
   *         request.
   * TODO(user): Make this protected when uses of the deprecated style
   * of media request constructors are all removed.
   */
  client::MediaUploader* media_uploader() {
    return uploader_.get();
  }

 private:
  std::unique_ptr<HttpRequest> http_request_;  //!< The underlying HTTP request.

  /*
   * Destroy the request when it finishes executing.
   */
  bool destroy_when_done_;

  /*
   * States whether request should use HTTP media download.
   *
   * This adds an implied optional query parameter 'alt=media' when true.
   * The parameter gets appended in the base AppendOptionalQueryParameters.
   */
  bool use_media_download_;

  /*
   * Copy of the URI template, needed in case the http request is reused.
   */
  string uri_template_;

 protected:
  /*
   * The uploader for request with POST/PUT bodies.
   *
   * TODO(user): Migrate users to using ResetMediaUploader and make this
   * private.
   */
  std::unique_ptr<MediaUploader> uploader_;

 private:
  /*
   * A helper method.
   *
   * This is an implementation for UriTemplate::AppendVariableCallback that
   * calls the virtual AppendVariable() method on this instance so that
   * subclasses can feed ther values into the template.
   */
  googleapis::util::Status CallAppendVariable(
      const string& variable_name,
      const UriTemplateConfig& config,
      string* target);

  /*
   * A helper method.
   *
   * This is an HttpRequestCallback implementation used to auto-destroy
   * instances when done.
   */
  void CallbackThenDestroy(
      HttpRequestCallback* callback, HttpRequest* request);

  /*
   * A helper method.
   *
   * Helper method to handle Execute when there is a media uploader.
   */
  virtual googleapis::util::Status ExecuteWithUploader();

  DISALLOW_COPY_AND_ASSIGN(ClientServiceRequest);
};

/*
 * Base class for denoting a cloud service.
 * @ingroup ClientServiceLayer
 *
 * A ClientService is a proxy to some service endpoint in the cloud.
 * It acts as a Facade to a particular service and Factory for specific
 * ClientServiceRequest instances to make requests of specific service
 * interfaces. There are no service methods common across all services so
 * the base class has an empty Facade and no requests to provide a factory
 * for. The specialized subclasses are more interesting with regard to these
 * roles.
 */
class ClientService {
 public:
  /*
   * Constructs a service instance acting as a proxy to a given service
   * endpoint.
   *
   * @param[in] root_url  The root_url to access the given service.
   * Usually this is for the webserver that is hosting the service.
   *
   * @param[in] service_path  The additional path to append to the url
   * to get at the particular service.
   *
   * @param[in] transport A transport instance to use when sending requests
   * to the service. The service instance will take ownership of the transport.
   */
  ClientService(
    const StringPiece& root_url,
    const StringPiece& service_path,
    HttpTransport* transport);

  /*
   * Standard destructor.
   */
  virtual ~ClientService();

  /*
   * Begins shutting down the service handle. After Shutdown, the ClientService
   * and its transport are considered unavailable, and all attempts to make
   * HTTP requests will fail.  Applications must still join any threads which
   * may be asynchronously executing HTTP requests, so that callbacks can
   * complete. At that time, may the ClientService may be safely destroyed.
   */
  void Shutdown();

  /*
   * Returns true if Shutdown() has been called.
   */
  bool in_shutdown() const { return in_shutdown_; }

  /*
   * Returns the bound url_root attribute.
   */
  const StringPiece& url_root() const { return url_root_; }

  /*
   * Returns the bound url_path attribute.
   */
  const StringPiece& url_path() const { return url_path_; }

  /*
   * Returns the complete url to the service.
   * @return url_root + url_path
   */
  const string& service_url() const { return service_url_; }

  /*
   * Returns the complete url for batch requests.
   * @return url_root + batch_path
   */
  std::string batch_url() const;
  
  /*
   * Allows you to change the service_url.
   *
   * This method is intended as a hook to change the service_url location
   * from the default constructor, especially of specialized services that
   * might be created elsewhere. It is intended to point to a different
   * instance or location, such as for testing or staging.
   *
   * If you are going to change the URL, you should do so before you start
   * creating requests to send to it.
   *
   * @param[in] url_root The root url for the web server.
   * @param[in] url_path the path in the root_url for the service.
   * The service_url will be JoinPath(url_root,url_path)
   */
  void ChangeServiceUrl(
      const StringPiece& url_root, const StringPiece& url_path);

  /*
   * Allows you to change the URL used for batch operations.
   *
   * If you are going to change the URL, you should do so before you start
   * creating requests to send to it.
   *
   * @param[in] batch_path A path to append to url_root to form the URL for the
   * service's batch endpoint.
   */
  void SetBatchPath(StringPiece batch_path) {
    batch_path_.assign(batch_path.begin(), batch_path.end());
  }
  
  /*
   * Returns the transport instance bound in the constructor.
   */
  HttpTransport* transport() const { return transport_.get(); }

 private:
  string service_url_;
  StringPiece url_root_;  // Subset of service_url_
  StringPiece url_path_;  // Subset of service_url_
  std::string batch_path_;

  std::unique_ptr<HttpTransport> transport_;
  bool in_shutdown_;  // Has Shutdown() been called.

  DISALLOW_COPY_AND_ASSIGN(ClientService);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_SERVICE_CLIENT_SERVICE_H_

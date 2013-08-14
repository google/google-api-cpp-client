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
 * @defgroup TransportLayerCore Transport Layer - Core Components
 *
 * The HTTP transport layer provides lower-level support for interacting
 * with standard HTTP/1.1 web servers. In particular those that are used
 * to host cloud-based services such as the Google Cloud Platform. Much if
 * not all of it can be used for other HTTP servers and uses. In fact, the
 * Google APIs Client Library for C++ uses it in the OAuth 2.0 module for
 * messaging with OAuth 2.0 servers.
 *
 * The HTTP transport layer is designed as a keystone building block to
 * implement the Google APIs Client Library for C++. One aspect of this is
 * that it does not explicitly dictate a particular implementation for the
 * physical HTTP messaging. Instead it chiefly provides an abstraction for
 * messaging and a means for "plugging in" implementation details for the
 * physical messaging. To be useful, it provides a reasonable implementation
 * suitable for most purposes. However some clients or situations may wish
 * to use some other mechanism, especially pre-existing ones in their
 * runtime environments. This is accomodated by the HttpTransport class.
 *
 * In addiition to the HttpTransport class, the core HTTP transport layer
 * also includes the HttpRequest, HttpRequestState and HttpResponse and
 * httpTransportErrorHandler classes. It also includes an abstract
 * AuthorizationCredential class for making authorized requests.
 * The Google APIs Client Library for C++ builds on this abstract class
 * to provide OAuth 2.0 support in the OAuth2 2.0 module. Together
 * this allows the transport layer to be used to comply with OAuth 2.0
 * protected resources without actually depending on a specific client-side
 * implementation of OAuth 2.0.
 */

#ifndef APISERVING_CLIENTS_CPP_TRANSPORT_HTTP_TRANSPORT_H_
#define APISERVING_CLIENTS_CPP_TRANSPORT_HTTP_TRANSPORT_H_

#include <string>
using std::string;
#include <map>
using std::map;
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/scoped_ptr.h"
#include "googleapis/client/transport/http_request.h"
namespace googleapis {

namespace thread {
class Executor;
}  // namespace thread

namespace client {

class HttpScribe;
class HttpTransportFactory;  // forward declaration
class HttpTransportLayerConf;  // forward declaration

/*
 * Specifies the error handling policy for HTTP messaging.
 * @ingroup TransportLayerCore
 *
 * This class specifies the policy for different types of errors including:
 * <ul>
 *   <li>Transport erors
 *   <li>HTTP redirect responses
 *   <li>HTTP error response codes
 * </ul>
 *
 * Instances can further refine specific error handling for individual HTTP
 * status codes.
 *
 * @see HttpRequest
 */
class HttpTransportErrorHandler {
 public:
  /*
   * Callback for handling a specific HTTP status code.
   *
   * @param[in] int The HTTP status code.
   * @param[in] HttpRequest The request that had the error.
   * @return true if the callback requests a retry, false if not.
   *
   * The callback and make changes to the request to indicate how to
   * perform a retry.
   */
  typedef ResultCallback2<bool, int, HttpRequest*> HttpCodeHandler;

  /*
   * Replaces the existing handler for a given HTTP status code.
   *
   * @param[in] code The HTTP status code to handle.
   * @param[in] handler The handler to use can be NULL to remove it.
   *                    Ownership is passed.
   * non-NULL handlers must be a repeatable callback since it can be
   * alled multiple times.
   *
   * @see NewPermanentCallback()
   */
  void ResetHttpCodeHandler(int code, HttpCodeHandler* handler);

  /*
   * Standard constructor.
   */
  HttpTransportErrorHandler();

  /*
   * Standard destructor.
   */
  virtual ~HttpTransportErrorHandler();

  /*
   * Handles transport errors.
   *
   * @param[in] num_retries_so_far Number of retries performed already.
   * @param[in] request The request that caused the error.
   *
   * @return true if we should consider trying again, policy permitting.
   */
  virtual bool HandleTransportError(
      int num_retries_so_far, HttpRequest* request) const;

  /*
   * Handles HTTP redirects (HTTP 3xx series results).
   *
   * @param[in] num_redirects_so_far Number of redirects already followed.
   * @param[in] request The request that caused the error.
   *
   * @return true if we should consider trying again, policy permitting.
   */
  virtual bool HandleRedirect(
      int num_redirects_so_far, HttpRequest* request) const;

  /*
   * Handles erorrs from requests with HTTP status code errors.
   *
   * This includes 401 (Authorization) and 503 (Unavailable)
   *
   * @param[in] num_retries_so_far Number of retries performed already.
   * @param[in] request The request that returned the error.
   *
   * @return true if we should consider trying again, policy permitting.
   *
   * @see ResetHttpCodeHandler for overriding this behavior before subclassing.
   */
  virtual bool HandleHttpError(
      int num_retries_so_far, HttpRequest* request) const;

 private:
  /*
   * Maps HTTP status code to specific handler for it.
   */
  map<int, HttpCodeHandler*>  specialized_http_code_handlers_;

  DISALLOW_COPY_AND_ASSIGN(HttpTransportErrorHandler);
};

/*
 * Configures options on an HttpTransport instance
 * @ingroup TransportLayerCore
 *
 * Each HttpTransport instance maintains its own options however typically
 * the default options are set on the HttpTransportFactory so that the
 * options will apply to all transport instances. Sometimes you may in fact
 * want to configure an an individual instance in some special way.
 *
 * Options are simple data objects so support assignment and the copy
 * constructor.
 *
 * @see HttpTransport
 * @see HttpTransportFactory::mutable_default_transport_options()
 */
class HttpTransportOptions {
 public:
  /*
   * Standard constructor.
   *
   * The options will be constructed without an error handler or executor.
   * It will use the default cacerts_path for SSL verification the default
   * application name in the user agent.
   *
   * @see DetermineDefaultApplicationName
   * @see DetermineDefaultCaCertsPath
   */
  HttpTransportOptions();

  /*
   * Standard destructor.
   */
  virtual ~HttpTransportOptions();

  /*
   * Set the executor to use for asynchronous requests.
   *
   * Setting the executor to NULL will use the global default executor.
   *
   * @param[in] executor The executor this transport will use. The caller
   * retains ownership so should guarantee that the executor will remain
   * valid over the lifetime of these options and any options assigned from it.
   */
  void set_executor(thread::Executor* executor)  { executor_ = executor; }

  /*
   * Returns the executor that should be used with this transport.
   *
   * @return  If the transport is using the global default hten the global
   *          default will be returned. This might still be NULL if no global
   *          default was set.
   */
  thread::Executor* executor() const;

  /*
   * Returns the error handler for this transport.
   *
   * The default options have the standard built-in error handler pre-assigned.
   *
   * @return NULL if no error handler has been set.
   */
  const HttpTransportErrorHandler* error_handler() const {
    return error_handler_;
  }

  /*
   * Replaces the error handler to use on this transport.
   *
   * @param[in] error_handler/ NULL indicates do not perform any error
   *            handling. Caller retains ownership.
   */
  void set_error_handler(const HttpTransportErrorHandler* error_handler) {
    error_handler_ = error_handler;
  }

  /*
   * Returns the value used for the HTTP User-Agent header.
   *
   * We recommend using SetApplicationName to change the application name
   * if the default is not appropriate. Use set_nonstandard_user_agent to
   * change the default to an arbitrary literal value if you must.
   */
  const string& user_agent() const { return user_agent_; }

  /*
   * Refines the user agent to use the given application name.
   *
   * @param[in] name The application name to use within the User-Agent header.
   *
   * @see DetermineDefaultApplicationName
   */
  void SetApplicationName(const StringPiece& name);

  /*
   * Sets an exact literal value to use for the HTTP User-Agent header.
   *
   * You may use this, but we would prefer you use SetApplicationName instead,
   * especially if talking to servers hosted on the Google Cloud Platform.
   *
   * @see SetApplicationName
   */
  void set_nonstandard_user_agent(const string& agent);

  /*
   * Returns true if SSL verification has been disabled.
   *
   * To disable SSL verification you must explicitly set the cacerts_path
   * to kDisableSslVerification.
   */
  bool ssl_verification_disabled() const { return ssl_verification_disabled_; }

  /*
   * Returns the path to the SSL certificate authority validation data.
   */
  const string& cacerts_path() const     { return cacerts_path_; }

  /*
   * Sets the path to the SSL certificate authority validation data.
   *
   * @see DetermineDefaultCaCertsPath
   */
  void set_cacerts_path(const StringPiece& path);

  /*
   * An identifier used to declare this client library within the User-Agent.
   */
  static const StringPiece kGoogleApisUserAgent;

  /*
   * A magical cacerts_path value indicating we intend on disabling
   * the ca certificate validation.
   */
  static const StringPiece kDisableSslVerification;

  /*
   * Returns the default application name assumed for this process.
   *
   * The default name will be the filename of the program that is
   * curently running (without other path elements).
   */
  static string DetermineDefaultApplicationName();

  /*
   * Returns the default location of the SSL certificate validation data.
   *
   * The default location is assumed to be the same directory as the
   * executable for the running process in a file called 'roots.pem'.
   */
  static string DetermineDefaultCaCertsPath();

 private:
  /*
   * The value for the User-Agent header identifying this client.
   */
  string user_agent_;

  /*
   * The path to the SSL certificate validation file.
   */
  string cacerts_path_;

  /*
   * True if SSL validation is disabled.
   */
  bool ssl_verification_disabled_;


  /*
   * Specifies the executor to use for asynchronous requests.
   *
   * Not owned. NULL means use global default.
   */
  thread::Executor* executor_;

  /*
   * Specifies the error handler to use.
   *
   * Not owned. NULL means no error handling.
   */
  const HttpTransportErrorHandler* error_handler_;
};


/*
 * Specifies the implementation components for the TransportLayer.
 * @ingroup TransportLayerCore
 */
class HttpTransportLayerConfig {
 public:
  /*
   * Standard constructor does not bind a default HttpTransportFactory.
   *
   * The default configuration does not bind a specific transport factory
   * so you must add one yourself. For the application name and SSL certificate
   * validation see the static methods that determine the default values for
   * more details.
   *
   * @see DetermineDefaultCaCertsPath();
   * @see DetermineDefaultApplicationName();
   */
  HttpTransportLayerConfig();

  /*
   * Standard destructor.
   */
  virtual ~HttpTransportLayerConfig();

  /*
   * Sets the default transport factory.
   *
   * @param[in] factory Ownership is passed to the configuration.
   *            NULL will unset the default factory.
   */
  void ResetDefaultTransportFactory(HttpTransportFactory* factory);

  /*
   * Returns the default transport factory, if one was set.
   *
   * @warning
   * There is no default transport factory. You must explicitly set it.
   *
   * @return The configuration maintains ownership. NULL is returned if there
   *         is no factory set.
   */
  HttpTransportFactory* default_transport_factory() const {
    return default_transport_factory_.get();
  }

  /*
   * Returns the default HttpTransport options for this configuration.
   *
   * The returned options can be copied and modified for each transport
   * independently.
   */
  const HttpTransportOptions& default_transport_options() const {
    return default_options_;
  }

  /*
   * Returns a modifiable instance for changing the default HttpTransport
   * options.
   */
  HttpTransportOptions* mutable_default_transport_options() {
    return &default_options_;
  }

  /*
   * Returns a new transport using the default transport factory with the
   * default user agent set.
   *
   * @param[out] status Reason for failure if NULL is returned
   * @return New transport instance created by the default transport factory.
   *
   * This method requires that ResetDefaultTransportFactory was
   * called or it will return NULL and set an error status.
   *
   * @see ResetDefaultTransportFactory
   */
  HttpTransport* NewDefaultTransport(util::Status* status) const;

  /*
   * Create a new transport or terminate the program on failure.
   *
   * Similar to NewDefaultTransport but will CHECK-fail rather than returning
   * NULL if there is not default transport factory to use.
   *
   * @see ResetDefaultTransportFactory
   */
  HttpTransport* NewDefaultTransportOrDie() const;

  /*
   * Resets the error handler used by the default options, passing ownership.
   *
   * The HttpTransportOptions does not own its error handler. This method
   * changes the error handler in the default transport options but passes
   * ownership of that handler to this configuration. Otherwise the caller
   * would need to take responsibility for it if it set the handler directly
   * in the mutable_default_transport_options.
   *
   * This method will destroy any previous default error handler that was
   * owned by this configuration. If there were other HttpTransportOptions
   * previously using it then they will become corrupted with an invalid
   * pointer to the old (deleted) handler.
   *
   * @param[in] error_handler Ownership is passed to the configuration.
   *            NULL will unset the default error handler in the options
   *            as well.
   */
  void ResetDefaultErrorHandler(HttpTransportErrorHandler* error_handler);

  /*
   * Resets the executor used by the default options, passing ownership.
   *
   * The HttpTransportOptions does not own its executor. This method
   * changes the executor in the default transport options but passes
   * ownership of that handler to this configuration. Otherwise the caller
   * would need to take responsibility for it if it set the handler directly
   * in the mutable_default_transport_options.
   *
   * This method will destroy any previous default executor that was
   * owned by this configuration. If there were other HttpTransportOptions
   * previously using it then they will become corrupted with an invalid
   * pointer to the old (deleted) handler.
   *
   * @param[in] executor Ownership is passed to the configuration.
   *            NULL will unset the default executor in the options as well.
   */
  void ResetDefaultExecutor(thread::Executor* executor);

 private:
  HttpTransportOptions default_options_;
  scoped_ptr<HttpTransportFactory> default_transport_factory_;
  scoped_ptr<HttpTransportErrorHandler> default_error_handler_;
  scoped_ptr<thread::Executor> default_executor_;

  DISALLOW_COPY_AND_ASSIGN(HttpTransportLayerConfig);
};


/*
 * Abstract interface defining an HTTP transport will be specialized
 * for different concrete mechanisms for interacting with HTTP servers.
 * @ingroup TransportLayerCore
 *
 * This is an abstract interface that acts as the base class for all
 * Http Transports used by the Google APIs for C++ runtime library.
 *
 * It is recommended that you always use this class when defining classes and
 * interfaces rather than the concrete subclasses. It is recommended that you
 * create instances using factory -- either HttpTransportFactory or
 * <code>HttpTransportLayerConfig::NewDefaultTransport()</code> on an
 * <code>HttpTransportLayerConfig</code> instance.
 *
 * An HttpTransport instance is stateless. It can accomodate multiple
 * outstanding requests to different servers at the same time. There is no
 * technical reason to have multiple instances other than wanting different
 * configurations, such as standard request options.
 *
 * You must set the HttpTransportOptions::set_cacerts_path() with the path to
 * a file to validate SSL certificates. The SDK provides a "roots.pem"
 * which can validate all the certificates used by Google Cloud Platform
 * servers and other commonly used certificates. To disable SSL verification
 * you can set it to HttpTransportOptions::kDisableSslValidation,
 * though doing so will lose security against some forms of SSL attacks.
 *
 * @see HttpRequest
 * @see HttpTransportFactory
 * @see HttpTransportLayerConfig
 * @see HttpTransportOptions
 */
class HttpTransport {
 public:
  /*
   * Construct the transport using the provided options.
   *
   * @param[in] options The options to configure the transport with.
   *
   * The options will be copied into a local attribute managed by this
   * instance. To make changes once this instance has been constructed
   * use mutable_transport_options().
   */
  explicit HttpTransport(const HttpTransportOptions& options);

  /*
   * Starndard destructor
   */
  virtual ~HttpTransport();

  /*
   * Returns the value of the User-Agent header for this transport instance.
   *
   * To change the value, modify the options.
   */
  const string& user_agent() const { return options().user_agent(); }

  /*
   * Retrieve the transport options for this instance
   */
  const HttpTransportOptions& options() const  { return options_; }

  /*
   * Get the options to modify this instance.
   *
   * Changing the result options will affect the configuration of this
   * instance but might not have effect on existing requests.
   */
  HttpTransportOptions* mutable_options()     { return &options_; }

  /*
   * Returns the default options used to initialize new HttpRequest instances.
   */
  const HttpRequestOptions& default_request_options() const {
    return default_request_options_;
  }

  /*
   * Returns modifiable options used to initialize new HttpRequest instances.
   *
   * Changing the options will affect new requests created but will not
   * affect any existing requests.
   */
  HttpRequestOptions* mutable_default_request_options() {
    return &default_request_options_;
  }

  /*
   * The transport id is used to tag instances for debug/tracing purposes.
   *
   * The default convention is to use simply the type of transport (ie 'curl').
   */
  void set_id(const StringPiece& id)  { id_ = id.as_string(); }

  /*
   * Returns the instance id for debug/tracing purposes.
   */
  const string& id() const  { return id_; }

  /*
   * Sets (or clears) the request scribe.
   *
   * Scribes incur significant overhead so are not indended for production
   * use. They are for debugging, testing, and diagnostics.
   *
   * @param[in] scribe The caller maintains ownership. NULL clears the scribe.
   */
  void set_scribe(HttpScribe* scribe) { scribe_ = scribe; }

  /*
   * Returns the bound scribe, if any.
   *
   * @return NULL if no scribe is bound.
   */
  HttpScribe* scribe() const { return scribe_; }

  /*
   * Create a new HttpRequest instance that will use this transport.
   *
   * This method is the HttpRequest factory. It is the preferred (and often
   * only) way to instantiate a request.
   *
   * @return a new HttpRequest
   */
  virtual HttpRequest* NewHttpRequest(
      const HttpRequest::HttpMethod& method) = 0;

 private:
  string id_;
  HttpTransportOptions options_;
  HttpRequestOptions default_request_options_;
  HttpScribe* scribe_;  // Reference is not owned

  DISALLOW_COPY_AND_ASSIGN(HttpTransport);
};


/*
 * Abstract interface for creating concrete HttpTransport instances.
 * @ingroup TransportLayerCore
 *
 * This class implements a Factory pattern for instantiating new HttpTransport
 * instances. It acts as the base class for all HttpTransport factories used
 * by the Google APIs Client Library for C++ runtime library.
 *
 * It is recommended that you always use this class when defining classes and
 * interfaces rather than the concrete subclasses. If you need a factory but
 * do not care about the implementation, consider
 * HttpTransport::default_transport_factory().
 *
 * Current HttpTransport::default_transport_factory must be explicit set at
 * some point during execution (such as application startup).
 *
 * @see New
 */
class HttpTransportFactory {
 public:
  /*
   * Default constructor creates instances with the default configuration
   * default <code>HttpTransportOptions</code>.
   *
   * This method still calls DoAlloc with the default options.
   */
  HttpTransportFactory();

  /*
   * Standard constructor.
   *
   * Caller maintains ownership of the configuration, which can be shared
   * across mutliple factory instances.
   *
   * @param[in] config If NULL then this behaves similar to the
   *            default constructor. If non-null the caller retains ownership.
   *
   * @note
   * This factory instance is independent of the default_transport_factory
   * in the config. If you want to use this instance as the default in the
   * config then call config->ResetDefaultTransportFactory with this instance
   * after the constructor returns.
   */
  explicit HttpTransportFactory(const HttpTransportLayerConfig* config);

  /*
   * Standard destructor.
   */
  virtual ~HttpTransportFactory();

  /*
   * Construct a new transport instance with the provided options.
   */
  HttpTransport* NewWithOptions(const HttpTransportOptions& options);

  /*
   * Construct a new instance using the default transport options given
   * to this factory instance.
   */
  HttpTransport* New() {
    return NewWithOptions(config_
                          ? config_->default_transport_options()
                          : HttpTransportOptions());
  }

  /*
   * Get the modifiable default request options given the transport instances
   * created by this factory.
   *
   * Changing these options will affect future transport instances created
   * but will not affect exsting transport or request instances.
   */
  HttpRequestOptions* mutable_request_options()  {
    return &default_request_options_;
  }

  /*
   * Returns the default request options assigned by this factory instance.
   */
  const HttpRequestOptions& default_request_options() const {
    return default_request_options_;
  }

  /*
   * Returns the default id to assign new transport instances created.
   */
  const string& default_id() const      { return default_id_; }

  /*
   * Change the default transport identifier for new instances.
   *
   * @param[in] id The initial identifier to use for new instances is typically
   *               the name of the transport implementation.
   *
   * @see HttpTransport::id()
   */
  void set_default_id(const StringPiece& id) { default_id_ = id.as_string(); }

  /*
   * Sets the scribe to bind to instances.
   *
   * If this is non-NULL then it must remain valid until any transports
   * created with this factory are finished. Since the factory will own the
   * scribe, that means the factory cannot be destroyed either until all
   * transports created from this are no longer in used.
   *
   * @param[in] scribe Ownership is passed. NULL will clear.
   */
  void reset_scribe(HttpScribe* scribe);

  /*
   * Returns the scribe, if any.
   */
  HttpScribe* scribe() { return scribe_.get(); }

  /*
   * Returns the configuration that this factory was constructed with.
   *
   * @return NULL if it just uses the defaults.
   */
  const HttpTransportLayerConfig* config() { return config_; }

 protected:
  /*
   * Specialized factories override this method to create new instances.
   *
   * The base class will add the scribe and other factory configurations.
   */
  virtual HttpTransport* DoAlloc(const HttpTransportOptions& options) = 0;

 private:
  const HttpTransportLayerConfig* config_;
  HttpRequestOptions default_request_options_;
  scoped_ptr<HttpScribe> scribe_;
  string default_id_;

  DISALLOW_COPY_AND_ASSIGN(HttpTransportFactory);
};

}  // namespace client

} // namespace googleapis
#endif  // APISERVING_CLIENTS_CPP_TRANSPORT_HTTP_TRANSPORT_H_

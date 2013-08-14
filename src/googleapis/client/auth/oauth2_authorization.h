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
 * @defgroup AuthSupportOAuth2 Auth Support - OAuth 2.0 Module
 *
 * The OAuth 2.0 module provides support for
 * <a href='http://tools.ietf.org/html/rfc6749#section-1.3.1'>
 * RFC 6749 OAuth 2.0</a>. The Google Cloud Platform uses OAuth 2.0 to
 * authorize access to services and endpoints when refering user data and
 * other protected resources.
 *
 * The OAuth 2.0 module provides the authorization for the HTTP transport layer
 * by using building on the AuthorizationCredential abstraction defined by the
 * transport layer. It adds a specialized mediator, OAuth2AuthorizationFlow, to
 * simplify the complex interactions making up the OAuth 2.0 protocol.
 *
 * The other abstractions using AuthorizationCredential can be used with
 * the OAuth2Credential defined by this module. This includes CredentialStore
 * for persisting credentials. The high level OAuth2AuthorizationFlow mediator
 * provides support for using a CredentialStore.
 */

#ifndef APISERVING_CLIENTS_CPP_AUTH_OAUTH2_AUTHORIZATION_H_
#define APISERVING_CLIENTS_CPP_AUTH_OAUTH2_AUTHORIZATION_H_

#include <string>
using std::string;
#include <vector>
using std::vector;
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/mutex.h"
#include "googleapis/base/scoped_ptr.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/strings/stringpiece.h"
namespace googleapis {

namespace client {
class CredentialStore;
class HttpTransport;
class HttpRequest;
class HttpResponse;

class OAuth2AuthorizationFlow;
class OAuth2ClientSpec;
class OAuth2Credential;

/*
 * A data object containing specifying the client information to present to
 * the OAuth 2.0 server.
 * @ingroup AuthSupportOAuth2
 *
 * Normally this class is created by
 * OAuth2AuthorizationFlow::InitFromJsonData() and not created directly.
 * The various attribute values come from the
 * <a href='https://code.google.com/apis/console/'> Google APIs console</a>
 * when registering the application.
 *
 * @see OAuth2AuthorizationFlow::InitFromJsonData()
 */
class OAuth2ClientSpec {
 public:
  /*
   * Standard constructor.
   */
  OAuth2ClientSpec();

  /*
   * Standard destructor.
   */
  ~OAuth2ClientSpec();

  /*
   * Returns the client ID.
   */
  const string& client_id() const { return client_id_; }

  /*
   * Sets the client ID.
   */
  void set_client_id(const StringPiece& id) { client_id_ = id.as_string(); }

  /*
   * Returns the client secret.
   */
  const string& client_secret() const { return client_secret_; }

  /*
   * Sets the client secret.
   */
  void set_client_secret(const StringPiece& secret) {
    client_secret_ = secret.as_string();
  }

  /*
   * Returns the redirect url.
   */
  const string& redirect_uri() const { return redirect_uri_; }

  /*
   * Sets the redirect url.
   *
   * @see OAuth2AuthorizationFlow::kOutOfBandUrl
   */
  void set_redirect_uri(const StringPiece& uri) {
    redirect_uri_ = uri.as_string();
  }


  /*
   * Returns the url for requesting an OAuth2 Authorization Code for
   * this service.
   */
  const string& auth_uri() const { return auth_uri_; }

  /*
   * Sets the url for requesting an OAuth2 Authorization Code for this service.
   */
  void set_auth_uri(const StringPiece& uri) { auth_uri_ = uri.as_string(); }


  /*
   * Returns the url for requesting an OAuth2 Access Token for this service.
   */
  const string& token_uri() const { return token_uri_; }

  /*
   * Sets the url for requesting an OAuth2 Access Token for this service.
   */
  void set_token_uri(const StringPiece& uri) { token_uri_ = uri.as_string(); }


  /*
   * Returns url for revoking an OAuth2 Access Token for this service.
   */
  const string& revoke_uri() const { return revoke_uri_; }

  /*
   * Sets the url for revoking an OAuth2 Access Token for this service.
   */
  void set_revoke_uri(const StringPiece& uri) { revoke_uri_ = uri.as_string(); }

 private:
  string client_id_;
  string client_secret_;
  string redirect_uri_;
  string auth_uri_;
  string token_uri_;
  string revoke_uri_;

  DISALLOW_COPY_AND_ASSIGN(OAuth2ClientSpec);
};

// Implements a threadsafe string. This is intended for access/refresh tokens
// which are primarily read only to copy them into headers and such but will
// need to be updated at some point, perhaps in another thread. If the mutex
// is in the object managing the string (e.g. credential managing the access
// token) then the update and access operations would need to be in that class
// as well (or the mutex exposed externally). However pushing the mutex down
// to the attribute (e.g. access token) decouples the management from uses,
// plus allows the different attributes to be managed independent of one
// another.
/*
 * For internal use only
 */
class ThreadsafeString {
 public:
  ThreadsafeString() {}

  bool empty() const {
    MutexLock l(&mutex_);
    return value_.empty();
  }

  void clear() {
    MutexLock l(&mutex_);
    value_.clear();
  }

  void set(const StringPiece& value) {
    MutexLock l(&mutex_);
    value_ = value.as_string();
  }

  string as_string() const {
    MutexLock l(&mutex_);
    return value_;
  }

  void AppendTo(string *target) const {
    MutexLock l(&mutex_);
    target->append(value_);
  }

 private:
  mutable Mutex mutex_;
  string value_;
  DISALLOW_COPY_AND_ASSIGN(ThreadsafeString);
};

/*
 * For internal use only
 */
template <typename T>
class ThreadsafePrimitive {
 public:
  ThreadsafePrimitive() : value_(0) {}
  T get() const {
    MutexLock l(&mutex_);
    return value_;
  }

  void set(T value) {
    MutexLock l(&mutex_);
    value_ = value;
  }

 private:
  mutable Mutex mutex_;
  T value_;
  DISALLOW_COPY_AND_ASSIGN(ThreadsafePrimitive);
};

/*
 * Specifies an OAuth 2.0 Credential.
 * @ingroup AuthSupportOAuth2
 *
 * The purpose of a credential is to carry the available access and
 * refresh tokens used to authorize HTTP requests.
 *
 * The easiest way to manage credentials is using
 * OAuth2AuthorizationFlow::RefreshCredentialWithOptions(), which lets you
 * treat the credential as an opaque type and not worry about any of its
 * attributes or other methods to manage them.
 *
 * However sometimes you may need to manipulate the credentials directly
 * if you are using non-standard storage techniques.
 *
 * @see OAuth2AuthorizationFlow::RefreshCredentialWithOptions()
 * @see CredentialStore
 */
class OAuth2Credential : public AuthorizationCredential {
 public:
  /*
   * Constructs an empty credential.
   *
   * @see Load()
   */
  OAuth2Credential();

  /*
   * Standard destructor.
   */
  ~OAuth2Credential();

  /*
   * Clears all the values in the credential but does not revoke any tokens.
   */
  void Clear();

  /*
   * Binds a flow to this instance so that it can support Refresh().
   *
   * @param[in] flow  NULL unbinds the flow. Otherwise, the caller retains
   *                  ownership of the flow so must keep it active.
   */
  void set_flow(OAuth2AuthorizationFlow* flow) { flow_ = flow; }

  /*
   * Returns the flow currently bound.
   */
  OAuth2AuthorizationFlow* flow() const { return flow_; }

  /*
   * Sets the access token.
   *
   * @param[in] access_token The access token provided by the OAuth 2.0 server.
   *
   * @see OAuth2ExchangeAuthorizationCodeRequest
   * @see OAuth2RefreshTokenRequest
   */
  void set_access_token(const StringPiece& access_token) {
    access_token_.set(access_token);
  }

  /*
   * Sets the refresh token.
   *
   * @param[in] refresh_token The refresh token provided by the
   *                          OAuth 2.0 server.
   *
   * @see OAuth2ExchangeAuthorizationCodeRequest
   */
  void set_refresh_token(const StringPiece& refresh_token) {
    refresh_token_.set(refresh_token);
  }

  /*
   * Returns the access token.
   *
   * The access token might no longer be valid.
   *
   * @return to get the value, take .as_string() from the result.
   *
   */
  const ThreadsafeString& access_token() const  { return access_token_; }

  /*
   * Returns a modifiable access token.
   */
  ThreadsafeString* mutable_access_token()      { return &access_token_; }


  /*
   * Returns the refresh token.
   *
   * @return to get the value, take .as_string() from the result.
   */
  const ThreadsafeString& refresh_token() const { return refresh_token_; }

  /*
   * Returns a modifiable access token.
   */
  ThreadsafeString* mutable_refresh_token()     { return &refresh_token_; }


  /*
   * Returns the timestamp (in epoch seconds) when the access token will
   * expire.
   *
   * @return Absolute value of epoch seconds might have already passed.
   */
  int64 expiration_timestamp_secs() const {
    return expiration_timestamp_secs_.get();
  }

  /*
   * Sets the timestamp (in epoch seconds) when the access token will
   * expire.
   *
   * @param[in] secs Absolute value of epoch seconds.
   */
  void set_expiration_timestamp_secs(int64 secs) {
    expiration_timestamp_secs_.set(secs);
  }

  /*
   * Resets the credential from the JSON data in the reader.
   *
   * @param[in] reader The JSON stream containing the credential attributes.
   * @return status indicating whether the credential values could be reset.
   *
   * @see Update()
   */
  virtual util::Status Load(DataReader* reader);

  /*
   * Updates the credential attributes from the JSON data in the reader.
   *
   * Unlike Load(), any attributes not explicitly specified in the reader
   * data will be left as is.
   *
   * @param[in] reader The JSON stream containing the credential attributes
   *            to update.
   * @return status indicating whether the credential values could be updated.
   */
  virtual util::Status Update(DataReader* reader);

  /*
   * Serializes the credential as a JSON stream.
   *
   * @return an in-memory data reader specifying the credential.
   */
  virtual DataReader* MakeDataReader() const;

  /*
   * Identifies this as an kOAuth2Type credential.
   */
  virtual const StringPiece type() const;

  /*
   * Adds the OAuth 2.9 Authorization Bearer header to the request.
   *
   * The value of the header will be the current access token.
   */
  virtual util::Status AuthorizeRequest(HttpRequest* request);

  /*
   * Updates the credential from a JSON string.
   *
   * Existing properties not present in the json are left as is.
   *
   * @param[in] json The JSON string containing the attributes to update.
   * @return fail if the JSON argument was not valid.
   *
   * @see Update()
   */
  util::Status UpdateFromString(const StringPiece& json);

  /*
   * Returns the user ID associated with this credential, if known.
   *
   * The user_id is not used for runtime authentication.
   * It is normally used for managing persistence but certain flows such as
   * web flow may use it as a login_hint.
   *
   * @warning The client application may have set the user id different from
   *          the source which actually authorized the user, making these out
   *          of sync.
   *
   * @see user_id_verified
   */
  const string& user_id() const { return user_id_; }

  /*
   * Returns true if the user_id has been verified to be the actual user id
   * that authorized access.
   *
   * @warning even if this is true, this data structure might not have come
   *          from a secure source so this attribute might be compramised.
   */
  bool user_id_verified() const { return user_id_verified_; }

  /*
   * Set the user_id for the credential.
   *
   * @param[in] user_id The user_id will be copied into the credental.
   * @param[in] verified Whether or not the user_id has been verified as true.
   */
  void set_user_id(const StringPiece& user_id, bool verified) {
    user_id_ = user_id.as_string();
    user_id_verified_ = verified;
  }

  /*
   * The AuthorizationCredential type identfying OAuth 2.0 credentials.
   */
  static const StringPiece kOAuth2CredentialType;

  /*
   * Attempts to refresh the credential.
   *
   * This will certainly fail if there is no flow bound.
   *
   * @return ok on success or reason for failure.
   */
  virtual util::Status Refresh();

 private:
  OAuth2AuthorizationFlow* flow_;
  ThreadsafeString access_token_;
  ThreadsafeString refresh_token_;
  ThreadsafePrimitive<int64> expiration_timestamp_secs_;
  string user_id_;
  bool user_id_verified_;

  DISALLOW_COPY_AND_ASSIGN(OAuth2Credential);
};

/*
 * Options for overriding the default attributes in an OAuth2AuthorizationFlow
 * when making OAuth2TokenRequests.
 * @ingroup AuthSupportOAuth2
 *
 * These overrides are used in APIs to change the default values configured
 * in the flow. Empty values will defer to the default value (if any).
 */
struct OAuth2RequestOptions {
  string redirect_uri;  //!< if empty use flow's default redirect_uri
  string scopes;        //!< if empty use flow's default scopes
  string user_id;       //!< an optional key for credential_store
};

/*
 * The base class for token-related requests made to an OAuth 2.0 server.
 *
 * @deprecated Use OAuth2AuthorizatioFlow methods
 */
class OAuth2TokenRequest {
 public:
  /*
   * Constructor
   * @param[in] request Takes ownership
   */
  explicit OAuth2TokenRequest(HttpRequest* request);

  /*
   * Standard destructor.
   */
  virtual ~OAuth2TokenRequest();

  /*
   * Sends the request to the OAuth 2.0 server and waits until the
   * response comes back and has been processed.
   *
   * @return ok if the request was successful, otherwise the cause of failure.
   */
  virtual util::Status Execute() = 0;

  /*
   * Returns the response from the HTTP request to the OAuth 2.0 server.
   */
  HttpResponse* http_response() const { return http_request_->response(); }

 protected:
  /*
   * Returns the HttpRequest used for the message to the OAuth 2.0 server.
   */
  HttpRequest* http_request() { return http_request_.get(); }

 private:
  scoped_ptr<HttpRequest> http_request_;
  DISALLOW_COPY_AND_ASSIGN(OAuth2TokenRequest);
};

/*
 * A concrete OAuth2TokenRequest for revoking access and refresh tokens.
 *
 * @deprecated Use OAuth2AuthorizatioFlow methods
 */
class OAuth2RevokeTokenRequest : public OAuth2TokenRequest {
 public:
  /*
   * Constructs the request.
   *
   * The caller retains ownership to all parameters. The instance will
   * be using these pointers so the caller must keep them valid over the
   * lifetime of the request.
   *
   * @param[in] transport Transport instance to use.
   * @param[in] client Client specification.
   * @param[in] token The token to revoke.
   */
  OAuth2RevokeTokenRequest(
      HttpTransport* transport,
      const OAuth2ClientSpec* client,
      ThreadsafeString* token);

  /*
   * Asks the OAuth 2.0 server to revoke the token.
   *
   * This will clear the token given the the constructor on success.
   */
  virtual util::Status Execute();

 private:
  const OAuth2ClientSpec* client_;
  ThreadsafeString* token_;
  DISALLOW_COPY_AND_ASSIGN(OAuth2RevokeTokenRequest);
};

/*
 * A concrete OAuth2TokenRequest for turning an authorization code
 * into access and refresh tokens.
 *
 * @deprecated Use OAuth2AuthorizatioFlow methods
 */
class OAuth2ExchangeAuthorizationCodeRequest : public OAuth2TokenRequest {
 public:
  /*
   * Constructs the request.
   *
   * The caller retains ownership to all parameters. The instance will
   * be using these pointers so the caller must keep them valid over the
   * lifetime of the request.
   *
   * @param[in] transport Transport instance to use.
   * @param[in] authorization_code The authorization code from the user action
   *            that authorized access.
   * @param[in] client Client specification.
   * @param[in] options Can override the redirect_uri from the one in
   *            the client parameter.
   * @param[in] credential The credential to update with the response tokens.
   */
  OAuth2ExchangeAuthorizationCodeRequest(
    HttpTransport* transport,
    const StringPiece& authorization_code,
    const OAuth2ClientSpec& client,
    const OAuth2RequestOptions& options,
    OAuth2Credential* credential);

  /*
   * Asks the OAuth 2.0 server for an initial Access Token and Refresh Token.
   *
   * This will update the credential given the constructor on success.
   */
  virtual util::Status Execute();

 private:
  OAuth2Credential* credential_;
  string content_;
  string token_uri_;
  DISALLOW_COPY_AND_ASSIGN(OAuth2ExchangeAuthorizationCodeRequest);
};

/*
 * A concrete OAuth2TokenRequest for obtaining a new access token
 * from a refresh token.
 *
 * @deprecated Use OAuth2AuthorizatioFlow methods
 */
class OAuth2RefreshTokenRequest : public OAuth2TokenRequest {
 public:
  /*
   * Constructs the request.
   *
   * The caller retains ownership to all parameters. The instance will
   * be using these pointers so the caller must keep them valid over the
   * lifetime of the request.
   *
   * @param[in] transport Transport instance to use.
   * @param[in] client Client specification.
   * @param[in] credential The credential to update with the response tokens.
   */
  OAuth2RefreshTokenRequest(
    HttpTransport* transport,
    const OAuth2ClientSpec* client,
    OAuth2Credential* credential);

  /*
   * Asks the OAuth 2.0 server for a new Access Token from the
   * existing Refresh Token
   *
   * This will update the credential given the constructor on success.
   */
  virtual util::Status Execute();

 private:
  const OAuth2ClientSpec* client_;
  OAuth2Credential* credential_;

  DISALLOW_COPY_AND_ASSIGN(OAuth2RefreshTokenRequest);
};

/*
 * Mediates interaction with the user, client and an OAuth 2.0 server to
 * obtain credentials for accessing protected resources.
 * @ingroup AuthSupportOAuth2
 *
 * This classs is based on a Mediator Pattern with the goal of
 * obtaining credentials to access resources protected by OAuth 2.0.
 *
 * This class is concrete, however is often subclassed for different
 * OAuth 2.0 flow patterns that may add additional specialized parameters
 * to their token requests. These are determined by the Client Type selection
 * in the <a href='https://code.google.com/apis/console/'>
 * Google APIs console</a> when the client was registered.
 *
 * To use this class you must first configure the client secrets needed
 * to issue requests to the OAuth 2.0 server on behalf of the client.
 * The easiest way to do this is to use the MakeFlowFromClientSecretsPath()
 * factory method to construct the flow or the InitFromJson() method if you
 * want to instantiate the flow yourself. Otherwise get the client spec using
 * mutable_client_spec().
 *
 * If a CredentialStore is bound to the flow then it will be used as a cache.
 * This is particularly useful for persisting refresh tokens across multiple
 * program executions. Without an existing refresh token the user will have
 * to re-authorize access all over again. With the store, the user will only
 * have to authorize access the first time the program is run.
 *
 * @see OAuth2Credential
 * @see OAuth2ClientSpec
 * @see MakeFlowFromClientSecretsPath
 * @see ResetCredentialStore
 */
class OAuth2AuthorizationFlow {
 public:
  /*
   * Responsible for getting an Authorization Code for the specified options.
   *
   * Note the Authorization Code is not an access token.
   *
   * @return util::Status is a failure if the authorization code could not
   *         be obtained, including if the user denies access.
   *
   * @param[in] OAuth2RequestOptions Specifies the scope and redirect_uri.
   * @param[out] string The authorization code if the call is successful.
   */
  typedef ResultCallback2< util::Status,
                           const OAuth2RequestOptions&, string*>
          AuthorizationCodeCallback;

  /*
   * Constructs the flow.
   *
   * @param[in] transport Transport to use when talking to the OAuth 2.0
   *            server. Ownership is passed to the flow.
   */
  explicit OAuth2AuthorizationFlow(HttpTransport* transport);

  /*
   * Standard destructor.
   */
  virtual ~OAuth2AuthorizationFlow();

  /*
   * Initializes the flow attributes from the JSON string.
   *
   * This includes the standard attributes for the client spec,
   * and any additional attributes for the specialization within the
   * flow. This method calls the protected InitFromJsonData() method
   * so that derived classes can initialize their specialized attributes
   * as well.
   *
   * @param[in] json JSON-encoded object with the configuraton attributes.
   */
  util::Status InitFromJson(const StringPiece& json);

  /*
   * Sets the callback used to obtain an Authorization Code.
   *
   * @param[in] callback Must be repeatable if not NULL. Ownership is
   *            passed to the flow.
   *
   * @see NewPermanentCallback()
   */
  void set_authorization_code_callback(AuthorizationCodeCallback* callback);

  /*
   * Returns the callback for obtaining Authorization Codes, or NULL
   * if it was not set.
   */
  AuthorizationCodeCallback* authorization_code_callback() const {
    return authorization_code_callback_.get();
  }

  /*
   * Sets the CredentialStore used by this flow.
   *
   * @param[in] store Ownership of the credential store is passed to the flow.
   *                  A NULL valid indicates that credentials should neither be
   *                  stored nor restored.
   */
  void ResetCredentialStore(CredentialStore* store);

  /*
   * Returne the bound credential store, if any.
   */
  CredentialStore* credential_store() const { return credential_store_.get(); }

  /*
   * Returns the client specification with the details needed for it to
   * use the OAuth 2.0 server.
   */
  const OAuth2ClientSpec& client_spec() const { return client_spec_; }

  /*
   * Returns a modificable client specification.
   */
  OAuth2ClientSpec* mutable_client_spec()     { return &client_spec_; }

  /*
   * Returns the default scopes to request when asking for Access Tokens.
   */
  const string& default_scopes() const    { return default_scopes_; }

  /*
   * Sets the default scopes to request when asking for Access Tokens.
   *
   * The OAuth2RequestOptions passed to various methods can be used to
   * override these on a per-request basis if needed. It is not required
   * to set this, but it is simpler if requests tend to use the same scopes.
   *
   * @param[in] scopes The specific values are specified by the individual
   *            services that you wish to talk to.
   */
  void set_default_scopes(const StringPiece& scopes) {
    default_scopes_ = scopes.as_string();
  }

  /*
   * Refreshes the credential with a current access token.
   *
   * @param options Clarifies details about the desired credentials.
   * <table><tr><th>Option<th>Purpose
   *        <tr><td>user_id
   *            <td>Only used as a key for the CredentialStore. If there is
   *                no store for the flow or this is empty then the call will
   *                proceed without using a CredentialStore. Some flows, such
   *                as the web flow, may also use this as a login_hint.
   *        <tr><td>scopes
   *            <td>Used to override the flow's default scope. This option
   *                is only required if the flow was not given a default.
   *        <tr><td>redirect_uri
   *            <td>Used to override the flow's default redirect_uri. This
   *                option is only required if the flow was not given a
   *                default.
   *  </table>
   *
   * @param[in] credential The credential to refresh can be empty. If it
   *            has already been initialized then the user_id, if any, must
   *            match that in the options.
   *
   * The a CredentialStore is to be used, then the flow will attempt to
   * read an existing credential from the store. Also, the flow will store
   * newly obtained or updated credentials into the store.
   *
   * When existing credentials are found in the store, it may already have a
   * valid access token. Otherwise it should have a refresh token from which
   * a new access token can be obtained. The flow will attempt to obtain
   * a valid access token and update the credential as needed.
   *
   * If there is no credential in the store, or the credential failed to update
   * then the flow will attempt to obtain a new Authorization Code, which will
   * require user interaction to approve. To obtain the Authorization Code,
   * the flow will call the AuthorizationCodeCallback, if one was bound.
   * If this code is obtained, the flow will use it to update the credential
   * with a valid Access Token and store the updated credential if
   * a store was configured.
   */
  // TODO(ewiseblatt): This currently only considers the scopes when the
  // credential has no refresh_token. If there is a refresh token, then the
  // updated credentials will still only be for the old scopes. To get around
  // this bug, you need to clear the credential and start all over.
  //
  virtual util::Status RefreshCredentialWithOptions(
      const OAuth2RequestOptions& options, OAuth2Credential* credential);

  /*
   * Returns a URL to the OAuth 2.0 server requesting a new Authorization Code.
   *
   * This method is only intended when manually implementing flows.
   *
   * @param[in] scopes The desired scopes. If more than one scope is desired
   *            then this should be a ' '-delimited string.
   *            The scope values are specified by the specific service you
   *            wish to get authorization to access.
   * @return The appropriate URL to HTTP GET.
   *
   * @see RefreshCredentialWithOptions.
   * @see GeenrateAuthorizationCodeRequestUrlWithOptions
   */
  string GenerateAuthorizationCodeRequestUrl(const StringPiece& scopes) const {
    OAuth2RequestOptions options;
    options.scopes = scopes.as_string();
    return GenerateAuthorizationCodeRequestUrlWithOptions(options);
  }

  /*
   * Variation of GenerateAuthorizationCodeRequestUrl that takes a vector of
   * scopes rather than a ' '-delmited string.
   *
   * @param[in] scopes The individual scope strings.
   *
   * @see GenerateAuthorizationCodeRequestUrl
   */
  string GenerateAuthorizationCodeRequestUrl(
      const vector<StringPiece>& scopes) const;

  /*
   * Returns a URL to the OAuth 2.0 server requesting a new Authorization Code.
   *
   * This method is only intended when manually implementing flows.
   *
   * @param[in] options Used to refine what is being requested.
   * @return The appropriate URL to HTTP GET.
   *
   * @see RefreshCredentialWithOptions.
   */
  virtual string GenerateAuthorizationCodeRequestUrlWithOptions(
      const OAuth2RequestOptions& options) const;

  /*
   * Talk to the OAuth 2.0 server to refresh the access token.
   *
   * @param[in] options  Overriden options, like the redirect_uri.
   * @param[in, out] credential Uses the refresh_token to update access_token.
   *
   * @return ok on success or reason for failure.
   */
  virtual util::Status PerformRefreshToken(
      const OAuth2RequestOptions& options, OAuth2Credential* credential);

  /*
   * Sends request to OAuth 2.0 server to obtain Access and Refresh tokens from
   * an Authorization Code.
   *
   * @param[in] authorization_code The Authorization Code.
   * @param[in] options Used to refine what is being requested.
   * @param[out] credential The credential to update with the tokens.
   * @return ok or reason for error.
   *
   * @see RefreshCredentialWithOptions.
   */
  virtual util::Status PerformExchangeAuthorizationCode(
      const string& authorization_code,
      const OAuth2RequestOptions& options,
      OAuth2Credential* credential);

  /*
   * Sends a request to the OAuth 2.0 server to revoke the credentials.
   *
   * @param[in] access_token_only If true then only revoke the access token
   *                              leaving the refresh token intact.
   * @param[in] credential The credential to update should contain the
   *            refresh and/or access tokens. The tokens will be cleared after
   *            the revoke request executes successfully.
   */
  virtual util::Status PerformRevokeToken(
      bool access_token_only, OAuth2Credential* credential);

  /*
   * Creates a new request for obtaining Access and Refresh tokens from
   * an Authorization Code.
   *
   * This method is only intended when manually implementing flows.
   *
   * @param[in] authorization_code The Authorization Code.
   * @param[out] credential The credential to update with the tokens.
   *
   * @see RefreshCredentialWithOptions.
   * @see PerformExchangeAuthorizationCode
   *
   * @deprecated
   */
  OAuth2TokenRequest* NewExchangeAuthorizationCodeRequest(
      const StringPiece& authorization_code,
      OAuth2Credential* credential) {
    return NewExchangeAuthorizationCodeRequestWithOptions(
        authorization_code, OAuth2RequestOptions(), credential);
  }

  /*
   * Creates a new request for obtaining Access and Refresh tokens from
   * an Authorization Code.
   *
   * This method is only intended when manually implementing flows.
   *
   * @param[in] authorization_code The Authorization Code.
   * @param[in] options Used to refine what is being requested.
   * @param[out] credential The credential to update with the tokens.
   *
   * @see RefreshCredentialWithOptions.
   * @see PerformExchangeAuthorizationCode
   *
   * @deprecated
   */
  OAuth2TokenRequest* NewExchangeAuthorizationCodeRequestWithOptions(
      const StringPiece& authorization_code,
      const OAuth2RequestOptions& options,
      OAuth2Credential* credential);

  /*
   * Creates a new request for obtaining an Access Token from an
   * existing Refresh Token.
   *
   * @param[in] credential The credential to update should already contain
   *            an existing refresh token. The credential will be updated
   *            with a new Access Token if the request is executed
   *            successfully.
   * @see RefreshCredentialWithOptions
   * @see PerformRefreshToken
   *
   * @deprecated
   */
  OAuth2TokenRequest* NewRefreshTokenRequest(OAuth2Credential* credential);

  /*
   * Creates a new request for revoking a Refresh Token so that it is no
   * longer valid.
   *
   * The user will have to go through the entire flow to obtain a new
   * Authorization Code if they want to use the credential again.
   *
   * @param[in] credential The credential to update should already contain
   *            an existing refresh token. The token will be cleared after
   *            the revoke request executes successfully.
   *
   * @see PerformRevokeToken
   *
   * @deprecated
   */
  OAuth2TokenRequest* NewRevokeRefreshTokenRequest(
      OAuth2Credential* credential);

  /*
   * Creates a new request for revoking an Access Token so that it is no
   * longer valid.
   *
   * @param[in] credential The credential to update should already contain
   *            an existing access token. The token will be cleared after
   *            the revoke request executes successfully.
   * @see PerformRevokeToken
   *
   * @deprecated
   */
  OAuth2TokenRequest* NewRevokeAccessTokenRequest(
      OAuth2Credential* credential);

  /*
   * The standard URL used for clients that do not have an HTTP server.
   // TODO(ewiseblatt): Better description.
   */
  static const char kOutOfBandUrl[];

  /*
   * The root URL for the standard OAuth 2.0 server used by the
   * Google Cloud Platform.
   */
  static const char kGoogleAccountsOauth2Url[];

  /*
   * Creates a new flow from a client secrets file.
   *
   * This method is a wrapper around MakeFlowFromClientSecretsJson
   * that reads the contents of a file and uess that as the JSON
   * paraemter to MakeFlowFromClientSecretsJson.
   *
   * As a security safeguard, the file must be read-only and not a symbolic
   * link.
   *
   * @param[in] path The path to a file containing the JSON-encoded secrets.
   * @param[in] transport The transport to use with the flow will be owned
   *            by the flow. If the flow could not be created then this
   *            transport will be destroyed.
   * @param[out] status Will explain the cause for failure.
   *
   * @return Ownership of a new authorization flow or NULL on failure.
   *
   * @see MakeFlowFromClientSecretsJson
   */
  static OAuth2AuthorizationFlow* MakeFlowFromClientSecretsPath(
      const StringPiece& path, HttpTransport* transport,
      util::Status* status);

  /*
   * Creates a new flow from a client secrets JSON document.
   *
   * The JSON document is a composite object whose name specifies the type
   * of flow to be created. For example
   * <pre>{
   *    "installed": {
   *       "client_id": "<deleted>.apps.googleusercontent.com",
   *       "client_secret": "<deleted>"
   *    }
   * }</pre>
   * where &lt;deleted&gt; were values created when the program was registered.
   *
   * @param[in] json The JSON encoded client secrets to configure the flow
   *            from. This can come from the Google APIs Console.
   * @param[in] transport The transport to use with the flow will be owned
   *            by the flow. If the flow could not be created then this
   *            transport will be destroyed.
   * @param[out] status Will explain the cause for failure.
   *
   * @return Ownership of a new authorization flow or NULL on failure.
   */
  static OAuth2AuthorizationFlow* MakeFlowFromClientSecretsJson(
      const StringPiece& json, HttpTransport* transport,
      util::Status* status);

  /*
   * Helper function to produce a ' '-delimited scopes string
   * from a vector of individual scopes.
   *
   * @param[in] scopes
   * @return scopes string for OAuth2RequestOptions
   */
  static string JoinScopes(const vector<StringPiece>& scopes);

  /*
   * Returns a new credential instance that will use this flow to refresh.
   *
   * @return Ownership of the credential is passed to the caller.
   *         The instance will be constructed with this flow bound to it
   *         to implement its refresh method. Therefore this flow must remain
   *         valid over the lifetime of the result or until you unbind the flow
   *         using OAuth2Credential::set_flow(NULL).
   */
  OAuth2Credential* NewCredential();

 protected:
  /*
   * The parser is currently private to the implementation because I do not
   * want to couple JsonCpp in the public API at this time.
   */
  friend class OAuth2Credential;
  class SimpleJsonData;

 protected:
  /*
   * Called by InitFromJson
   * @param[out] data The data to initialized.
   */
  virtual util::Status InitFromJsonData(const SimpleJsonData* data);

 private:
  OAuth2ClientSpec client_spec_;
  string default_scopes_;
  scoped_ptr<HttpTransport> transport_;
  scoped_ptr<CredentialStore> credential_store_;
  scoped_ptr<AuthorizationCodeCallback> authorization_code_callback_;

  DISALLOW_COPY_AND_ASSIGN(OAuth2AuthorizationFlow);
};

/*
 * Manages credentials using OAuth 2.0 installed application flow.
 * @ingroup AuthSupportOAuth2
 *
 * This is a specialization of OAuth2Authorization flow.
 *
 * It does not add anything beyond that which is already in the base class,
 * however this will be the class created for "installed" application flows
 * for future maintainability.
 */
class OAuth2InstalledApplicationFlow : public OAuth2AuthorizationFlow {
 public:
  explicit OAuth2InstalledApplicationFlow(
      HttpTransport* transport);
  virtual ~OAuth2InstalledApplicationFlow();
  virtual string GenerateAuthorizationCodeRequestUrl(
      const StringPiece& scope) const;

 protected:
  // Called by initFromJson.
  virtual util::Status InitFromJsonData(const SimpleJsonData* data);

 private:
  DISALLOW_COPY_AND_ASSIGN(OAuth2InstalledApplicationFlow);
};


/*
 * Manages credentials using OAuth 2.0 web application flow.
 * @ingroup AuthSupportOAuth2
 *
 * This is a specialization of OAuth2Authorization flow.
 *
 * The web application flow adds the "approval_prompt" and "access_type"
 * attributes.
 */
class OAuth2WebApplicationFlow : public OAuth2AuthorizationFlow {
 public:
  explicit OAuth2WebApplicationFlow(HttpTransport* transport);
  virtual ~OAuth2WebApplicationFlow();
  virtual string GenerateAuthorizationCodeRequestUrl(
      const StringPiece& scope) const;

  /*
   * Returns whether or not the "approval_prompt" should be 'force'.
   */
  bool force_approval_prompt() const { return force_approval_prompt_; }

  /*
   * Used to set the approval_prompt attribute.
   *
   * @param[in] force If true then approval_prompt will be "force".
   *                  Otherwise it will be the default ("auto").
   */
  void set_force_approval_prompt(bool force) {
    force_approval_prompt_ = force;
  }

  /*
   * Returns whether the "access_type" attribute should be 'offline'.
   */
  bool offline_access_type() const { return offline_access_type_; }

  /*
   * Used to set the access_type attribute.
   *
   * @param[in] offline If true then access_type will be "offline".
   *                    Otherwise it will be the default ("online").
   */
  void set_offline_access_type(bool offline) {
    offline_access_type_ = offline;
  }

 protected:
  /*
   * Intitializes from the JSON data, including web-flow specific attributes.
   *
   * @param[in] data The json data specifying the flow.
   */
  virtual util::Status InitFromJsonData(const SimpleJsonData* data);

 private:
  bool offline_access_type_;
  bool force_approval_prompt_;

  DISALLOW_COPY_AND_ASSIGN(OAuth2WebApplicationFlow);
};

}  // namespace client

} // namespace googleapis
#endif  // APISERVING_CLIENTS_CPP_AUTH_OAUTH2_AUTHORIZATION_H_

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


// This sample shows different ways to use OAuth2 webflow.
// Usage:
//    You need a client registered for hostname():port
//
//    sample_webflow --logtostderr --client_secrets_path=...  --port=...
//
//    The --gplus_login determines whether this example will use
//    gplus_login (generally recommended) or a server-side mechanism
//    implemented by the sample for experimental and illustrative purposes.
//
//  When it is running, you can run the following URLs from one or more
//  browsers with different users
//
//  login   to get credentials
//  me      to see who you are (requires authentication already)
//  revoke  to revoke access tokens
//  quit    to quit
//
//
//  When pages are unauthorized the server will redirect to the login page
//  then redirect back.
//  The direct login page redirects to itself on success (as a welcome page).
//  The revoke page redirects to the login page (as a not-logged in page).
//
//  For example try the following sequence
//     me will redirect to login
//     login will redirect back to me
//     revoke will redirect to login
//     login will redirect to itself (as welcome)
//     me (will show private details)
//     revoke will logout again
//     quit

#include <stdlib.h>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
#include <string>
using std::string;

#include "samples/abstract_gplus_login_flow.h"
#include "samples/abstract_webserver_login_flow.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/auth/oauth2_pending_authorizations.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/jsoncpp_data.h"
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/mongoose_webserver.h"
#include "googleapis/client/util/status.h"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "googleapis/base/mutex.h"
#include "googleapis/strings/split.h"
#include "googleapis/strings/strip.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/util.h"

namespace googleapis {


DEFINE_bool(gplus_login,
            false,
            "Use Google+ Sig-In button if true."
            " By default it will use webserver_login");

DEFINE_int32(port, 8080,
             "The port to listen on must be registered"
             " for this host with the Google APIs Console"
             " for the service described in client_secrets_path");

DEFINE_string(client_secrets_path, "",
              "REQUIRED: Path to JSON client_secrets file for OAuth 2.0."
              " This can be downloaded comes from the Google APIs Console"
              " when you register this client application."
              " See //https://code.google.com/apis/console");

using sample::AbstractGplusLoginFlow;
using sample::AbstractWebServerLoginFlow;
using sample::WebServerRequest;
using sample::WebServerResponse;

using client::CurlHttpTransportFactory;
using client::HttpTransportLayerConfig;
using client::HttpRequest;
using client::HttpResponse;
using client::HttpStatusCode;
using client::HttpTransport;
using client::HttpTransportOptions;
using client::MongooseWebServer;
using client::OAuth2AuthorizationFlow;
using client::OAuth2PendingAuthorizations;
using client::OAuth2Credential;
using client::OAuth2RequestOptions;
using client::StatusOk;
using client::StatusUnknown;

/*
 * URL to get user info for login confirmation.
 */
const char kMeUrl[] = "https://www.googleapis.com/userinfo/v2/me";

/*
 * URL query parameter used for redirect urls.
 */
const char kLoginRedirectQueryParam[] = "redirect_uri";

/*
 * The OAuth scopes we'll ask for.
 */
const char kDefaultScopes[] =
    "https://www.googleapis.com/auth/userinfo.profile";

/*
 * We'll use this cookie to remember our cookie_id
 */
const char kCookieName[] = "SampleWorkflow";


class SampleWebApplication;

/*
 * Stores application user data.
 * @ingroup Samples
 *
 * Each user will store their credentials here.
 * We'll also track the user name to confirm login.
 */
class UserData {
 public:
  /*
   * Default constructor for new users.
   */
  UserData() {
    char tmp[40];
    std::snprintf(tmp, sizeof(tmp), "%08lx%08lx%08lx%08lx",
                  random(), random(), random(), random());
    cookie_id_ = tmp;
  }

  /*
   * Standard constructor for returning users.
   *
   * @param[in] cookie_id  Our user id was handed out as a cookie in an
   *                      earlier response
   */
  explicit UserData(const string& cookie_id) : cookie_id_(cookie_id) {
  }

  /*
   * Standard destructor.
   */
  virtual ~UserData() {}

  /*
   * Returns the cookie_id bound by the constructor.
   */
  const string& cookie_id() const { return cookie_id_; }

  /*
   * Returns the Google Account id learned at login.
   */
  const string& gid() const       { return gid_; }

  /*
   * Sets the google account id for confirming future logins are not
   * reusing a cookie designating a different user.
   */
  void set_gid(const string& id)  { gid_ = id; }

  /*
   * Returns the real user's name.
   */
  const string& user_name() const          { return user_name_; }

  /*
   * Sets the real user's name, presumably from their Google Account.
   */
  void set_user_name(const string& name)   { user_name_ = name; }

  /*
   * Returns the credential for this user.
   *
   * @return NULL if the user is not actively logged in.
   */
  OAuth2Credential* credential() { return credential_.get(); }

  /*
   * Resets the credential.
   *
   * @param[in] cred Should contain an access token. Ownership passed.
   */
  void ResetCredential(OAuth2Credential* cred) { credential_.reset(cred); }

 private:
  /*
   * We're storing the cookie_id for logging and convienience.
   * We dont really need it here, just in repository mapping to this instance.
   * However since we want to return it as a cookie, it is convienent to
   * store here rather than propagating another parameter around.
   */
  string cookie_id_;  //< Our cookie_id (cookie value).
  string user_name_;  //< Real user name (for confirming login).
  string gid_;        //< Google account id (for confirmation).

  std::unique_ptr<OAuth2Credential> credential_;  //< NULL when not logged in.
};

/*
 * Repository managing UserData.
 * @ingroup Samples
 *
 * For purposes of this application we arent creating real users.
 * Instead we're treating the session as our user. We'll do extra work
 * to try to distinguish the current Google Account in the browser when
 * using the Google+ Login button (flag) but normally you would have your
 * own application user and tie that to a specific Google Account from the
 * authorization code when the user gave access.
 */
class UserRepository {
 public:
  /*
   * Standard constructor.
   *
   * @param[in] Transport to use for fetching user profile information.
   *            Ownership is passed.
   * @param[in] verify_gid  If true we should verify the credentials by
   *                        fetch Google Account id and comparing it to
   *                        previous.
   *
   * TODO(user): 20130608
   * The means of verifying the user hers is not quite right. I think one
   * would normally use the id_token already returned from the g+ button
   * but I dont have support for the JWT decoding yet. I'm not sure how to
   * use CSRF tokens either. So we'll brute force verify the underlying acocunt
   * id's are consistent when we are pushed access tokens for a user. With a
   * more elaborate user model we could use these as the keys into our users
   * to be independent of the cookies, which could change over time and
   * across machines. Most real sites woudl have their own login and
   * independent user accounts. Here we are piggy backing off the google
   * account for simplicitly, but perhaps it is that which is causing confusion
   * when switching accounts within the same browser.
   */
  explicit UserRepository(HttpTransport* transport, bool verify_gid)
      : transport_(transport), verify_gid_(verify_gid) {
  }

  /*
   * Standard destructor.
   */
  ~UserRepository() {
    MutexLock l(&mutex_);

    // Cleanup the repository.
    //
    // We're deleting our user entries here, but leaving the access tokens
    // valid to expire.
    auto begin = repository_.begin();
    auto end = repository_.end();
    while (begin != end) {
      auto temp = begin;
      ++begin;
      delete temp->second;
    }
  }

  /*
   * Returns UserData instance for the given cookie_id.
   *
   * @parma[in] cookie_id NULL for a new user,
   *            otherwise cookie_id from earlier UserData.
   *            This is persistent so the UserData might not be in memory.
   * @return UserData managing the given cookie_id.
   */
  UserData* GetUserDataFromCookieId(const string& cookie_id) {
    UserData* user_data;

    MutexLock l(&mutex_);
    if (!cookie_id.empty()) {
      std::map<string, UserData*>::iterator it = repository_.find(cookie_id);
      if (it != repository_.end()) {
        VLOG(1) << "Already have UserData for cookie=" << cookie_id;
        return it->second;
      }
      VLOG(1) << "Creating new UserData for existing cookie=" << cookie_id;
      user_data = new UserData(cookie_id);
    } else {
      user_data = new UserData;
      VLOG(1) << "Creating new UserData. cookie=" << user_data->cookie_id();
    }

    repository_.insert(std::make_pair(user_data->cookie_id(), user_data));
    return user_data;
  }

  googleapis::util::Status GetPersonalUserData(
       OAuth2Credential* cred, client::JsonCppDictionary* dict) {
    CHECK(cred != NULL);
    std::unique_ptr<HttpRequest> request(
        transport_->NewHttpRequest(HttpRequest::GET));
    request->set_credential(cred);
    request->set_url(kMeUrl);
    googleapis::util::Status status = request->Execute();
    if (!status.ok()) {
      LOG(ERROR)
          << "Failed invoking " << kMeUrl << status.error_message();
      return status;
    }

    status = dict->LoadFromJsonReader(request->response()->body_reader());
    // These are expected results from the URL we invoked.
    if (!dict->has("name")) {
      return StatusUnknown("name is missing!");
    }
    if (!dict->has("id")) {
      return StatusUnknown("id is missing!");
    }
    return StatusOk();
  }

  /*
   * Adds credential for the user with the given_id.
   *
   * Creates the application user if it was not previously known.
   *
   * @param[in] cookie_id User to update.
   * @param[in] status ok if we have a credential, otherwise the failure.
   * @param[in] credential The credential to update, or NULL.
   */
  bool AddCredential(
      const string& cookie_id,
      const googleapis::util::Status& status,
      OAuth2Credential* credential) {
    std::unique_ptr<OAuth2Credential> credential_deleter(credential);
    if (!status.ok()) {
      LOG(WARNING) << "Did not get credential for cookie="
                   << cookie_id << ": " << status.error_message();
      return false;
    }

    UserData* user_data = GetUserDataFromCookieId(cookie_id);
    bool new_user = user_data->gid().empty();

    if (new_user) {
      client::JsonCppCapsule<
          client::JsonCppDictionary> capsule;
      googleapis::util::Status status = GetPersonalUserData(credential, &capsule);
      if (!status.ok()) {
        LOG(ERROR) << "Could not get user data so removing user.";
        RemoveUser(cookie_id);
        return false;
      }
      user_data->set_user_name(capsule.as_value("name").asCString());
      user_data->set_gid(capsule.as_value("id").asCString());
      user_data->ResetCredential(credential_deleter.release());
    } else if (verify_gid_) {
      // If the access tokens are the same then we're good.
      string old_access_token;
      OAuth2Credential* old_credential = user_data->credential();
      if (old_credential) {
        old_access_token = old_credential->access_token().as_string();
      }
      string new_access_token = credential->access_token().as_string();
      if (old_access_token != new_access_token) {
        // If they are different, look for the underlying gid and see if
        // the user just refreshed the token. If not, swap out our user record.
        // Otherwise just keep the new credential.
        client::JsonCppCapsule<
            client::JsonCppDictionary> capsule;
        googleapis::util::Status status = GetPersonalUserData(credential, &capsule);
        if (!status.ok()) {
          LOG(ERROR) << "Could not get user data so removing user.";
          RemoveUser(cookie_id);
          return false;
        }
        string gid = capsule.as_value("id").asCString();
        if (user_data->gid() != gid) {
          if (!user_data->gid().empty()) {
            LOG(WARNING) << "It appears user changed so swapping records.";
            RemoveUser(cookie_id);
            user_data = GetUserDataFromCookieId(cookie_id);
          }
          user_data->set_user_name(capsule.as_value("name").asCString());
          user_data->set_gid(gid);
          new_user = true;
        }
        user_data->ResetCredential(credential_deleter.release());
      }
    } else {
      // If we arent verifying the user, just swap the credential
      user_data->ResetCredential(credential_deleter.release());
    }
    return !new_user;
  }

  void RemoveUser(const string& cookie_id) {
    MutexLock l(&mutex_);
    std::map<string, UserData*>::iterator found = repository_.find(cookie_id);
    if (found != repository_.end()) {
      delete found->second;
      repository_.erase(found);
    }
  }

 private:
  std::unique_ptr<HttpTransport> transport_;   //< For getting user info.
  bool verify_gid_;         //< Whether to verify gid
  Mutex mutex_;            //< Protects repository
  std::map<string, UserData*> repository_ GUARDED_BY(mutex_);
  DISALLOW_COPY_AND_ASSIGN(UserRepository);
};

template<class BASE>
class SampleWebApplicationLoginFlow : public BASE {
 public:
  /*
   * Standard constructor.
   * @param[in] flow The OAuth 2.0 flow for getting and revoking
   *                 Access Tokens. The caller retains ownership.
   * @param[in] sample_app The flow will delegate the various page responses
   *                       to the application object itself.
   *                       The caller retains ownership.
   * @param[in] repository The flow will interact with the repository to
   *                       manage credentials. The caller retains ownership.
   */
  SampleWebApplicationLoginFlow(
      OAuth2AuthorizationFlow* flow,
      SampleWebApplication* sample_app,  // for shared response handling.
      UserRepository* repository)
    : BASE(kCookieName, kLoginRedirectQueryParam, flow),
      sample_app_(sample_app),
      user_repository_(repository) {
  }

  /*
   * Standard destructor.
   */
  virtual ~SampleWebApplicationLoginFlow() {}

  SampleWebApplication* sample_app() const { return sample_app_; }
  UserRepository* user_repository()  const { return user_repository_; }

 protected:
  /*
   * Hook for the base login flow to update credentials in the registry.
   * @param[in] cookie_id The application's user id being managed
   * @param[in] status If not ok then this is the error explaining a failure.
   * @param[in] credential If NULL the credential was revoked,
   *            otherwise ownership is passed.
   */
  virtual bool DoReceiveCredentialForCookieId(
      const string& cookie_id,
      const googleapis::util::Status& status,
      OAuth2Credential* credential) {
    if (credential) {
      return user_repository_->AddCredential(cookie_id, status, credential);
    } else {
      user_repository_->RemoveUser(cookie_id);
      return true;
    }
  }

  /*
   * Hook for the base login flow to get credentials for a given user.
   *
   * @return NULL if the user isnt actively logged in.
   */
  virtual OAuth2Credential* DoGetCredentialForCookieId(
      const string& cookie_id) {
    return user_repository_->GetUserDataFromCookieId(cookie_id)->credential();
  }

  /*
   * Hook for the base login flow to render a welcome page after login.
   */
  virtual googleapis::util::Status DoRespondWithWelcomePage(
      const string& cookie_id, WebServerRequest* request);

  /*
   * Hook for the base login flow to render a login page.
   */
  virtual googleapis::util::Status DoRespondWithNotLoggedInPage(
      const string& cookie_id, WebServerRequest* request);

  /*
   * Hook for the base login flow to render a login failure page.
   */
  virtual googleapis::util::Status DoRespondWithLoginErrorPage(
      const string& cookie_id, const googleapis::util::Status& status,
      WebServerRequest* request);

 private:
  SampleWebApplication* sample_app_;
  UserRepository* user_repository_;

  DISALLOW_COPY_AND_ASSIGN(SampleWebApplicationLoginFlow);
};

typedef SampleWebApplicationLoginFlow<AbstractWebServerLoginFlow>
        SampleMicroLoginFlow;
typedef SampleWebApplicationLoginFlow<AbstractGplusLoginFlow>
        SampleGplusLoginFlow;


/*
 * Our sample application just illustrates logging in to access
 * a protected page.
 * @defgroup Samples
 *
 * All protected pages redirect to the Login page when the user lacks
 * credentials.
 *
 * This class uses two separate login flow implementations for purposes of
 * illustration of two different approaches, their similarities and
 * differences.
 */
class SampleWebApplication {
 public:
  /*
   * Standard constructor also initializes application.
   */
  SampleWebApplication() {
    httpd_.reset(new MongooseWebServer(FLAGS_port));
    bool gplus_login = FLAGS_gplus_login;

    InitTransportLayer();
    InitAuthorizationFlow();
    user_repository_.reset(
        new UserRepository(config_->NewDefaultTransportOrDie(), true));
    InitLoginFlow(gplus_login);

    // Add this last (after the login flow) so it has lower-prcedence.
    httpd_->AddPathHandler(
        "/",
        NewPermanentCallback(this,
                             &SampleWebApplication::HandleDefaultUrls));

    googleapis::util::Status status = httpd_->Startup();
    CHECK(status.ok()) << status.error_message();
  }

  /*
   * Standard destructor.
   */
  ~SampleWebApplication() {
  }

  /*
   * Blocks this thread until the "quit" url signals it.
   *
   * The WebServer will use its own threads to process queriess.
   */
  void Run() {
    MutexLock l(&mutex_);
    condvar_.Wait(&mutex_);
  }

  /*
   * Returns the UserData instance for the given request.
   *
   * Creates a new instance if one was not known already.
   * @param[in] request
   */
  UserData* GetUserData(WebServerRequest* request) {
    string cookie_id;
    request->GetCookieValue(kCookieName, &cookie_id);  // Empty if not there.
    return user_repository_->GetUserDataFromCookieId(cookie_id);
  }

  /*
   * Sends the welcome page back after user logs in.
   *
   * This is broken out so we can share it among the different
   * login mechanisms that the sample demonstrates.
   */
  googleapis::util::Status RespondWithWelcomePage(
       UserData* user_data, WebServerRequest* request) {
    return RespondWithHtml(user_data, HttpStatusCode::OK, "Welcome!", request);
  }

  /*
   * Sends the "not logged in" page when user has no credentials.
   *
   * This is broken out so we can share it among the different
   * login mechanisms that the sample demonstrates.
   */
  googleapis::util::Status RespondWithNotLoggedInPage(
       UserData* user_data, WebServerRequest* request) {
    string redirect_url;
    request->parsed_url().GetQueryParameter(
        kLoginRedirectQueryParam, &redirect_url);
    return RespondWithHtml(
        user_data, HttpStatusCode::OK, "You must first log in.",
        request, redirect_url);
  }

  /*
   * Sends the "login error" page when user has failed to login.
   *
   * This is broken out so we can share it among the different login
   * mechanisms that the sample demonstrates.
   */
  googleapis::util::Status RespondWithLoginErrorPage(
      UserData* user_data,
      const googleapis::util::Status& status,
      WebServerRequest* request) {
    return RespondWithHtml(
        user_data,
        HttpStatusCode::UNAUTHORIZED,
        StrCat("Login error: ", status.error_message()),
        request);
  }

 private:
  /*
   * Helper function for initializing the transport layer.
   */
  void InitTransportLayer() {
    config_.reset(new client::HttpTransportLayerConfig);
    client::HttpTransportFactory* factory =
        new client::CurlHttpTransportFactory(config_.get());
    config_->ResetDefaultTransportFactory(factory);
    config_->mutable_default_transport_options()->set_cacerts_path(
        HttpTransportOptions::kDisableSslVerification);
    transport_.reset(config_->NewDefaultTransportOrDie());
  }

  /*
   * Helper function for initializing the OAuth 2.0 authorization flow.
   */
  void InitAuthorizationFlow() {
    CHECK(config_.get()) << "Must InitTransportLayer first";

    googleapis::util::Status status;
    flow_.reset(OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath(
        FLAGS_client_secrets_path,
        config_->NewDefaultTransportOrDie(),
        &status));
    CHECK(status.ok()) << status.error_message();
    flow_->mutable_client_spec()->set_redirect_uri(
        httpd_->MakeEndpointUrl(false, "/oauth"));
    flow_->set_default_scopes(kDefaultScopes);
  }

  /*
   * Helper function for initializing the login component for our application.
   *
   * @param[in] use_gplus_login If true use Google+ Sign-in button,
   *                            otherwise do it directly within our server.
   */
  void InitLoginFlow(bool use_gplus_login) {
    CHECK(httpd_.get()) << "Must initialize httpd_ first";
    CHECK(flow_.get()) << "Must InitAuthorizationFlow first";
    CHECK(user_repository_.get()) << "Must initialize user_repository_ first";

    if (use_gplus_login) {
      gplus_login_.reset(
          new SampleGplusLoginFlow(flow_.get(), this, user_repository_.get()));
      gplus_login_->set_client_id(flow_->client_spec().client_id());
      gplus_login_->set_scopes(kDefaultScopes);
      gplus_login_->set_log_to_console(true);
      gplus_login_->AddLoginUrl("/login", httpd_.get());
      gplus_login_->AddLogoutUrl("/revoke", httpd_.get());
      gplus_login_->AddReceiveAccessTokenUrl("/oauth", httpd_.get());
    } else {
      login_.reset(
          new SampleMicroLoginFlow(flow_.get(), this, user_repository_.get()));
      login_->AddLoginUrl("/login", httpd_.get());
      login_->AddLogoutUrl("/revoke", httpd_.get());
      login_->AddReceiveAccessTokenUrl("/oauth", httpd_.get());
    }
  }

  /*
   * Helper function for returning pages when using Google+ Sign-in button.
   *
   * @param[in] user_data The user data this page is for isnt always logged in.
   * @param[in] request The request we are responding to.
   * @param[in] redirect_success If non-empty then redirect here after login.
   *
   * @return HTML page to return will have $MSG_BODY to resolve.
   */
  string MakeGplusPageTemplate(
      UserData* user_data,
      WebServerRequest* request,
      const string& redirect_success) {
    const string generic_template =
        "<html><head>\n$GOOGLE_PLUS_HEAD\n</head><body>\n"
        "$GOOGLE_PLUS_BUTTON\n"
        "<div>$USER_IDENTITY $LOGIN_CONTROL</div>\n"
        "<div id='msg_body'>$MSG_BODY</div>\n"
        "</body></html>\n";
    const string& cookie_id = user_data->cookie_id();
    string html;

    string success_block;
    string redirect_url = redirect_success;
    if (!user_data->credential() && redirect_success.empty()) {
      redirect_url ="/login";
    }
    if (!redirect_url.empty()) {
      success_block = StrCat("window.location='", redirect_url, "'");
    }
    string failure_block = "window.location='/login?error=' + error";
    string immediate_block;
    if (user_data->credential()) {
      immediate_block = StrCat("window.location.replace='",
                               request->parsed_url().url(),
                               "'");
    }
    string gplus_head = StrCat(
        gplus_login_->GetPrerequisiteHeadHtml(),
        gplus_login_->GetSigninCallbackJavascriptHtml(
            cookie_id, immediate_block, success_block, failure_block));
    html = StringReplace(
        generic_template, "$GOOGLE_PLUS_HEAD", gplus_head, false);

    if (user_data->credential()) {
      html = StringReplace(html, "$LOGIN_CONTROL",
                           "(<a href='/revoke'>Logout</a>)", false);
    } else {
      html = StringReplace(html, "$LOGIN_CONTROL", "", false);
    }

    // Dont show button by default.
    string gplus_button = gplus_login_->GetSigninButtonHtml(false);
    return StringReplace(
        html, "$GOOGLE_PLUS_BUTTON", gplus_button, false);
  }

  /*
   * Helper function for returning pages when using in-application login.
   *
   * @param[in] user_data The user data this page is for isnt always logged in.
   * @param[in] request The request we are responding to.
   *
   * @return HTML page to return will have
   *         $MSG_BODY and $LOGIN_CONTROL to resolve.
   */
  string MakeWebServerLoginPageTemplate(
      UserData* user_data, WebServerRequest* request) {
    const string generic_template =
        "<html><body>\n"
        "<div>$USER_IDENTITY ($LOGIN_CONTROL)</div>\n"
        "$MSG_BODY\n"
        "</body></html>\n";
    string html;

    string escaped_login = client::EscapeForUrl("/login");
    string redirect_to_login =
        StrCat(kLoginRedirectQueryParam, "=", escaped_login);
    if (user_data->credential()) {
      html = StringReplace(
          generic_template, "$LOGIN_CONTROL",
          StrCat("<a href='/revoke?", redirect_to_login, "'>Logout</a>"),
          false);
    } else {
      html = StringReplace(
          generic_template, "$LOGIN_CONTROL",
          StrCat("<a href='/login?", redirect_to_login, "'>Login</a>"),
          false);
    }
    return html;
  }

  /*
   * Responds to request with a [temporary] redirect.
   *
   * @param[in] user_data Contains this user's id for a cookie.
   * @param[in] url The url to redirect to.
   * @param[in] request The request we are responding to.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status RespondWithRedirect(
      UserData* user_data, const string& url, WebServerRequest* request) {
    LOG(INFO) << "Redirecting cookie=" << user_data->cookie_id()
              << " to " << url;

    WebServerResponse* response = request->response();
    googleapis::util::Status status =
        response->AddCookie(kCookieName, user_data->cookie_id());
    if (!status.ok()) {
      LOG(ERROR) << "Embedded webserver coudlnt add a cookie when redirecting:"
                 << status.error_message();
      // We'll still do the redirect though.
    }

    return response->SendRedirect(307, url);
  }

  /*
   * Responds to request with an HTML page.
   *
   * We're going to wrap the appliation's response with the login control.
   *
   * @param[in] user_data Contains information confirming login and add cookie.
   * @param[in] http_code The HTTP code to respond with.
   * @param[in] html_body The body of the page we're returning.
   * @param[in] request The request we are responding to.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status RespondWithHtml(
      UserData* user_data, int http_code, const string& html_body,
      WebServerRequest* request, string redirect_success = "") {
    string html = gplus_login_.get()
        ? MakeGplusPageTemplate(user_data, request, redirect_success)
        : MakeWebServerLoginPageTemplate(user_data, request);

    if (user_data->credential()) {
      const string& user_name = user_data->user_name();
      string identity = StrCat("Logged in as <b>", user_name, "</b>");
      html = StringReplace(html, "$USER_IDENTITY", identity, false);
    } else if (!gplus_login_.get()) {
      html = StringReplace(html, "$USER_IDENTITY",
                           "<b>Not logged in</b>", false);
    } else {
      html = StringReplace(html, "$USER_IDENTITY", "", false);
    }
    html = StringReplace(html, "$MSG_BODY", html_body, false);

    WebServerResponse* response = request->response();
    googleapis::util::Status status =
        response->AddCookie(kCookieName, user_data->cookie_id());
    if (!status.ok()) {
      LOG(ERROR) << "Embedded webserver couldnt add a cookie. "
                 << status.error_message();
      // We'll still allow the request to continue though.
    }

    return response->SendHtml(http_code, html);
  }

  /*
   * Responds to /quit URL by quitting the server.
   *
   * @param[in] request The request we are responding to.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status ProcessQuitCommand(WebServerRequest* request) {
    MutexLock l(&mutex_);
    condvar_.Signal();
    return request->response()->SendText(
        HttpStatusCode::OK, "Terminated server.");
  }

  /*
   * Responds to /me URL by displaying protected user data.
   *
   * @param[in] user_data Contains information to confirm login and add cookie.
   *                      It is assumed that the credential is already valid.
   * @param[in] request The request we are responding to.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status ProcessMeCommand(
      UserData* user_data, WebServerRequest* request) {
    std::unique_ptr<HttpRequest> http_request(
        transport_->NewHttpRequest(HttpRequest::GET));
    http_request->set_url(kMeUrl);
    http_request->set_credential(user_data->credential());
    http_request->Execute().IgnoreError();

    int http_code = http_request->response()->http_code();
    string msg;
    if (http_code) {
      msg = http_request->response()->body_reader()->RemainderToString();
    } else {
      msg = StrCat("Could not execute: ",
                   http_request->state().status().error_message());
    }
    return RespondWithHtml(user_data, http_code, msg, request);
  }

  /*
   * Handles all the non-oauth callback urls int our sample application.
   *
   * @param[in] request The request we are responding to.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status HandleDefaultUrls(WebServerRequest* request) {
    VLOG(1) << "Default url handler=" << request->parsed_url().url();
    // Strip leading "/" to get the command.
    string command = request->parsed_url().path().substr(1);
    if (command == "favicon.ico") {
      VLOG(1) << "Ignoring request=" << request->parsed_url().url();
      return request->response()->SendText(HttpStatusCode::NOT_FOUND, "");
    } else if (command == "quit") {
      return ProcessQuitCommand(request);
    }

    UserData* user_data = GetUserData(request);
    if (!user_data->credential()) {
      VLOG(1) << "No credential for " << user_data->cookie_id()
              << " so redirect";
      string encoded_url =
          client::EscapeForUrl(request->parsed_url().url());
      return RespondWithRedirect(
          user_data,
          StrCat("/login?", kLoginRedirectQueryParam, "=", encoded_url),
          request);
    }

    if (command == "me") {
      return ProcessMeCommand(user_data, request);
    }

    string msg = "Unrecognized command.";
    return RespondWithHtml(
        user_data, HttpStatusCode::NOT_FOUND, msg, request);
  }

  std::unique_ptr<MongooseWebServer> httpd_;
  std::unique_ptr<HttpTransport> transport_;
  std::unique_ptr<OAuth2AuthorizationFlow> flow_;
  std::unique_ptr<client::HttpTransportLayerConfig> config_;
  std::unique_ptr<UserRepository> user_repository_;

  // We'll be using one or the other of these depending on
  // FLAGS_gplus_login.
  std::unique_ptr<SampleMicroLoginFlow> login_;
  std::unique_ptr<SampleGplusLoginFlow> gplus_login_;

  Mutex mutex_;
  CondVar condvar_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(SampleWebApplication);
};


template<class C>
util::Status SampleWebApplicationLoginFlow<C>::DoRespondWithWelcomePage(
    const string& cookie_id, WebServerRequest* request) {
  UserData* user_data =
      user_repository_->GetUserDataFromCookieId(cookie_id);
  return sample_app_->RespondWithWelcomePage(user_data, request);
}

template<class C>
util::Status SampleWebApplicationLoginFlow<C>::DoRespondWithNotLoggedInPage(
    const string& cookie_id, WebServerRequest* request) {
  UserData* user_data =
      user_repository_->GetUserDataFromCookieId(cookie_id);
  return sample_app_->RespondWithNotLoggedInPage(user_data, request);
}

template<class C>
util::Status SampleWebApplicationLoginFlow<C>::DoRespondWithLoginErrorPage(
    const string& cookie_id,
    const googleapis::util::Status& status,
    WebServerRequest* request) {
  UserData* user_data =
      user_repository()->GetUserDataFromCookieId(cookie_id);
  return sample_app()->RespondWithLoginErrorPage(user_data, status, request);
}

}  // namespace googleapis

using namespace googleapis;
int main(int argc, char* argv[]) {
google::ParseCommandLineFlags(&argc, &argv, true);

  SampleWebApplication app;

  // Wait until "/quit" url is hit.
  app.Run();

  return 0;
}

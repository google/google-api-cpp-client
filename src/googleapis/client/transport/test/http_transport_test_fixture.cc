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


#ifndef _MSC_VER
#include <signal.h>
#endif

#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include <memory>
#include <string>
using std::string;
#include <vector>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/data/file_data_writer.h"
#include "googleapis/client/transport/html_scribe.h"
#include "googleapis/client/transport/json_scribe.h"
#include "googleapis/client/transport/test/http_transport_test_fixture.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/client/util/status.h"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"
#include <gtest/gtest.h>
#include "googleapis/util/executor.h"
#include "googleapis/util/status.h"
#include "google/wax_api/wax_api.h"

DEFINE_bool(allow_503, false,
            "Abort test when seeing a 503, but consider it passing.\n"
            "This is to avoid wax flakiness from causing my tests to fail.");

DEFINE_string(http_scribe_path, "",
              "If non-null then scribe messages to this file");

DEFINE_string(wax_root_url,
              "http://localhost:5000",
              "URL for Wax Service server.");

DEFINE_string(wax_service_path,
              "/",
              "URL path to Wax Service root.");

DEFINE_bool(fork_wax, true, "Fork a local wax server to talk to.");


using google_wax_api::ItemsResource_DeleteMethod;
using google_wax_api::ItemsResource_GetMethod;
using google_wax_api::ItemsResource_InsertMethod;
using google_wax_api::ItemsResource_ListMethod;
using google_wax_api::ItemsResource_PatchMethod;
using google_wax_api::ItemsResource_UpdateMethod;
using google_wax_api::SessionsResource_NewSessionMethod;
using google_wax_api::SessionsResource_RemoveSessionMethod;
using google_wax_api::WaxDataItem;
using google_wax_api::WaxListResponse;
using google_wax_api::WaxNewSessionRequest;
using google_wax_api::WaxNewSessionResponse;
using google_wax_api::WaxRemoveSessionRequest;
using google_wax_api::WaxService;

namespace googleapis {

using client::ClientServiceRequest;
using client::DataReader;
using client::HttpRequest;
using client::HttpRequestState;
using client::HttpResponse;
using client::HttpTransport;
using client::HttpTransportFactory;
using client::JsonCppArray;
using client::JsonCppCapsule;
using client::JsonCppData;
using client::JoinPath;


static std::unique_ptr<string> global_session_id_;
static WaxService* global_service_ = NULL;

// This macro aborts out of the test early if we are ignoring 503's.
#define MAYBE_CANCEL_TEST_ON_503(http_code)        \
  if (!FLAGS_allow_503 || (http_code != 503)) {}   \
  else {  LOG(ERROR) << "Saw 503 -- Skipping testcase. "; return; }  // NOLINT

static const client::HttpTransportLayerConfig* config_(NULL);
static const client::HttpTransportLayerConfig* GetConfig() {
  CHECK(config_ != NULL) << "Test did not SetTestConfiguration";
  CHECK(config_->default_transport_factory())
      << "Config has no default transport factory";
  return config_;
}

#ifndef _MSC_VER

static volatile sig_atomic_t server_is_ready_ = false;

static void HandleSignalFromChild(int sig) {
  if (sig == SIGUSR1) {
    server_is_ready_ = true;
  }
}

static void StartServerWithForkAndSignals() {
  if (!FLAGS_fork_wax) {
    return;
  }

  server_is_ready_ = false;

  // Setup signal handle so we know when child is ready
  struct sigaction usr_action;
  struct sigaction restore_sigusr1_action;
  sigset_t block_mask;
  sigfillset(&block_mask);
  usr_action.sa_handler = HandleSignalFromChild;
  usr_action.sa_mask = block_mask;
  usr_action.sa_flags = 0;
  CHECK_EQ(0, sigaction(SIGUSR1, &usr_action, &restore_sigusr1_action));

  pid_t pid = fork();
  if (pid < 0) {
    LOG(ERROR) << "Could not fork";
  } else if (pid != 0) {
    // We're reversing the normal parent/child so that the
    // parent becomes a webserver and the child becomes the test.
    // This is to make it easier to kill the server if the child test
    // crashes.
    string program_path = File::GetCurrentProgramFilenamePath();
    StringPiece test_dir = File::StripBasename(program_path);
    string wax_path = JoinPath(test_dir, "wax_server.py");
    string signal_pid = StrCat("--signal_pid=", pid);

    const char* const wax_args[] = {
      wax_path.c_str(), "-g", signal_pid.c_str(), NULL
    };
    if (execv(wax_args[0], const_cast<char * const*>(wax_args)) < 0) {
      LOG(ERROR) << "Could not run " << wax_path << " errno=" << errno;
    }
  } else {
    while (!server_is_ready_) {
      sleep(0);
    }
    CHECK_EQ(0, sigaction(SIGUSR1, &restore_sigusr1_action, NULL));
  }
}

#else

static PROCESS_INFORMATION process_info_;

static void StartWindowsServer() {
  string program_path = File::GetCurrentProgramFilenamePath();
  StringPiece test_dir = File::StripBasename(program_path);
  string wax_path = JoinPath(test_dir, "wax_server.py");

  // TODO(user): 20130723
  // Shoudl find this on PATH.
  const StringPiece kPythonPath = "c:\\python_27\\files\\python.exe";
  string command_line = StrCat(kPythonPath, " ", wax_path);

  STARTUPINFO startup_info = { sizeof(STARTUPINFO) };
  string windows_command_line;
  googleapis::ToWindowsString(command_line, &windows_command_line);
  std::unique_ptr<TCHAR[]> writeable_windows_command_line(new TCHAR[MAX_PATH + 1]);
  memcpy(writeable_windows_command_line.get(), windows_command_line.data(),
         (windows_command_line.size() + sizeof(TCHAR)));

  startup_info.dwFlags = STARTF_USESHOWWINDOW;
  startup_info.wShowWindow = SW_HIDE;
  bool ok = CreateProcess(
      NULL,
      writeable_windows_command_line.get(),
      NULL,  // process security
      NULL,  // thread security
      false,  // inherit handles
      HIGH_PRIORITY_CLASS | CREATE_NO_WINDOW,
      NULL,  // environment
      NULL,  // current dir
      &startup_info,
      &process_info_);
  CHECK(ok);
  // TODO(user): 20130723
  // Use synchronization objects.
  Sleep(2 * 1000);
}
#endif


void HttpTransportTestFixture::SetUpTestCase() {
  if (!FLAGS_fork_wax) {
    return;
  }

#ifndef _MSC_VER
  StartServerWithForkAndSignals();
#else
  StartWindowsServer();
#endif

}


void HttpTransportTestFixture::TearDownTestCase() {
  if (!FLAGS_fork_wax) {
    return;
  }

  HttpTransport* transport = GetConfig()->NewDefaultTransportOrDie();
  std::unique_ptr<HttpRequest> request(
      transport->NewHttpRequest(HttpRequest::GET));
  request->set_url(
      JoinPath(
          JoinPath(FLAGS_wax_root_url, FLAGS_wax_service_path),
          "quit"));
  googleapis::util::Status status = request->Execute();
  if (!status.ok()) {
    LOG(ERROR) << "Error quiting server: " << status.error_message();
  }


#if _MSC_VER
  CloseHandle(process_info_.hProcess);
  CloseHandle(process_info_.hThread);
#endif

}

void HttpTransportTestFixture::SetTestConfiguration(
    const client::HttpTransportLayerConfig* config) {
  config_ = config;
}

void HttpTransportTestFixture::ResetGlobalSessionId() {
  WaxService* service = &GetGlobalWaxService();
  const WaxService::SessionsResource& rsrc = service->get_sessions();
  std::unique_ptr<WaxRemoveSessionRequest> request(
      WaxRemoveSessionRequest::New());
  request->set_session_id(GetGlobalSessionId());

  std::unique_ptr<SessionsResource_RemoveSessionMethod> remove_method(
      rsrc.NewRemoveSessionMethod(NULL, *request));

  googleapis::util::Status got_status = remove_method->Execute();

  // Check for 503, but we need to do the cleanup so dont use the macro.
  // Explicitly guard it instead.
  if (!FLAGS_allow_503 || remove_method->http_response()->http_code() != 503) {
    EXPECT_TRUE(got_status.ok()) << got_status.ToString();
    EXPECT_EQ(200, remove_method->http_response()->http_code());
  }

  // Since we deleted the global session id, let's erase it here so
  // if the tests are run out of order we'll know to generate a
  // new session for the next test.
  delete global_service_;
  global_service_ = NULL;
  delete global_session_id_.release();
}

WaxService& HttpTransportTestFixture::GetGlobalWaxService() {
  if (global_service_ == NULL) {
    HttpTransport* transport = GetConfig()->NewDefaultTransportOrDie();

    // We'll allow this to be really slow for this test.
    // Rather have late responses than timeouts.
    // Wax is really slow sometimes and fails with a 10s timeout.
    // This particular test is about protocol correctness, not timeliness
    // so we'll be very generous with our timeout to mitigate timing errors.
    transport->mutable_default_request_options()->set_timeout_ms(60000);
    global_service_ = new WaxService(transport);
    global_service_->ChangeServiceUrl(
        FLAGS_wax_root_url, FLAGS_wax_service_path);

    std::unique_ptr<WaxNewSessionRequest> request(WaxNewSessionRequest::New());
    const char* kPrototype = "HttpTransportTest";
    request->set_session_name(kPrototype);

    WaxService* service = global_service_;
    const WaxService::SessionsResource& rsrc = service->get_sessions();
    std::unique_ptr<SessionsResource_NewSessionMethod> new_method(
        rsrc.NewNewSessionMethod(NULL, *request));

    std::unique_ptr<WaxNewSessionResponse> result(WaxNewSessionResponse::New());
    googleapis::util::Status got_status =
          new_method->ExecuteAndParseResponse(result.get());
    HttpResponse* http_response = new_method->http_response();
    if (http_response->http_code() == 503 && FLAGS_allow_503) {
      LOG(ERROR) << "Terminating test because wax is not available";
      exit(0);
    }

    // No point in continuing if we cannot get this far.
    CHECK(got_status.ok()) << got_status
                           << " / http_code=" << http_response->http_code();

    EXPECT_EQ(200, http_response->http_code());
    if (http_response->ok()) {
      global_session_id_.reset(
          new string(result->get_new_session_id().as_string()));
      LOG(INFO) << "Wax Session ID=" << *global_session_id_;
    } else {
      LOG(ERROR) << "FAILED to create new wax session id";
    }
  }
  return *global_service_;
}

const string& HttpTransportTestFixture::GetGlobalSessionId() {
  if (!global_session_id_.get()) {
    GetGlobalWaxService();
  }
  return *global_session_id_;
}


HttpTransportTestFixture::HttpTransportTestFixture() {

  if (!FLAGS_http_scribe_path.empty()) {
    HttpTransportFactory* factory =
        GetConfig()->default_transport_factory();
    if (factory->scribe()) return;

    // This body just executes the first time we construct a fixture.
    // It modifies the global transport factory and scribe on it.
    // Each test is going to construct a new fixture, but they all share
    // the same global factory so we only have one file.


    string path = FLAGS_http_scribe_path;
    client::DataWriter* writer =
        client::NewFileDataWriter(path);
    LOG(INFO) << "Scribing HttpTransport activity to " << path;
    CHECK(writer->status().ok()) << writer->status();

    client::HttpScribe* scribe;
    client::HttpScribeCensor* censor =
        new client::HttpScribeCensor;

    if (StringPiece(path).ends_with(".json")) {
      scribe = new client::JsonScribe(censor, writer, false);
    } else if (StringPiece(path).ends_with(".html")) {
      string title = "Standard Transport Test";
      scribe = new client::HtmlScribe(censor, title, writer);
    } else {
      LOG(FATAL) << "Unknown scribe type for path=" << path;
    }
    factory->reset_scribe(scribe);
  }
}

HttpTransportTestFixture::~HttpTransportTestFixture() {
  HttpTransportFactory* factory =
      GetConfig()->default_transport_factory();
  if (factory && factory->scribe()) {
    factory->scribe()->Checkpoint();
  }
}

TEST_F(HttpTransportTestFixture, TestList) {
  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_ListMethod> list_method(
      rsrc.NewListMethod(NULL, GetGlobalSessionId()));

  std::unique_ptr<WaxListResponse> result(WaxListResponse::New());
  googleapis::util::Status got_status =
        list_method->ExecuteAndParseResponse(result.get());
  HttpResponse* http_response = list_method->http_response();
  MAYBE_CANCEL_TEST_ON_503(http_response->http_code());

  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(200, http_response->http_code());
  EXPECT_EQ(HttpRequestState::COMPLETED,
            http_response->request_state_code());
  EXPECT_TRUE(http_response->ok());
  EXPECT_TRUE(http_response->done());
  EXPECT_TRUE(http_response->status().ok());
  const JsonCppArray<WaxDataItem>& items = result->get_items();

  // It could be > 2 if we've executed other tests that inserted.
  ASSERT_LE(2, items.size());

  EXPECT_EQ("A", items.get(0).get_id());
  EXPECT_EQ("B", items.get(1).get_id());
  EXPECT_EQ("Item A", items.get(0).get_name());
  EXPECT_EQ("Item B", items.get(1).get_name());
  EXPECT_EQ("wax#waxDataItem", items.get(0).get_kind());
  EXPECT_EQ("wax#waxDataItem", items.get(1).get_kind());
}

TEST_F(HttpTransportTestFixture, TestBadGet) {
  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_GetMethod> get_method(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "XXX"));

  JsonCppCapsule<WaxDataItem> result;
  EXPECT_FALSE(get_method->ExecuteAndParseResponse(&result).ok());
  HttpResponse* http_response = get_method->http_response();
  MAYBE_CANCEL_TEST_ON_503(http_response->http_code());

  EXPECT_EQ(404, http_response->http_code());
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
  EXPECT_EQ("", http_response->transport_status().error_message());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_FALSE(http_response->ok());
  EXPECT_TRUE(http_response->done());
}

TEST_F(HttpTransportTestFixture, TestReuse) {
  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_GetMethod> get_method(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "A"));

  googleapis::util::Status got_status = get_method->Execute();
  MAYBE_CANCEL_TEST_ON_503(get_method->http_response()->http_code());

  EXPECT_TRUE(got_status.ok());

  // We cannot reuse without explicitly resetting.
  EXPECT_DEATH(get_method->Execute().IgnoreError(), "");

  // But once we do then we can execute again usng the same response...
  EXPECT_TRUE(
      get_method->mutable_http_request()->PrepareToReuse().ok());
  got_status = get_method->Execute();
  MAYBE_CANCEL_TEST_ON_503(get_method->http_response()->http_code());

  EXPECT_TRUE(got_status.ok());
}

TEST_F(HttpTransportTestFixture, TestGoodGet) {
  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_GetMethod> get_method(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "A"));

  JsonCppCapsule<WaxDataItem> wax;
  googleapis::util::Status got_status = get_method->ExecuteAndParseResponse(&wax);
  HttpResponse* http_response = get_method->http_response();
  MAYBE_CANCEL_TEST_ON_503(http_response->http_code());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();

  EXPECT_EQ(200, http_response->http_code());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
  EXPECT_TRUE(http_response->ok());
  EXPECT_TRUE(http_response->done());
  EXPECT_TRUE(http_response->status().ok());
  EXPECT_TRUE(http_response->transport_status().ok());

  EXPECT_EQ("A", wax.get_id());
  EXPECT_EQ("Item A", wax.get_name());
  EXPECT_LT(0, http_response->headers().size());
}

TEST_F(HttpTransportTestFixture, TestTimeout) {
  JsonCppCapsule<WaxDataItem> wax;
  wax.set_id("timout");
  wax.set_name("timeout test");
  wax.set_kind("wax#waxDataItem");

  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();

  // We're going to try this test multiple times until we get a timeout.
  // The actual transport errors are sometimes sensitive to timing out.
  // A really small value may fail before it gets to timeout.
  // A really large value may succeed without timing out.
  // So we'll either backoff or add more time.
  const int kTestAttempts = 10;
  const int kIncreasePerInterval = 10;
  const int kInitialTimeoutMs = 1;
  bool saw_timeout = false;
  for (int i = 0; i < kTestAttempts; ++i) {
    std::unique_ptr<ItemsResource_InsertMethod> insert_method(
        rsrc.NewInsertMethod(NULL, GetGlobalSessionId(), wax));

    HttpRequest* http_request = insert_method->mutable_http_request();
    const int64 timeout_ms = kInitialTimeoutMs + kIncreasePerInterval * i;
    http_request->mutable_options()->set_timeout_ms(timeout_ms);
    JsonCppCapsule<WaxDataItem> wax_result;
    googleapis::util::Status got_status =
          insert_method->ExecuteAndParseResponse(&wax_result);
    EXPECT_FALSE(got_status.ok());
    HttpResponse* http_response = insert_method->http_response();
    EXPECT_FALSE(http_response->transport_status().ok());
    if (http_response->request_state_code() == HttpRequestState::TIMED_OUT) {
      LOG(INFO) << "Timed out with ms=" << timeout_ms;
      saw_timeout = true;
      break;
    }
    if (i == 0) {
      LOG(WARNING) << "Expected timeout (ms=" << timeout_ms << ") but got"
                   << " state=" << http_response->request_state_code()
                   << " status=" << http_response->transport_status()
                   << ". This might be intermittent -- trying again.";
    }
  }
  EXPECT_TRUE(saw_timeout)
      << "Failed to timeout in range " << kInitialTimeoutMs
      << ".." << (kInitialTimeoutMs + kIncreasePerInterval * kTestAttempts)
      << "ms";
}

TEST_F(HttpTransportTestFixture, TestInsert) {
  JsonCppCapsule<WaxDataItem> wax;
  wax.set_id("I");
  wax.set_name("Item I");
  wax.set_kind("wax#waxDataItem");

  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_InsertMethod> insert_method(
      rsrc.NewInsertMethod(NULL, GetGlobalSessionId(), wax));

  JsonCppCapsule<WaxDataItem> wax_result;
  googleapis::util::Status got_status =
        insert_method->ExecuteAndParseResponse(&wax_result);
  MAYBE_CANCEL_TEST_ON_503(insert_method->http_response()->http_code());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();

  HttpResponse* http_response = insert_method->http_response();
  EXPECT_EQ(200, http_response->http_code());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
  EXPECT_TRUE(http_response->ok());
  EXPECT_TRUE(http_response->done());
  EXPECT_TRUE(http_response->status().ok());
  EXPECT_TRUE(http_response->transport_status().ok());

  JsonCppCapsule<WaxDataItem> check_wax;
  std::unique_ptr<ItemsResource_GetMethod> check_method(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "I"));

  got_status = check_method->ExecuteAndParseResponse(&check_wax);
  HttpResponse* check_response = check_method->http_response();
  MAYBE_CANCEL_TEST_ON_503(check_response->http_code());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();

  EXPECT_EQ(200, check_response->http_code());
  if (check_response->ok()) {
    EXPECT_EQ("I", check_wax.get_id());
    EXPECT_EQ("Item I", check_wax.get_name());
    EXPECT_EQ("wax#waxDataItem", check_wax.get_kind());
  }
}

TEST_F(HttpTransportTestFixture, TestBadInsert) {
  JsonCppCapsule<WaxDataItem> wax;
  wax.set_id("A");
  wax.set_name("Duplicate of Item A");
  wax.set_kind("wax#waxDataItem");

  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_InsertMethod> insert_method(
      rsrc.NewInsertMethod(NULL, GetGlobalSessionId(), wax));

  JsonCppCapsule<WaxDataItem> wax_result;
  googleapis::util::Status got_status =
        insert_method->ExecuteAndParseResponse(&wax_result);
  EXPECT_FALSE(got_status.ok());
  HttpResponse* http_response = insert_method->http_response();
  MAYBE_CANCEL_TEST_ON_503(http_response->http_code());

  EXPECT_EQ(403, http_response->http_code());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
  EXPECT_FALSE(http_response->ok());
  EXPECT_TRUE(http_response->done());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_EQ("", http_response->transport_status().error_message());
}

TEST_F(HttpTransportTestFixture, TestDelete) {
  JsonCppCapsule<WaxDataItem> wax;
  wax.set_id("D");
  wax.set_name("Item D");
  wax.set_kind("wax#waxDataItem");

  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_InsertMethod> insert_method(
      rsrc.NewInsertMethod(NULL, GetGlobalSessionId(), wax));

  googleapis::util::Status got_status = insert_method->Execute();
  HttpResponse* http_response = insert_method->http_response();
  MAYBE_CANCEL_TEST_ON_503(http_response->http_code());
  EXPECT_TRUE(got_status.ok());
  EXPECT_EQ(200, http_response->http_code());

  std::unique_ptr<ItemsResource_GetMethod> check_method(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "D"));
  got_status = check_method->Execute();
  MAYBE_CANCEL_TEST_ON_503(check_method->http_response()->http_code());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(200, check_method->http_response()->http_code());

  std::unique_ptr<ItemsResource_DeleteMethod> delete_method(
      rsrc.NewDeleteMethod(NULL, GetGlobalSessionId(), "D"));
  got_status = delete_method->Execute();
  MAYBE_CANCEL_TEST_ON_503(delete_method->http_response()->http_code());
  HttpResponse* delete_response = delete_method->http_response();

  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(204, delete_response->http_code());
  EXPECT_TRUE(delete_response->ok());
  EXPECT_TRUE(delete_response->done());
  EXPECT_TRUE(delete_response->transport_status().ok());
  EXPECT_TRUE(delete_response->status().ok());

  check_method.reset(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "D"));
  EXPECT_FALSE(check_method->Execute().ok());
  MAYBE_CANCEL_TEST_ON_503(check_method->http_response()->http_code());
  EXPECT_EQ(404, check_method->http_response()->http_code());
}

TEST_F(HttpTransportTestFixture, TestPatch) {
  JsonCppCapsule<WaxDataItem> wax;
  wax.set_name("Patched A");

  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_PatchMethod> patch_method(
      rsrc.NewPatchMethod(NULL, GetGlobalSessionId(), "A", wax));

  googleapis::util::Status got_status = patch_method->Execute();
  MAYBE_CANCEL_TEST_ON_503(patch_method->http_response()->http_code());

  // TODO(user): 20130227
  // Need some kind of mechanism to ask a transport if it supports a method.
  // Probably need to give it the request also.
  if (got_status.error_code() == util::error::UNIMPLEMENTED) {
    LOG(WARNING) << "Patch not implemented -- skipping test";
    return;
  }

  HttpResponse* http_response = patch_method->http_response();
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(200, http_response->http_code());
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_TRUE(http_response->status().ok());

  JsonCppCapsule<WaxDataItem> check_wax;
  std::unique_ptr<ItemsResource_GetMethod> check_method(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "A"));

  got_status = check_method->ExecuteAndParseResponse(&check_wax);
  MAYBE_CANCEL_TEST_ON_503(check_method->http_response()->http_code());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(200, check_method->http_response()->http_code());
  if (check_method->http_response()->ok()) {
    EXPECT_EQ("A", check_wax.get_id());
    EXPECT_EQ("Patched A", check_wax.get_name());
    EXPECT_EQ("wax#waxDataItem", check_wax.get_kind());
  }
}

TEST_F(HttpTransportTestFixture, TestUpdate) {
  JsonCppCapsule<WaxDataItem> wax;
  wax.set_name("Updated A");

  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_UpdateMethod> update_method(
      rsrc.NewUpdateMethod(NULL, GetGlobalSessionId(), "A", wax));

  googleapis::util::Status got_status = update_method->Execute();
  MAYBE_CANCEL_TEST_ON_503(update_method->http_response()->http_code());

  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  HttpResponse* http_response = update_method->http_response();
  EXPECT_EQ(200, http_response->http_code());
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_TRUE(http_response->status().ok());

  JsonCppCapsule<WaxDataItem> check_wax;
  std::unique_ptr<ItemsResource_GetMethod> check_method(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "A"));
  got_status = check_method->ExecuteAndParseResponse(&check_wax);
  MAYBE_CANCEL_TEST_ON_503(check_method->http_response()->http_code());

  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(200, check_method->http_response()->http_code());
  if (check_method->http_response()->ok()) {
    EXPECT_EQ("A", check_wax.get_id());
    EXPECT_EQ("Updated A", check_wax.get_name());
    EXPECT_EQ("wax#waxDataItem", check_wax.get_kind());
  }
}

TEST_F(HttpTransportTestFixture, TestRemoveSessionId) {
  WaxService* service = &GetGlobalWaxService();
  string original_id = GetGlobalSessionId();
  std::unique_ptr<ItemsResource_GetMethod> check_method(
      service->get_items().NewGetMethod(NULL, GetGlobalSessionId(), "A"));

  googleapis::util::Status got_status = check_method->Execute();
  MAYBE_CANCEL_TEST_ON_503(check_method->http_response()->http_code());

  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(200, check_method->http_response()->http_code());

  ResetGlobalSessionId();
  service = &GetGlobalWaxService();

  // We removed the original_id so this request will now fail.
  check_method.reset(
      service->get_items().NewGetMethod(NULL, original_id, "A"));

  EXPECT_FALSE(check_method->Execute().ok());
  MAYBE_CANCEL_TEST_ON_503(check_method->http_response()->http_code());

  EXPECT_EQ(404, check_method->http_response()->http_code());
}

TEST_F(HttpTransportTestFixture, TestResponseHeaders) {
  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  std::unique_ptr<ItemsResource_GetMethod> get_method(
      rsrc.NewGetMethod(NULL, GetGlobalSessionId(), "A"));

  googleapis::util::Status got_status = get_method->Execute();
  MAYBE_CANCEL_TEST_ON_503(get_method->http_response()->http_code());

  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_LT(0, get_method->http_response()->headers().size());
}

static void GatherAsyncResponse(
    int i, Mutex* mutex, std::vector<HttpRequest*>* got, int* remaining,
    HttpRequest* request) {
  HttpResponse* response = request->response();
  VLOG(1) << "*** Got Response for i=" << i << " status=" << response->status();
  if (!response->ok()) {
    if (response->body_reader()) {
      LOG(ERROR) << "ERROR BODY\n\n"
                 << response->body_reader()->RemainderToString()
                 << "\n\n\n";
      response->body_reader()->Reset();
    } else {
      LOG(ERROR) << "ERROR (null body)\n\n";
    }
  }

  MutexLock l(mutex);
  (*got)[i] = request;
  *remaining -= 1;
}

static void VerifyAsyncResponse(int i, int expect_len, HttpRequest* request) {
  HttpResponse* response = request->response();
  VLOG(1) << "*** Got Response for i=" << i << " status=" << response->status();
  MAYBE_CANCEL_TEST_ON_503(response->http_code());

  ASSERT_TRUE(response->ok()) << "i=" << i;
  std::unique_ptr<WaxListResponse> list(WaxListResponse::New());
  DataReader* reader = response->body_reader();
  ASSERT_TRUE(reader != NULL);

  ASSERT_TRUE(list->LoadFromJsonReader(reader).ok()) << " i=" << i;
  const JsonCppArray<WaxDataItem>& items = list->get_items();
  EXPECT_EQ(expect_len, items.size());

  VLOG(1) << "Checking results for i=" << i;
  // Items are not necessarily ordered.
  int num_found = 0;
  for (int check = 0; check < expect_len; ++check) {
    bool found = false;
    StringPiece id = items.get(check).get_id();
    VLOG(1) << "   scanning for id=" << id;
    for (int scan = 0; scan < expect_len; ++scan) {
      if (id == StrCat("AsyncId ", scan)) {
        EXPECT_EQ(StrCat("AsyncName ", scan), items.get(check).get_name());
        found = true;
        break;
      }
    }
    if (found) {
      ++num_found;
    } else {
      LOG(ERROR) << "***** missing " << id;
    }
  }
  EXPECT_EQ(num_found, items.size()) << "Returned duplicate items";
  VLOG(1) << "Finished checking i=" << i;
}

TEST_F(HttpTransportTestFixture, TestAsynchronous) {
  // First clear old session id so we are fresh.
  ResetGlobalSessionId();

  // Set up our async executor
  int kNumInserts = 12;  // Concurrent insert operations
  int kNumLookups = 8;   // Concurrent list operations
  int kNumBuiltins = 2;  // Delete builtin 'A' and 'B' Id's

  Mutex mutex;
  int remaining;

  // We're going to post a bunch of inserts asynchronously
  WaxService* service = &GetGlobalWaxService();
  const WaxService::ItemsResource& rsrc = service->get_items();
  remaining = kNumInserts + kNumBuiltins;  // delete the builtins
  std::vector<ClientServiceRequest*> requests;
  std::vector<HttpRequest*> got(kNumInserts + kNumBuiltins);

  for (int i = 0; i < kNumInserts; ++i) {
    JsonCppCapsule<WaxDataItem> wax;
    wax.set_id(StrCat("AsyncId ", i));
    wax.set_name(StrCat("AsyncName ", i));
    ItemsResource_InsertMethod* insert_method(
        rsrc.NewInsertMethod(NULL, GetGlobalSessionId(), wax));
    requests.push_back(insert_method);
    VLOG(1) << "Adding " << i;
    insert_method->ExecuteAsync(
        NewCallback(&GatherAsyncResponse, i, &mutex, &got, &remaining));
    googleapis::util::Status status = insert_method->http_response()->transport_status();
    ASSERT_TRUE(status.ok()) << status.error_message();
  }

  // Also delete the kNumBuiltins ('A' and 'B' )defaults added by Wax itself
  for (int i = 0; i < kNumBuiltins; ++i) {
    string builtin_id(1, 'A' + i);
    int response_index = i + kNumInserts;
    ItemsResource_DeleteMethod* delete_method =
        rsrc.NewDeleteMethod(NULL, GetGlobalSessionId(), builtin_id);

    requests.push_back(delete_method);
    VLOG(1) << "Adding delete " << builtin_id;
    delete_method->ExecuteAsync(
        NewCallback(&GatherAsyncResponse, response_index, &mutex, &got,
                    &remaining));
    googleapis::util::Status status = delete_method->http_response()->transport_status();
    ASSERT_TRUE(status.ok()) << status.error_message();
  }

  // Then wait for them to come back, (but with a timeout)
  // The + kNumBuiltins for the deletes
  const int kWaitSecs = 20;
  bool saw_503 = false;
  for (int i = 0; i < kNumInserts + kNumBuiltins; ++i) {
    if (got[i]) {
      EXPECT_EQ(requests[i]->mutable_http_request(), got[i]);
      if (requests[i]->http_response()->http_code() == 503) {
        saw_503 = true;
      }
      // We're calling DestroyWhenDone here rather than delete because
      // there's a race condition if we set got in the callback, but
      // the method is still executing and referencing the method state
      // that we would be deleting.
      requests[i]->DestroyWhenDone();
      requests[i] = NULL;
    } else {
      VLOG(1) << "Waiting on " << i;
      if (requests[i]->http_response()->WaitUntilDone(kWaitSecs * 1000)) {
        CHECK(requests[i]->http_response()->done())
            << " state_code[" << i << "]="
            << requests[i]->http_response()->request_state_code();
        VLOG(1) << "   OK";
      } else {
        VLOG(1) << "   NOT YET";
      }
    }
  }

  // Now wait forever because we need to ensure were cleaned up
  // before we leave this test.
  for (int i = 0; i < kNumInserts + kNumBuiltins; ++i) {
    if (requests[i]) {
      VLOG(1) << "Blocking until i=" << i << " completes";
      ASSERT_TRUE(requests[i]->http_response()->WaitUntilDone(kint64max));
      EXPECT_EQ(requests[i]->mutable_http_request(), got[i]);
      if (requests[i]->http_response()->http_code() == 503) {
        saw_503 = true;
      }
      requests[i]->DestroyWhenDone();
      requests[i] = NULL;
    }
  }
  ASSERT_EQ(0, remaining);  // Called back number of times expected.

  if (saw_503 && !FLAGS_allow_503) {
    LOG(ERROR) << "Saw 503 - skipping test";
    return;
  }

  // Now we're going to perform concurrent GETs and ensure that they
  // are all reasonable.
  // (note that by now the builtins are gone).
  for (int i = 0; i < kNumLookups; ++i) {
    ItemsResource_ListMethod* list_method =
        rsrc.NewListMethod(NULL, GetGlobalSessionId());
    got[i] = NULL;
    requests[i] = list_method;
    VLOG(1) << "Listing " << i;
    list_method->ExecuteAsync(
        NewCallback(&VerifyAsyncResponse, i, kNumInserts));
  }

  // Wait for them all to complete before we finish this test.
  for (int i = 0; i < kNumLookups; ++i) {
    if (requests[i]) {
      VLOG(1) << "Blocking until i=" << i << " completes";
      ASSERT_TRUE(requests[i]->http_response()->WaitUntilDone(kint64max));
      requests[i]->DestroyWhenDone();
    }
  }

}

}  // namespace googleapis

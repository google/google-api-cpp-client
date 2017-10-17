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

//
// This is a sample application illustrating the use of the GoogleApis C++
// Client. The application makes calls into the Google Calendar service.
// The application itself is not particularly useful, rather it just
// illustrates how to interact with a live service.
//
// Usage:
//
// Calendar requires OAuth2 authorization, which in turn requires that the
// application be authorized using the https://code.google.com/apis/console.
// You will need to do this yourself -- creating your own client ID and
// secret in order to run it.
//
// For this example, you want to create an Installed Application
//    From the "API Access" tab, create an "Installed Application" client ID
//       Download the client secrets JSON file.
//    From the "Services" tab, give access to the Calendar API.
//
// If you already know the ID and secret, you can create the json file yourself
// from teh following example (including outer {}). Replace the "..." with
// your values, but be sure to quote them  (i.e. "mysecret" }
// {
//    "installed": {
//       "client_id": "...",
//       "client_secret": "..."
//    }
//  }
//
//
// When the program starts up you will be asked to authorize by copying
// a URL into a browser and copying the response back. From there the
// program executes a shell that takes commands. Type 'help' for a list
// of current commands and 'quit' to exit.

#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include <memory>
#include "googleapis/client/auth/file_credential_store.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/data/data_reader.h"
#if HAVE_OPENSSL
#include "googleapis/client/data/openssl_codec.h"
#endif
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_request_batch.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/strcat.h"

#include "google/calendar_api/calendar_api.h"  // NOLINT

namespace googleapis {

using std::cin;
using std::cout;
using std::cerr;
using std::endl;

using google_calendar_api::Calendar;
using google_calendar_api::CalendarList;
using google_calendar_api::CalendarListEntry;
using google_calendar_api::CalendarsResource_DeleteMethod;
using google_calendar_api::CalendarsResource_InsertMethod;
using google_calendar_api::CalendarListResource_ListMethod;
using google_calendar_api::CalendarService;
using google_calendar_api::Event;
using google_calendar_api::Events;
using google_calendar_api::EventsResource_GetMethod;
using google_calendar_api::EventsResource_InsertMethod;
using google_calendar_api::EventsResource_ListMethod;
using google_calendar_api::EventsResource_ListMethodPager;
using google_calendar_api::EventsResource_PatchMethod;
using google_calendar_api::EventsResource_UpdateMethod;

using client::ClientServiceRequest;
using client::DateTime;
using client::FileCredentialStoreFactory;
using client::HttpRequestBatch;
using client::HttpResponse;
using client::HttpTransport;
using client::HttpTransportLayerConfig;
using client::JsonCppArray;
using client::OAuth2Credential;
using client::OAuth2AuthorizationFlow;
using client::OAuth2RequestOptions;
#if HAVE_OPENSSL
using client::OpenSslCodecFactory;
#endif
using client::StatusCanceled;
using client::StatusInvalidArgument;
using client::StatusOk;

const char kSampleStepPrefix[] = "SAMPLE:  ";

static googleapis::util::Status PromptShellForAuthorizationCode(
    OAuth2AuthorizationFlow* flow,
    const OAuth2RequestOptions& options,
    string* authorization_code) {
  string url = flow->GenerateAuthorizationCodeRequestUrlWithOptions(options);
  std::cout << "Enter the following URL into a browser:\n" << url << std::endl;
  std::cout << std::endl;
  std::cout << "Enter the browser's response to confirm authorization: ";

  authorization_code->clear();
  std::cin >> *authorization_code;
  if (authorization_code->empty()) {
    return StatusCanceled("Canceled");
  } else {
    return StatusOk();
  }
}

static googleapis::util::Status ValidateUserName(const string& name) {
  if (name.find("/") != string::npos) {
    return StatusInvalidArgument("UserNames cannot contain '/'");
  } else if (name == "." || name == "..") {
    return StatusInvalidArgument(
        StrCat("'", name, "' is not a valid UserName"));
  }
  return StatusOk();
}

void DisplayError(ClientServiceRequest* request) {
  const HttpResponse& response = *request->http_response();
  std::cout << "====  ERROR  ====" << std::endl;
  std::cout << "Status: " << response.status().error_message() << std::endl;
  if (response.transport_status().ok()) {
    std::cout << "HTTP Status Code = " << response.http_code() << std::endl;
    std::cout << std::endl
              << response.body_reader()->RemainderToString() << std::endl;
  }
  std::cout << std::endl;
}

void Display(const string& prefix, const CalendarListEntry& entry) {
  std::cout << prefix << "CalendarListEntry" << std::endl;
  std::cout << prefix << "  ID: " << entry.get_id() << std::endl;
  std::cout << prefix << "  Summary: " << entry.get_summary() << std::endl;
  if (entry.has_description()) {
    std::cout << prefix << "  Description: " << entry.get_description()
              << std::endl;
  }
}

void Display(const string& prefix, const Calendar& entry) {
  std::cout << prefix << "Calendar" << std::endl;
  std::cout << prefix << "  ID: " << entry.get_id() << std::endl;
  std::cout << prefix << "  Summary: " << entry.get_summary() << std::endl;
  if (!entry.get_description().empty()) {
    std::cout << prefix << "  Description: " << entry.get_description()
              << std::endl;
  }
}

void Display(const string& prefix, const Event& event) {
  std::cout << prefix << "Event" << std::endl;
  std::cout << prefix << "  ID: " << event.get_id() << std::endl;
  if (event.has_summary()) {
    std::cout << prefix << "  Summary: " << event.get_summary() << std::endl;
  }
  if (event.get_start().has_date_time()) {
    std::cout << prefix << "  Start Time: "
              << event.get_start().get_date_time().ToString() << std::endl;
  }
  if (event.get_end().has_date_time()) {
    std::cout << prefix
              << "  End Time: " << event.get_end().get_date_time().ToString()
              << std::endl;
  }
}

template <class LIST, typename ELEM>
void DisplayList(
    const string& prefix, const string& title, const LIST& list) {
  std::cout << prefix << "====  " << title << "  ====" << std::endl;
  string sub_prefix = StrCat(prefix, "  ");
  bool first = true;
  const JsonCppArray<ELEM>& items = list.get_items();
  for (typename JsonCppArray<ELEM>::const_iterator it = items.begin();
       it != items.end();
       ++it) {
    if (first) {
      first = false;
    } else {
      std::cout << std::endl;
    }
    Display(sub_prefix, *it);
  }
  if (first) {
    std::cout << sub_prefix << "<no items>" << std::endl;
  }
}

class CalendarSample {
 public:
  static googleapis::util::Status Startup(int argc, char* argv[]);
  void Run();

 private:
  // Gets authorization to access the user's personal calendar data.
  googleapis::util::Status Authorize();

  // Prints some current calendar data to the console to show the effects
  // from the calls that the sample has made.
  void ShowCalendars();

  // Demonstrates adding a new resource. For this example, it is a calendar.
  // Returns a proxy to the calendar added in the Calendar Service (cloud).
  // We only really need the ID so that's all we return.
  string AddCalendar();

  // Demonstrates adding an embedded resource.
  // For this example it is a calendar event.
  //
  // This takes a calendar ID such as that returned by AddCalendar().
  // We'll take a configured event as input and modify its ID with the
  // ID assigned by the Calendar Service (cloud). We could have just returned
  // the ID as in the case of Calendar but we are doing it differently for
  // demonstration purposes.
  void AddEvent(const string& calendar_id, Event* event);

  // Demonstrates using a ServiceRequestPager to list all the events on the
  // given calendar.
  void PageThroughAllEvents(const string& calendar_id, int events_per_page);

  // Demonstrates deleting a resource. For this example, it is a calendar.
  // We are deleting the calendar in the Calendar Service (cloud) that
  // has the given calendar_id.
  void DeleteCalendar(const string& calendar_id);

  // Demonstrates getting a resource.
  // For this example, it is a calendar event.
  // Returns the final status for the request. If not ok() then event wasn't
  // populated.
  googleapis::util::Status GetEvent(
      const string& calendar_id, const string& event_id, Event* event);

  // Demonstrates patching a resource.
  // For this example, it is a calendar event.
  void PatchEvent(const string& calendar_id, const Event& event);

  // Demonstrates updating a resource.
  // For this example, it is a calendar event.
  void UpdateEvent(const string& calendar_id, const Event& event);

  OAuth2Credential credential_;
  static std::unique_ptr<CalendarService> service_;
  static std::unique_ptr<OAuth2AuthorizationFlow> flow_;
  static std::unique_ptr<HttpTransportLayerConfig> config_;
};

// static
std::unique_ptr<CalendarService> CalendarSample::service_;
std::unique_ptr<OAuth2AuthorizationFlow> CalendarSample::flow_;
std::unique_ptr<HttpTransportLayerConfig> CalendarSample::config_;


/* static */
util::Status CalendarSample::Startup(int argc, char* argv[]) {
  if ((argc < 2) || (argc > 3)) {
    string error =
        StrCat("Invalid Usage:\n",
               argv[0], " <client_secrets_file> [<cacerts_path>]\n");
    return StatusInvalidArgument(error);
  }

  // Set up HttpTransportLayer.
  googleapis::util::Status status;
  config_.reset(new HttpTransportLayerConfig);
  client::HttpTransportFactory* factory =
      new client::CurlHttpTransportFactory(config_.get());
  config_->ResetDefaultTransportFactory(factory);
  if (argc > 2) {
    config_->mutable_default_transport_options()->set_cacerts_path(argv[2]);
  }

  // Set up OAuth 2.0 flow for getting credentials to access personal data.
  const string client_secrets_file = argv[1];
  flow_.reset(OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath(
      client_secrets_file, config_->NewDefaultTransportOrDie(), &status));
  if (!status.ok()) return status;

  flow_->set_default_scopes(CalendarService::SCOPES::CALENDAR);
  flow_->mutable_client_spec()->set_redirect_uri(
      OAuth2AuthorizationFlow::kOutOfBandUrl);
  flow_->set_authorization_code_callback(
      NewPermanentCallback(&PromptShellForAuthorizationCode, flow_.get()));

  string home_path;
  status = FileCredentialStoreFactory::GetSystemHomeDirectoryStorePath(
      &home_path);
  if (status.ok()) {
    FileCredentialStoreFactory store_factory(home_path);
    // Use a credential store to save the credentials between runs so that
    // we dont need to get permission again the next time we run. We are
    // going to encrypt the data in the store, but leave it to the OS to
    // protect access since we do not authenticate users in this sample.
#if HAVE_OPENSSL
    OpenSslCodecFactory* openssl_factory = new OpenSslCodecFactory;
    status = openssl_factory->SetPassphrase(
        flow_->client_spec().client_secret());
    if (!status.ok()) return status;
    store_factory.set_codec_factory(openssl_factory);
#endif

    flow_->ResetCredentialStore(
        store_factory.NewCredentialStore("CalendarSample", &status));
  }
  if (!status.ok()) return status;

  // Now we'll initialize the calendar service proxy that we'll use
  // to interact with the calendar from this sample program.
  HttpTransport* transport = config_->NewDefaultTransport(&status);
  if (!status.ok()) return status;

  service_.reset(new CalendarService(transport));
  return status;
}


util::Status CalendarSample::Authorize() {
  std::cout
      << std::endl
      << "Welcome to the Google APIs for C++ CalendarSample.\n"
      << "  You will need to authorize this program to look at your calendar.\n"
      << "  If you would like to save these credentials between runs\n"
      << "  (or restore from an earlier run) then enter a Google Email "
         "Address.\n"
      << "  Otherwise just press return.\n" << std::endl
      << "  Address: ";
  string email;
  std::getline(std::cin, email);
  if (!email.empty()) {
    googleapis::util::Status status = ValidateUserName(email);
    if (!status.ok()) {
      return status;
    }
  }

  OAuth2RequestOptions options;
  options.email = email;
  googleapis::util::Status status =
        flow_->RefreshCredentialWithOptions(options, &credential_);
  if (!status.ok()) {
    return status;
  }

  credential_.set_flow(flow_.get());
  std::cout << "Authorized " << email << std::endl;
  return StatusOk();
}


void CalendarSample::ShowCalendars() {
  std::unique_ptr<CalendarListResource_ListMethod> method(
      service_->get_calendar_list().NewListMethod(&credential_));

  std::unique_ptr<CalendarList> calendar_list(CalendarList::New());
  if (!method->ExecuteAndParseResponse(calendar_list.get()).ok()) {
    DisplayError(method.get());
    return;
  }
  DisplayList<CalendarList, CalendarListEntry>(
      "", "CalendarList", *calendar_list);
  std::cout << std::endl;
}

string CalendarSample::AddCalendar() {
  std::unique_ptr<Calendar> calendar(Calendar::New());
  calendar->set_summary("Calendar added by CalendarSample");

  std::unique_ptr<CalendarsResource_InsertMethod> method(
      service_->get_calendars().NewInsertMethod(&credential_, *calendar));

  if (!method->ExecuteAndParseResponse(calendar.get()).ok()) {
    DisplayError(method.get());
    return "";
  }

  string result = calendar->get_id().as_string();
  std::cout << "Added new calendar ID=" << result << ":" << std::endl;
  Display("  ", *calendar);
  std::cout << std::endl;

  return result;
}

void CalendarSample::AddEvent(const string& calendar_id, Event* event) {
  std::unique_ptr<EventsResource_InsertMethod> method(
      service_->get_events().NewInsertMethod(
          &credential_, calendar_id, *event));

  if (!method->ExecuteAndParseResponse(event).ok()) {
    DisplayError(method.get());
    return;
  }

  std::cout << "Added new event ID=" << event->get_id() << ":" << std::endl;
  Display("  ", *event);
  std::cout << std::endl;
}

void CalendarSample::PageThroughAllEvents(
    const string& calendar_id, int num_per_page) {
  std::cout << "All Events" << std::endl;
  std::unique_ptr<EventsResource_ListMethodPager> pager(
      service_->get_events().NewListMethodPager(&credential_, calendar_id));
  pager->request()->set_max_results(num_per_page);
  while (pager->NextPage()) {
    DisplayList<Events, Event>("  ", "EventList", *pager->data());
  }
}


util::Status CalendarSample::GetEvent(
    const string& calendar_id, const string& event_id, Event* event) {
  std::unique_ptr<EventsResource_GetMethod> method(
      service_->get_events().NewGetMethod(
          &credential_, calendar_id, event_id));

  return method->ExecuteAndParseResponse(event);
}

void CalendarSample::PatchEvent(
    const string& calendar_id, const Event& event) {
  std::unique_ptr<EventsResource_PatchMethod> method(
      service_->get_events().NewPatchMethod(
          &credential_, calendar_id, event.get_id(), event));

  if (!method->Execute().ok()) {
    DisplayError(method.get());
    return;
  }

  std::unique_ptr<Event> cloud_event(Event::New());
  googleapis::util::Status status =
        GetEvent(calendar_id, event.get_id().as_string(), cloud_event.get());
  if (status.ok()) {
    std::cout << "Patched event:" << std::endl;
    Display("  ", *cloud_event);
  } else {
    std::cout << "** Could not get patched event: " << status.error_message()
              << std::endl;
  }
  std::cout << std::endl;
}

void CalendarSample::UpdateEvent(
    const string& calendar_id, const Event& event) {
  std::unique_ptr<EventsResource_UpdateMethod> method(
      service_->get_events().NewUpdateMethod(
          &credential_, calendar_id, event.get_id(), event));

  if (!method->Execute().ok()) {
    DisplayError(method.get());
    return;
  }

  std::unique_ptr<Event> cloud_event(Event::New());
  googleapis::util::Status status =
        GetEvent(calendar_id, event.get_id().as_string(), cloud_event.get());
  if (status.ok()) {
    std::cout << "Updated event:" << std::endl;
    Display("  ", *cloud_event);
  } else {
    std::cout << "** Could not get updated event: " << status.error_message()
              << std::endl;
  }
  std::cout << std::endl;
}

void CalendarSample::DeleteCalendar(const string& id) {
  std::unique_ptr<CalendarsResource_DeleteMethod> method(
      service_->get_calendars().NewDeleteMethod(&credential_, id));

  if (!method->Execute().ok()) {
    DisplayError(method.get());
    return;
  }
  std::cout << "Deleted ID=" << id << std::endl;
  std::cout << std::endl;
}


void CalendarSample::Run() {
  std::cout << kSampleStepPrefix << "Getting User Authorization" << std::endl;
  googleapis::util::Status status = Authorize();
  if (!status.ok()) {
    std::cout << "Could not authorize: " << status.error_message() << std::endl;
    return;
  }

  std::cout << std::endl
            << kSampleStepPrefix << "Showing Initial Calendars" << std::endl;
  ShowCalendars();

  std::cout << std::endl
            << kSampleStepPrefix << "Adding Calendar" << std::endl;
  string calendar_id =  AddCalendar();

  std::cout << std::endl
            << kSampleStepPrefix << "Showing Updated Calendars" << std::endl;
  ShowCalendars();

  DateTime now;
  std::unique_ptr<Event> event(Event::New());
  event->set_summary("Calendar event added by CalendarSample");
  event->mutable_start().set_date_time(now);
  event->mutable_end().set_date_time(DateTime(now.ToEpochTime() + 60 * 60));

  std::cout << std::endl
            << kSampleStepPrefix << "Add Calendar Event" << std::endl;
  AddEvent(calendar_id, event.get());

  std::cout << std::endl
            << kSampleStepPrefix << "Patch Calendar Event" << std::endl;
  event->clear_start();
  event->clear_end();
  event->set_summary("Event patched by CalendarSample");
  PatchEvent(calendar_id, *event);

  std::cout << std::endl
            << kSampleStepPrefix << "Update Calendar Event" << std::endl;
  // An update requires a time.
  // Go back a year and one day to distinguish it from the old value.
  event->mutable_start().set_date_time(
      DateTime(now.ToEpochTime() - 60 * 60 * 24 * 367));
  event->mutable_end().set_date_time(
      DateTime(now.ToEpochTime() - 60 * 60 * 24 * 366));
  event->clear_summary();
  UpdateEvent(calendar_id, *event);

  std::cout << std::endl
            << "Adding bulk events using a batch request" << std::endl;
  HttpRequestBatch batch(service_->transport(), service_->batch_url());
  batch.mutable_http_request()->set_credential(&credential_);

  for (int i = 0; i < 10; ++i) {
    std::unique_ptr<Event> the_event(Event::New());

    // Space the events at hour intervals with 15 minute durations.
    the_event->set_summary(StrCat("Extra event ", i));
    the_event->mutable_start().set_date_time(
        DateTime(now.ToEpochTime() + i * 60 * 60));
    the_event->mutable_end().set_date_time(
        DateTime(now.ToEpochTime() + i * 60 * 60 + 15 * 60));

    EventsResource_InsertMethod* method(
        service_->get_events().NewInsertMethod(
            &credential_, calendar_id, *the_event));

    method->ConvertIntoHttpRequestBatchAndDestroy(&batch);
  }

  status = batch.Execute();
  if (!status.ok()) {
    std::cout << "Entire batch execution failed: " << status.error_message()
              << std::endl;
  }
  for (int i = 0; i < 10; ++i) {
    HttpResponse* response = batch.requests()[i]->response();
    if (!response->ok()) {
      string detail;
      if (response->body_reader()) {
        detail = response->body_reader()->RemainderToString();
      } else {
        detail = "No response data available.";
      }
      std::cout << "Error adding batched event " << i << std::endl
                << response->status().ToString() << std::endl
                << detail << std::endl;
    }
  }

  PageThroughAllEvents(calendar_id, 7);
  std::cout << std::endl
            << kSampleStepPrefix << "Deleting Calendar" << std::endl;
  DeleteCalendar(calendar_id);

  std::cout << std::endl
            << kSampleStepPrefix << "Showing Final Calendars" << std::endl;
  ShowCalendars();
}


}  // namespace googleapis

using namespace googleapis;
int main(int argc, char* argv[]) {

  googleapis::util::Status status = CalendarSample::Startup(argc, argv);
  if (!status.ok()) {
    std::cerr << "Could not initialize application." << std::endl;
    std::cerr << status.error_message() << std::endl;
    return -1;
  }

  CalendarSample sample;
  sample.Run();
  std::cout << "Done!" << std::endl;

  return 0;
}

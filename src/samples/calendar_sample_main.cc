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

// Author: ewiseblatt@google.com (Eric Wiseblatt)
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
#include "googleapis/client/auth/file_credential_store.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/data/data_reader.h"
#if HAVE_OPENSSL
#include "googleapis/client/data/openssl_codec.h"
#endif
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/scoped_ptr.h"
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

const StringPiece kSampleStepPrefix = "SAMPLE:  ";

static util::Status PromptShellForAuthorizationCode(
    OAuth2AuthorizationFlow* flow,
    const OAuth2RequestOptions& options,
    string* authorization_code) {
  string url = flow->GenerateAuthorizationCodeRequestUrlWithOptions(options);
  cout << "Enter the following URL into a browser:\n" << url << endl;
  cout << endl;
  cout << "Enter the browser's response to confirm authorization: ";

  authorization_code->clear();
  cin >> *authorization_code;
  if (authorization_code->empty()) {
    return StatusCanceled("Canceled");
  } else {
    return StatusOk();
  }
}

static util::Status ValidateUserName(const string& name) {
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
  cout << "====  ERROR  ====" << endl;
  cout << "Status: " << response.status().error_message() << endl;
  if (response.transport_status().ok()) {
    cout << "HTTP Status Code = " << response.http_code() << endl;
    cout << endl << response.body_reader()->RemainderToString() << endl;
  }
  cout << endl;
}

void Display(const string& prefix, const CalendarListEntry& entry) {
  cout << prefix << "CalendarListEntry" << endl;
  cout << prefix << "  ID: " << entry.get_id() << endl;
  cout << prefix << "  Summary: " << entry.get_summary() << endl;
  if (entry.has_description()) {
    cout << prefix << "  Description: " << entry.get_description() << endl;
  }
}

void Display(const string& prefix, const Calendar& entry) {
  cout << prefix << "Calendar" << endl;
  cout << prefix << "  ID: " << entry.get_id() << endl;
  cout << prefix << "  Summary: " << entry.get_summary() << endl;
  if (!entry.get_description().empty()) {
    cout << prefix << "  Description: " << entry.get_description() << endl;
  }
}

void Display(const string& prefix, const Event& event) {
  cout << prefix << "Event" << endl;
  cout << prefix << "  ID: " << event.get_id() << endl;
  if (event.has_summary()) {
    cout << prefix << "  Summary: " << event.get_summary() << endl;
  }
  if (event.get_start().has_date_time()) {
    cout << prefix << "  Start Time: "
         << event.get_start().get_date_time().ToString() << endl;
  }
  if (event.get_end().has_date_time()) {
    cout << prefix << "  End Time: "
         << event.get_end().get_date_time().ToString() << endl;
  }
}

template <class LIST, typename ELEM>
void DisplayList(
    const string& prefix, const StringPiece& title, const LIST& list) {
  cout << prefix << "====  " << title << "  ====" << endl;
  string sub_prefix = StrCat(prefix, "  ");
  bool first = true;
  const JsonCppArray<ELEM>& items = list.get_items();
  for (typename JsonCppArray<ELEM>::const_iterator it = items.begin();
       it != items.end();
       ++it) {
    if (first) {
      first = false;
    } else {
      cout << endl;
    }
    Display(sub_prefix, *it);
  }
  if (first) {
    cout << sub_prefix << "<no items>" << endl;
  }
}

class CalendarSample {
 public:
  static util::Status Startup(int argc, char* argv[]);
  void Run();

 private:
  // Gets authorization to access the user's personal calendar data.
  util::Status Authorize();

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
  util::Status GetEvent(
      const string& calendar_id, const StringPiece& event_id, Event* event);

  // Demonstrates patching a resource.
  // For this example, it is a calendar event.
  void PatchEvent(const string& calendar_id, const Event& event);

  // Demonstrates updating a resource.
  // For this example, it is a calendar event.
  void UpdateEvent(const string& calendar_id, const Event& event);

  OAuth2Credential credential_;
  static scoped_ptr<CalendarService> service_;
  static scoped_ptr<OAuth2AuthorizationFlow> flow_;
  static scoped_ptr<HttpTransportLayerConfig> config_;
};

// static
scoped_ptr<CalendarService> CalendarSample::service_;
scoped_ptr<OAuth2AuthorizationFlow> CalendarSample::flow_;
scoped_ptr<HttpTransportLayerConfig> CalendarSample::config_;


/* static */
util::Status CalendarSample::Startup(int argc, char* argv[]) {
  if ((argc < 2) || (argc > 3)) {
    string error =
        StrCat("Invalid Usage:\n",
               argv[0], " <client_secrets_file> [<cacerts_path>]\n");
    return StatusInvalidArgument(error);
  }

  // Set up HttpTransportLayer.
  util::Status status;
  config_.reset(new HttpTransportLayerConfig);
  client::HttpTransportFactory* factory =
      new client::CurlHttpTransportFactory(config_.get());
  config_->ResetDefaultTransportFactory(factory);
  if (argc > 2) {
    config_->mutable_default_transport_options()->set_cacerts_path(argv[2]);
  }

  // Set up OAuth 2.0 flow for getting credentials to access personal data.
  const StringPiece client_secrets_file = argv[1];
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
  cout
    << endl
    << "Welcome to the Google APIs for C++ CalendarSample.\n"
    << "  You will need to authorize this program to look at your calendar.\n"
    << "  If you would like to save these credentials between runs\n"
    << "  (or restore from an earlier run) then enter a User ID.\n"
    << "  Otherwise just press return.\n"
    << endl
    << "  ID: ";
  string id;
  std::getline(cin, id);
  if (!id.empty()) {
    util::Status status = ValidateUserName(id);
    if (!status.ok()) {
      return status;
    }
  }

  OAuth2RequestOptions options;
  options.user_id = id;
  util::Status status =
        flow_->RefreshCredentialWithOptions(options, &credential_);
  if (!status.ok()) {
    return status;
  }

  credential_.set_flow(flow_.get());
  cout << "Authorized " << id << endl;
  return StatusOk();
}


void CalendarSample::ShowCalendars() {
  scoped_ptr<CalendarListResource_ListMethod> method(
      service_->get_calendar_list().NewListMethod(&credential_));

  scoped_ptr<CalendarList> calendar_list(CalendarList::New());
  if (!method->ExecuteAndParseResponse(calendar_list.get()).ok()) {
    DisplayError(method.get());
    return;
  }
  DisplayList<CalendarList, CalendarListEntry>(
      "", "CalendarList", *calendar_list);
  cout << endl;
}

string CalendarSample::AddCalendar() {
  scoped_ptr<Calendar> calendar(Calendar::New());
  calendar->set_summary("Calendar added by CalendarSample");

  scoped_ptr<CalendarsResource_InsertMethod> method(
      service_->get_calendars().NewInsertMethod(&credential_, *calendar));

  if (!method->ExecuteAndParseResponse(calendar.get()).ok()) {
    DisplayError(method.get());
    return "";
  }

  string result = calendar->get_id().as_string();
  cout << "Added new calendar ID=" << result << ":" << endl;
  Display("  ", *calendar);
  cout << endl;

  return result;
}

void CalendarSample::AddEvent(const string& calendar_id, Event* event) {
  scoped_ptr<EventsResource_InsertMethod> method(
      service_->get_events().NewInsertMethod(
          &credential_, calendar_id, *event));

  if (!method->ExecuteAndParseResponse(event).ok()) {
    DisplayError(method.get());
    return;
  }

  cout << "Added new event ID=" << event->get_id() << ":" << endl;
  Display("  ", *event);
  cout << endl;
}

void CalendarSample::PageThroughAllEvents(
    const string& calendar_id, int num_per_page) {
  cout << "All Events" << endl;
  scoped_ptr<EventsResource_ListMethodPager> pager(
      service_->get_events().NewListMethodPager(&credential_, calendar_id));
  pager->request()->set_max_results(num_per_page);
  while (pager->NextPage()) {
    DisplayList<Events, Event>("  ", "EventList", *pager->data());
  }
}


util::Status CalendarSample::GetEvent(
    const string& calendar_id, const StringPiece& event_id, Event* event) {
  scoped_ptr<EventsResource_GetMethod> method(
      service_->get_events().NewGetMethod(
          &credential_, calendar_id, event_id));

  return method->ExecuteAndParseResponse(event);
}

void CalendarSample::PatchEvent(
    const string& calendar_id, const Event& event) {
  scoped_ptr<EventsResource_PatchMethod> method(
      service_->get_events().NewPatchMethod(
          &credential_, calendar_id, event.get_id(), event));

  if (!method->Execute().ok()) {
    DisplayError(method.get());
    return;
  }

  scoped_ptr<Event> cloud_event(Event::New());
  util::Status status =
        GetEvent(calendar_id, event.get_id(), cloud_event.get());
  if (status.ok()) {
    cout << "Patched event:" << endl;
    Display("  ", *cloud_event);
  } else {
    cout << "** Could not get patched event: "
         << status.error_message() << endl;
  }
  cout << endl;
}

void CalendarSample::UpdateEvent(
    const string& calendar_id, const Event& event) {
  scoped_ptr<EventsResource_UpdateMethod> method(
      service_->get_events().NewUpdateMethod(
          &credential_, calendar_id, event.get_id(), event));

  if (!method->Execute().ok()) {
    DisplayError(method.get());
    return;
  }

  scoped_ptr<Event> cloud_event(Event::New());
  util::Status status =
        GetEvent(calendar_id, event.get_id(), cloud_event.get());
  if (status.ok()) {
    cout << "Updated event:" << endl;
    Display("  ", *cloud_event);
  } else {
    cout << "** Could not get updated event: "
         << status.error_message() << endl;
  }
  cout << endl;
}

void CalendarSample::DeleteCalendar(const string& id) {
  scoped_ptr<CalendarsResource_DeleteMethod> method(
      service_->get_calendars().NewDeleteMethod(&credential_, id));

  if (!method->Execute().ok()) {
    DisplayError(method.get());
    return;
  }
  cout << "Deleted ID=" << id << endl;
  cout << endl;
}


void CalendarSample::Run() {
  cout << kSampleStepPrefix << "Getting User Authorization" << endl;
  util::Status status = Authorize();
  if (!status.ok()) {
    cout << "Could not authorize: " << status.error_message() << endl;
    return;
  }

  cout << endl << kSampleStepPrefix << "Showing Initial Calendars" << endl;
  ShowCalendars();

  cout << endl << kSampleStepPrefix << "Adding Calendar" << endl;
  string calendar_id =  AddCalendar();

  cout << endl << kSampleStepPrefix << "Showing Updated Calendars" << endl;
  ShowCalendars();

  DateTime now;
  scoped_ptr<Event> event(Event::New());
  event->set_summary("Calendar event added by CalendarSample");
  event->mutable_start().set_date_time(now);
  event->mutable_end().set_date_time(DateTime(now.ToEpochTime() + 60 * 60));

  cout << endl << kSampleStepPrefix << "Add Calendar Event" << endl;
  AddEvent(calendar_id, event.get());

  cout << endl << kSampleStepPrefix << "Patch Calendar Event" << endl;
  event->clear_start();
  event->clear_end();
  event->set_summary("Event patched by CalendarSample");
  PatchEvent(calendar_id, *event);

  cout << endl << kSampleStepPrefix << "Update Calendar Event" << endl;
  // An update requires a time.
  // Go back a year and one day to distinguish it from the old value.
  event->mutable_start().set_date_time(
      DateTime(now.ToEpochTime() - 60 * 60 * 24 * 367));
  event->mutable_end().set_date_time(
      DateTime(now.ToEpochTime() - 60 * 60 * 24 * 366));
  event->clear_summary();
  UpdateEvent(calendar_id, *event);

  cout << endl << "Adding bulk events" << endl;
  for (int i = 0; i < 10; ++i) {
    scoped_ptr<Event> the_event(Event::New());

    // Space the events at hour intervals with 15 minute durations.
    the_event->set_summary(StrCat("Extra event ", i));
    the_event->mutable_start().set_date_time(
        DateTime(now.ToEpochTime() + i * 60 * 60));
    the_event->mutable_end().set_date_time(
        DateTime(now.ToEpochTime() + i * 60 * 60 + 15 * 60));

    scoped_ptr<EventsResource_InsertMethod> method(
        service_->get_events().NewInsertMethod(
            &credential_, calendar_id, *the_event));

    if (!method->Execute().ok()) {
      cout << "Error adding event " << i << endl
           << method->http_response()->body_reader()->RemainderToString()
           << endl;
      break;
    }
  }

  PageThroughAllEvents(calendar_id, 7);
  cout << endl << kSampleStepPrefix << "Deleting Calendar" << endl;
  DeleteCalendar(calendar_id);

  cout << endl << kSampleStepPrefix << "Showing Final Calendars" << endl;
  ShowCalendars();
}


} // namespace googleapis

using namespace googleapis;
int main(int argc, char* argv[]) {

  util::Status status = CalendarSample::Startup(argc, argv);
  if (!status.ok()) {
    cerr << "Could not initialize application." << endl;
    cerr << status.error_message() << endl;
    return -1;
  }

  CalendarSample sample;
  sample.Run();
  cout << "Done!" << endl;

  return 0;
}

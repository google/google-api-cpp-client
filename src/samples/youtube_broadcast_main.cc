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
 * Demo of inserting a broadcast and a stream then binding them together
 * using the YouTube Live API (V3) with OAuth2 for authorization.
 *
 * To run this sample you must have a Google APIs Project that enables
 * YouTube Data API using the cloud console as described in the
 * "Getting Started" document at
 * http://google.github.io/google-api-cpp-client/latest/start/get_started.html
 *
 * Run the sample with the command line
 *    --client_secrets_path=<path>
 * where <path> is the path to the Client Secrets file you downloaded for
 * your project. Be sure the file has only user read-only permissions to
 * satisfy the security checks.
 *
 * Type the command "help" for a list of available commands in this sample.
 */

#include <string>
using std::string;


#include "samples/command_processor.h"
#include "samples/installed_application.h"
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/status.h"
#include <gflags/gflags.h>
#include "googleapis/strings/numbers.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/util/status.h"

#include "google/youtube_api/youtube_api.h"

namespace googleapis {

// Small number to make paging easier to demonstrate.
DEFINE_int32(max_results, 5, "Max results per page");
DEFINE_string(client_secrets_path, "",
              "REQUIRED: Path to JSON client_secrets file for OAuth.");

using std::cerr;
using std::cout;
using std::endl;

using client::JsonCppArray;
using client::DateTime;
using sample::InstalledServiceApplication;

using google_youtube_api::CdnSettings;
using google_youtube_api::LiveBroadcast;
using google_youtube_api::LiveBroadcastContentDetails;
using google_youtube_api::LiveBroadcastSnippet;
using google_youtube_api::LiveBroadcastStatus;
using google_youtube_api::LiveBroadcastsResource_BindMethod;
using google_youtube_api::LiveBroadcastsResource_DeleteMethod;
using google_youtube_api::LiveBroadcastsResource_InsertMethod;
using google_youtube_api::LiveBroadcastsResource_ListMethodPager;
using google_youtube_api::LiveStream;
using google_youtube_api::LiveStreamSnippet;
using google_youtube_api::LiveStreamStatus;
using google_youtube_api::LiveStreamsResource_DeleteMethod;
using google_youtube_api::LiveStreamsResource_InsertMethod;
using google_youtube_api::LiveStreamsResource_ListMethodPager;
using google_youtube_api::YouTubeService;

static void DumpLiveBroadcastList(const JsonCppArray<LiveBroadcast>& list) {
  for (JsonCppArray<LiveBroadcast>::const_iterator it = list.begin();
       it != list.end();
       ++it) {
    const LiveBroadcast& bcast = *it;
    const LiveBroadcastSnippet& snippet = bcast.get_snippet();
    const LiveBroadcastContentDetails& details = bcast.get_content_details();
    string bound_id =
        details.has_bound_stream_id()
        ? details.get_bound_stream_id().as_string()
        : "<No Bound Stream>";
    cout << "  ID=" << bcast.get_id() << "\n"
         << "    StreamID=" << bound_id << "\n"
         << "    Start="
         << snippet.get_scheduled_start_time().ToString() << "\n"
         << "    Title=" << snippet.get_title() << endl;
  }
}

static void DumpLiveStreamList(const JsonCppArray<LiveStream>& list) {
  for (JsonCppArray<LiveStream>::const_iterator it = list.begin();
       it != list.end();
       ++it) {
    const LiveStream& stream = *it;
    string format =
        stream.has_cdn()
        ? stream.get_cdn().get_format().as_string()
        : "<No CDN available>";
    cout << "  ID=" << stream.get_id() << "\n"
         << "    Format=" << format << "\n"
         << "    ChannelID=" << stream.get_snippet().get_channel_id() << "\n"
         << "    Title=" << stream.get_snippet().get_title() << endl;
  }
}

/*
 * Configures and manages YouTubeService instance and OAuth2.0 flow we'll use.
 *
 * The actual functionality for the sample is in the command processor.
 *
 * @see YouTubeBroadcastCommandProcessor
 */
class YouTubeBroadcastSampleApplication
    : public InstalledServiceApplication<YouTubeService> {
 public:
  YouTubeBroadcastSampleApplication()
      : InstalledServiceApplication<YouTubeService>("YouTubeBroadcastSample") {
    std::vector<string>* scopes = mutable_default_oauth2_scopes();
    scopes->push_back(YouTubeService::SCOPES::YOUTUBE);
    scopes->push_back(YouTubeService::SCOPES::YOUTUBE_READONLY);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(YouTubeBroadcastSampleApplication);
};


/*
 * Implements the commands available for this sample.
 */
class YouTubeBroadcastCommandProcessor : public sample::CommandProcessor {
 public:
  explicit YouTubeBroadcastCommandProcessor(
      YouTubeBroadcastSampleApplication* app) : app_(app) {}
  virtual ~YouTubeBroadcastCommandProcessor() {}

  virtual void Init() {
    AddBuiltinCommands();

    AddCommand("authorize",
       new CommandEntry(
           "user_name [refresh token]",
           "Re-authorize user [with refresh token].\n"
               "The user_name is only used for persisting the credentials.\n"
               "The credentials will be persisted under the directory "
               "$HOME/.googleapis/user_name.\n"
               "If refresh token is empty then authorize interactively.",
           NewPermanentCallback(
               this, &YouTubeBroadcastCommandProcessor::AuthorizeHandler)));

    AddCommand(
        "revoke",
        new CommandEntry(
            "",
            "Revoke authorization. You will need to reauthorize again.\n",
            NewPermanentCallback(
                this, &YouTubeBroadcastCommandProcessor::RevokeHandler)));

    AddCommand(
        "create",
        new CommandEntry(
            "<start date> <minutes> <title>", "Create a new broadcast.",
            NewPermanentCallback(
                this,
                &YouTubeBroadcastCommandProcessor::CreateBroadcastHandler)));


    AddCommand(
        "delete",
        new CommandEntry(
            "<broadcast|stream> <ID>",
            "Deletes the live [broadcast or stream] resource with given ID.",
            NewPermanentCallback(
                this,
                &YouTubeBroadcastCommandProcessor::DeleteLiveHandler)));


    AddCommand(
        "broadcasts",
        new CommandEntry(
            "", "List your broadcasts. Can page through using 'next'.",
            NewPermanentCallback(
                this,
                &YouTubeBroadcastCommandProcessor::ListBroadcastsHandler)));

    AddCommand(
        "streams",
        new CommandEntry(
            "", "List your Streams. Can page through using 'next'.",
            NewPermanentCallback(
                this,
                &YouTubeBroadcastCommandProcessor::ListStreamsHandler)));

    AddCommand(
        "next",
        new CommandEntry(
            "", "List the next page since the previous 'list' or 'next'.",
            NewPermanentCallback(
                this,
                &YouTubeBroadcastCommandProcessor::NextBroadcastsHandler)));
  }

 private:
  void AuthorizeHandler(const string& cmd, const std::vector<string>& args) {
    if (args.size() == 0 || args.size() > 2) {
      cout << "no user_name provided." << endl;
      return;
    }

    googleapis::util::Status status = app_->ChangeUser(args[0]);
    if (status.ok()) {
      status = app_->AuthorizeClient();
    }

    if (status.ok()) {
      cout << "Authorized as user '" << args[0] << "'" << endl;
    } else {
      cerr << status.ToString();
    }
  }

  /*
   * Implements "revoke" command.
   *
   * Apps dont typically do this and rely on Google's User Consoles and
   * Dashboards to allow users to revoke permissions across applications.
   * However it is convienent, especially here, to allow the app to revoke
   * its permissions.
   */
  void RevokeHandler(const string&, const std::vector<string>&) {
    app_->RevokeClient().IgnoreError();
  }

  /*
   * Implements the "create" command.
   */
  void CreateBroadcastHandler(
      const string& cmd, const std::vector<string>& args) {
    if (args.size() != 3) {
      cout << "Expected <start time> <minutes> <title>." << endl;
      return;
    }
    DateTime start_time(args[0]);
    if (!start_time.is_valid()) {
      cout << "Expected start time in format <YYYY-MM-DD>T<HH:MM:SS>Z" << endl;
      return;
    }

    int32 mins;
    if (!safe_strto32(args[1].c_str(), &mins)) {
      cout << "<minutes> was not a number." << endl;
      return;
    }
    DateTime end_time(start_time.ToEpochTime() + mins * 60);

    const string& title = args[2];

    std::unique_ptr<LiveBroadcast> broadcast(LiveBroadcast::New());

    LiveBroadcastSnippet broadcast_snippet = broadcast->mutable_snippet();
    broadcast_snippet.set_title(title);
    broadcast_snippet.set_scheduled_start_time(start_time);
    broadcast_snippet.set_scheduled_end_time(end_time);

    LiveBroadcastStatus broadcast_status = broadcast->mutable_status();
    broadcast_status.set_privacy_status("private");

    string broadcast_parts = "snippet,status";
    std::unique_ptr<LiveBroadcastsResource_InsertMethod> insert_broadcast(
        app_->service()->get_live_broadcasts().NewInsertMethod(
            app_->credential(), broadcast_parts, *broadcast.get()));
    std::unique_ptr<LiveBroadcast> got_broadcast(LiveBroadcast::New());
    insert_broadcast->ExecuteAndParseResponse(
        got_broadcast.get()).IgnoreError();
    if (!CheckAndLogResponse(insert_broadcast->http_response())) {
      return;
    }
    cout << "Inserted LiveBroadcast ID=" << got_broadcast->get_id() << endl;

    std::unique_ptr<LiveStream> stream(LiveStream::New());

    LiveStreamSnippet stream_snippet = stream->mutable_snippet();
    stream_snippet.set_title(string("Stream for ")+title);

    CdnSettings stream_cdn = stream->mutable_cdn();
    stream_cdn.set_format("1080p");
    stream_cdn.set_ingestion_type("rtmp");

    string stream_parts = "snippet,cdn";
    std::unique_ptr<LiveStreamsResource_InsertMethod> insert_stream(
        app_->service()->get_live_streams().NewInsertMethod(
            app_->credential(), stream_parts, *stream.get()));
    std::unique_ptr<LiveStream> got_stream(LiveStream::New());
    insert_stream->ExecuteAndParseResponse(got_stream.get()).IgnoreError();
    if (!CheckAndLogResponse(insert_stream->http_response())) {
      return;
    }
    cout << "Inserted LiveStream id=" << got_stream->get_id() << endl;

    std::unique_ptr<LiveBroadcastsResource_BindMethod> bind(
        app_->service()->get_live_broadcasts().NewBindMethod(
            app_->credential(), got_broadcast->get_id(), "id,contentDetails"));
    bind->set_stream_id(got_stream->get_id().as_string());
    std::unique_ptr<LiveBroadcast> bound_broadcast(LiveBroadcast::New());
    bind->ExecuteAndParseResponse(bound_broadcast.get()).IgnoreError();
    if (!CheckAndLogResponse(bind->http_response())) {
      return;
    }

    cout << "Bound Broadcast is:\n" << *bound_broadcast.get() << endl;
  }

  /*
   * Implements the "delete" command.
   */
  void DeleteLiveHandler(
      const string& cmd, const std::vector<string>& args) {
    if (args.size() != 2) {
      cout << "Expected <broadcast|stream> <ID>." << endl;
      return;
    }
    if (args[0] == "broadcast") {
      DeleteLiveBroadcastHandler(cmd, args);
    } else if (args[0] == "stream") {
      DeleteLiveStreamHandler(cmd, args);
    } else {
      cout << "Expected <broadcast|stream> <ID>." << endl;
    }
  }

  /*
   * Implements the "broadcasts" command.
   *
   * @see NextBroadcastsHandler
   */
  void ListBroadcastsHandler(const string&, const std::vector<string>&) {
    streams_pager_.reset(NULL);
    const YouTubeService::LiveBroadcastsResource& rsrc
        = app_->service()->get_live_broadcasts();

    // We could use a LiveBroadcastsResource_ListMethod but we'll instead use
    // a pager so that we can play with it. Reset the old one (if any).
    // The 'next' command will advance the pager.
    const string& parts = "id,snippet";
    broadcasts_pager_.reset(rsrc.NewListMethodPager(app_->credential(), parts));
    broadcasts_pager_->request()->set_max_results(FLAGS_max_results);
    broadcasts_pager_->request()->set_broadcast_status("all");

    cout << "Getting (partial) broadcast list..." << endl;
    bool ok = broadcasts_pager_->NextPage();
    CheckAndLogResponse(broadcasts_pager_->http_response());
    if (ok) {
      DumpLiveBroadcastList(broadcasts_pager_->data()->get_items());
    }

    if (broadcasts_pager_->is_done()) {
      cout << "There are no more results to page through." << endl;
    } else {
      cout << "\nEnter 'next' to see the next page of results." << endl;
    }
  }

  /*
   * Implements the "streams" command.
   *
   * @see NextStreamsHandler
   */
  void ListStreamsHandler(const string&, const std::vector<string>&) {
    broadcasts_pager_.reset(NULL);
    const YouTubeService::LiveStreamsResource& rsrc
        = app_->service()->get_live_streams();

    // We could use a LiveStreamsResource_ListMethod but we'll instead use
    // a pager so that we can play with it. Reset the old one (if any).
    // The 'next' command will advance the pager.
    const string& parts = "id,snippet";
    streams_pager_.reset(rsrc.NewListMethodPager(app_->credential(), parts));
    streams_pager_->request()->set_max_results(FLAGS_max_results);
    streams_pager_->request()->set_mine(true);  // list only my streams.

    cout << "Getting (partial) stream list..." << endl;
    bool ok = streams_pager_->NextPage();
    CheckAndLogResponse(streams_pager_->http_response());
    if (ok) {
      DumpLiveStreamList(streams_pager_->data()->get_items());
    }

    if (streams_pager_->is_done()) {
      cout << "There are no more results to page through." << endl;
    } else {
      cout << "\nEnter 'next' to see the next page of results." << endl;
    }
  }

  /*
   * Implements the "next" command.
   */
  void NextHandler(const string& cmd, const std::vector<string>& args) {
    if (broadcasts_pager_.get()) {
      NextBroadcastsHandler(cmd, args);
    } else if (streams_pager_.get()) {
      NextStreamsHandler(cmd, args);
    } else {
      cout << "You must ask for 'broadcasts' or 'streams' first.";
    }
  }

  /*
   * Implements the "next" command for listing broadcasts.
   *
   * @see ListBroadcastsHandler
   */
  void NextBroadcastsHandler(const string&, const std::vector<string>&) {
    CHECK(broadcasts_pager_.get());

    cout << "Getting next page of broadcast list..." << endl;
    bool ok = broadcasts_pager_->NextPage();
    CheckAndLogResponse(broadcasts_pager_->http_response());
    if (ok) {
      DumpLiveBroadcastList(broadcasts_pager_->data()->get_items());
    }

    if (broadcasts_pager_->is_done()) {
      cout << "There are no more results to page through." << endl;
    } else {
      cout << "\nEnter 'next' to see the next page of results." << endl;
    }
  }

  /*
   * Implements the "next" command for listing streams.
   *
   * @see ListStreamsHandler
   */
  void NextStreamsHandler(const string&, const std::vector<string>&) {
    CHECK(streams_pager_.get());

    cout << "Getting next page of streams list..." << endl;
    bool ok = streams_pager_->NextPage();
    CheckAndLogResponse(streams_pager_->http_response());
    if (ok) {
      DumpLiveStreamList(streams_pager_->data()->get_items());
    }

    if (streams_pager_->is_done()) {
      cout << "There are no more results to page through." << endl;
    } else {
      cout << "\nEnter 'next' to see the next page of results." << endl;
    }
  }

  void DeleteLiveBroadcastHandler(
      const string&, const std::vector<string>& args) {
    string id(args[1]);
    std::unique_ptr<LiveBroadcastsResource_DeleteMethod> delete_broadcast(
        app_->service()->get_live_broadcasts().NewDeleteMethod(
            app_->credential(), id));

    delete_broadcast->Execute().IgnoreError();
    if (!CheckAndLogResponse(delete_broadcast->http_response())) {
      return;
    }
    cout << "Deleted LiveBroadcast ID=" << id << endl;
  }

  void DeleteLiveStreamHandler(
      const string&, const std::vector<string>& args) {
    string id(args[1]);
    std::unique_ptr<LiveStreamsResource_DeleteMethod> delete_broadcast(
        app_->service()->get_live_streams().NewDeleteMethod(
            app_->credential(), id));

    delete_broadcast->Execute().IgnoreError();
    if (!CheckAndLogResponse(delete_broadcast->http_response())) {
      return;
    }
    cout << "Deleted LiveStream ID=" << id << endl;
  }

  // At most one of these will not be NULL indicating which thing we
  // are listing so that we can have the command "next" apply to either
  // without being ambiguous. When we list "streams" or "broadcasts" it will
  // instantiate the pager we want and NULL the other one out.
  std::unique_ptr<LiveBroadcastsResource_ListMethodPager> broadcasts_pager_;
  std::unique_ptr<LiveStreamsResource_ListMethodPager> streams_pager_;

  YouTubeBroadcastSampleApplication* app_;
  DISALLOW_COPY_AND_ASSIGN(YouTubeBroadcastCommandProcessor);
};


}  // namespace googleapis

using namespace googleapis;
int main(int argc, char** argv) {
  YouTubeBroadcastSampleApplication app;
  googleapis::util::Status status = app.Init(FLAGS_client_secrets_path);
  if (!status.ok()) {
    cerr << "Could not initialize application." << endl;
    cerr << status.error_message() << endl;
    return -1;
  }

  YouTubeBroadcastCommandProcessor processor(&app);
  processor.Init();
  processor.set_log_success_bodies(true);
  processor.RunShell();

  return 0;
}

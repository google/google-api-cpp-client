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
// Note that in this example we often IgnoreError when Executing.
// This is because we look at the status in the response and detect errors
// there. Checking the result of Execute would be redundant. The IgnoreError
// calls are just to explicitly acknowledge the googleapis::util::Status returned by
// Execute.
//
// If we were automatically destroying the request then it would not be
// valid when Execute() returns and would need to use the googleapis::util::Status
// returned by Execute in order to check for errors.

#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include <memory>
#include <string>
using std::string;
#include <vector>

#include "samples/command_processor.h"
#include "samples/installed_application.h"

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/data/file_data_writer.h"
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/service/media_uploader.h"
#include "google/drive_api/drive_api.h"
#include <gflags/gflags.h>
#include "googleapis/base/macros.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

DEFINE_int32(max_results, 5, "Max results for listing files.");
DEFINE_string(client_secrets_path, "",
              "REQUIRED: Path to JSON client_secrets file for OAuth.");
DEFINE_int32(port, 0,
             "If specified, use this port with an httpd for OAuth 2.0");

using std::cin;
using std::cerr;
using std::cout;
using std::endl;

using client::HttpTransport;
using client::HttpRequest;
using client::HttpResponse;
using client::JsonCppArray;  // only because we arent using C++x11
using client::JsonCppAssociativeArray;  // because not using C++x11
using client::ClientService;
using client::DataReader;
using client::NewUnmanagedFileDataReader;
using client::StatusOk;
using client::StatusUnknown;

using google_drive_api::DriveService;
using google_drive_api::AboutResource_GetMethod;
using google_drive_api::FilesResource_DeleteMethod;
using google_drive_api::FilesResource_GetMethod;
using google_drive_api::FilesResource_InsertMethod;
using google_drive_api::FilesResource_ListMethod;
using google_drive_api::FilesResource_ListMethodPager;
using google_drive_api::FilesResource_TrashMethod;
using google_drive_api::FilesResource_UpdateMethod;
using google_drive_api::RevisionsResource_GetMethod;
using google_drive_api::RevisionsResource_ListMethod;

using sample::InstalledServiceApplication;
using sample::CommandProcessor;


/**
 * Example of a writer which could provide download progress.
 */
class ProgressMeterDataWriter : public client::FileDataWriter {
 public:
  explicit ProgressMeterDataWriter(const string& path)
      : client::FileDataWriter(path, FileOpenOptions()) {
  }

  ~ProgressMeterDataWriter() override {
    cout << "~ProgressMeterDataWriter" << endl;
  }

  googleapis::util::Status DoWrite(int64 bytes, const char* buffer) override {
    // In a real application, we might callback to the UI to display here.
    cout << "*** Got another " << bytes << " bytes." << endl;
    return client::FileDataWriter::DoWrite(bytes, buffer);
  }

  std::unique_ptr<client::DataWriter> file_writer_;
};


class DriveUtilApplication : public InstalledServiceApplication<DriveService> {
 public:
  DriveUtilApplication()
      : InstalledServiceApplication<DriveService>("GDriveUtil") {
    std::vector<string>* scopes = mutable_default_oauth2_scopes();
    scopes->push_back(DriveService::SCOPES::DRIVE_READONLY);
    scopes->push_back(DriveService::SCOPES::DRIVE_FILE);
    scopes->push_back(DriveService::SCOPES::DRIVE);
    // Not adding metadata scope because I dont think I'm using
    // anything needing it
  }

 protected:
  virtual googleapis::util::Status InitServiceHelper() {
    if (FLAGS_port > 0) {
      client::WebServerAuthorizationCodeGetter::AskCallback*
          asker = NewPermanentCallback(
              &client::WebServerAuthorizationCodeGetter
              ::PromptWithCommand,
              "/usr/bin/firefox", "\"$URL\"");
              // "/opt/google/chrome/google-chrome", "--app=\"$URL\"");

      return StartupHttpd(FLAGS_port, "/oauth", asker);
    }
    return StatusOk();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DriveUtilApplication);
};


class DriveCommandProcessor : public sample::CommandProcessor {
 public:
  explicit DriveCommandProcessor(DriveUtilApplication* app) : app_(app) {}
  virtual ~DriveCommandProcessor() {}

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
               this, &DriveCommandProcessor::AuthorizeHandler)));

    AddCommand("revoke",
       new CommandEntry(
           "",
           "Revoke authorization. You will need to reauthorize again.\n",
           NewPermanentCallback(
               this, &DriveCommandProcessor::RevokeHandler)));

    AddCommand("about",
       new CommandEntry(
           "", "Get information about yourself and drive settings.",
           NewPermanentCallback(this, &DriveCommandProcessor::AboutHandler)));

    AddCommand("list",
       new CommandEntry(
           "", "List your files. Can page through using 'next'.",
           NewPermanentCallback(
               this, &DriveCommandProcessor::ListFilesHandler)));

    AddCommand("next",
       new CommandEntry(
           "", "List the next page since the previous 'list' or 'next'.",
           NewPermanentCallback(
               this, &DriveCommandProcessor::NextFilesHandler)));

    AddCommand("revisions",
       new CommandEntry(
           "<fileid>", "List the revisions for the given fileid.",
           NewPermanentCallback(
               this, &DriveCommandProcessor::FileRevisionsHandler)));

    AddCommand("upload",
       new CommandEntry(
           "<path> [<mime-type>]",
           "Upload the path to your GDrive. If no mime-type is given then it"
           " is assumed to be text/plain",
           NewPermanentCallback(
               this, &DriveCommandProcessor::UploadFileHandler)));

    AddCommand("delete",
       new CommandEntry(
           "<fileid>",
           "Delete the given fileid",
           NewPermanentCallback(
               this, &DriveCommandProcessor::DeleteFileHandler)));

    AddCommand("trash",
       new CommandEntry(
           "<fileid>",
           "Permanently delete the given fileid",
           NewPermanentCallback(
               this, &DriveCommandProcessor::TrashFileHandler)));

    AddCommand("update",
       new CommandEntry(
           "<fileid> <path> [<mime-type>]",
           "Update the fileid with the contents of the given path",
           NewPermanentCallback(
               this, &DriveCommandProcessor::UpdateFileHandler)));

    AddCommand("download",
       new CommandEntry(
           "<fileid> <path|-> [<mime_type>] [<revisionid>]",
           "Download the specified fileid."
           " If a mime_type is provided, download that version."
           " If a revision is supplied then download that particular one."
           " Otherwise download whatever is on the GDrive.",
           NewPermanentCallback(
               this, &DriveCommandProcessor::DownloadRevisionHandler)));
  }

 private:
  void AboutHandler(const string&, const std::vector<string>&) {
    const DriveService::AboutResource& rsrc = app_->service()->get_about();
    std::unique_ptr<AboutResource_GetMethod> get(
       rsrc.NewGetMethod(app_->credential()));

    cout << "Finding out about you..." << endl;
    std::unique_ptr<google_drive_api::About> about(
        google_drive_api::About::New());
    get->ExecuteAndParseResponse(about.get()).IgnoreError();
    if (CheckAndLogResponse(get->http_response())) {
      cout << "  Name: " << about->get_name();
    }
  }

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

  void RevokeHandler(const string&, const std::vector<string>&) {
    app_->RevokeClient().IgnoreError();
  }

  void ShowFiles(const google_drive_api::FileList& list) {
    const JsonCppArray<google_drive_api::File>& items = list.get_items();
    if (items.size() == 0) {
      cout << "No files." << endl;
      return;
    }

    // We can use C++11 style iterators here, i.e.
    //   for (google_drive_api::File file : list.get_items())
    // But for the sake of broader compilers we'll be traditional.
    const char* sep = "";
    for (
        JsonCppArray<google_drive_api::File>::const_iterator it = items.begin();
        it != items.end();
        ++it) {
      const google_drive_api::File& file = *it;
      cout << sep;
      if (file.get_labels().get_trashed()) {
        cout << "*** TRASHED ***  ";  // continue ID on this line
      } else if (file.get_labels().get_hidden()) {
        cout << "*** HIDDEN ***  ";  // continue ID on this line
      }
      cout << "ID: " << file.get_id() << endl;
      cout << "  Size: " << file.get_file_size() << endl;
      cout << "  MimeType: " << file.get_mime_type() << endl;
      cout << "  Created: " << file.get_created_date().ToString() << endl;
      cout << "  Description: " << file.get_description() << endl;
      cout << "  Download Url: " << file.get_download_url() << endl;
      cout << "  Original Name: " << file.get_original_filename() << endl;
      cout << "  Modified By: " << file.get_last_modifying_user_name() << endl;
      sep = "\n";
    }
  }

  void ListFilesHandler(const string&, const std::vector<string>&) {
    const DriveService::FilesResource& rsrc = app_->service()->get_files();

    // We could use a FilesResource_ListMethod but we'll instead use
    // a pager so that we can play with it. Reset the old one (if any).
    // The 'next' command will advance the pager.
    list_pager_.reset(rsrc.NewListMethodPager(app_->credential()));
    list_pager_->request()->set_max_results(FLAGS_max_results);

    cout << "Getting (partial) file list..." << endl;
    bool ok = list_pager_->NextPage();
    CheckAndLogResponse(list_pager_->http_response());
    if (ok) {
      ShowFiles(*list_pager_->data());
    }

    if (list_pager_->is_done()) {
      cout << "There are no more results to page through." << endl;
    } else {
      cout << "\nEnter 'next' to see the next page of results." << endl;
    }
  }

  void NextFilesHandler(const string&, const std::vector<string>&) {
    if (!list_pager_.get()) {
      cout << "Cannot page through files util you 'list' them." << endl;
      return;
    }

    cout << "Getting next page of file list..." << endl;
    bool ok = list_pager_->NextPage();
    CheckAndLogResponse(list_pager_->http_response());
    if (ok) {
      ShowFiles(*list_pager_->data());
    }

    if (list_pager_->is_done()) {
      cout << "There are no more results to page through." << endl;
    } else {
      cout << "\nEnter 'next' to see the next page of results." << endl;
    }
  }

  void UploadFileHandler(const string& cmd, const std::vector<string>& args) {
    if (args.size() < 1 || args.size() > 2) {
      cout << "Usage: " << cmd << " <path> [<mime-type>]" << endl;
      return;
    }
    const string& path = args[0];
    const string& mime_type = args.size() > 1 ? args[1] : "text/plain";

    std::unique_ptr<google_drive_api::File> file(google_drive_api::File::New());
    file->set_title(StrCat("Uploaded from ", file::Basename(path)));
    file->set_editable(true);
    file->set_original_filename(file::Basename(path));

    DataReader* reader = NewUnmanagedFileDataReader(path);
    cout << "Uploading "<< reader->TotalLengthIfKnown()
         << " bytes from type=" << mime_type << " path=" << path << endl;

    const DriveService::FilesResource& rsrc = app_->service()->get_files();
    std::unique_ptr<FilesResource_InsertMethod> insert(
        rsrc.NewInsertMethod(
            app_->credential(), file.get(), mime_type, reader));
    insert->set_convert(false);

    insert->Execute().IgnoreError();
    CheckAndLogResponse(insert->http_response());
  }

  void UpdateFileHandler(const string& cmd, const std::vector<string>& args) {
    if (args.size() < 2 || args.size() > 3) {
      cout << "Usage: " << cmd << " <fileid> <path> [<mime-type>]" << endl;
      return;
    }
    const string& fileid = args[0];
    const string& path = args[1];
    const string& mime_type = args.size() > 2 ? args[2] : "text/plain";

    DataReader* reader = NewUnmanagedFileDataReader(path);
    cout << "Updating fileid=" << fileid
         << " with " << reader->TotalLengthIfKnown()
         << " bytes from type=" << mime_type << " path=" << path << endl;

    const DriveService::FilesResource& rsrc = app_->service()->get_files();

    std::unique_ptr<google_drive_api::File> file(google_drive_api::File::New());
    file->set_title(StrCat("Updated from ", file::Basename(path)));
    file->set_original_filename(file::Basename(path));
    std::unique_ptr<FilesResource_UpdateMethod> update(
        rsrc.NewUpdateMethod(
            app_->credential(), fileid, file.get(), mime_type, reader));

    update->Execute().IgnoreError();
    CheckAndLogResponse(update->http_response());
  }

  void DeleteFileHandler(const string& cmd, const std::vector<string>& args) {
    if (args.size() < 1) {
      cout << "Usage: " << cmd << " <fileid>" << endl;
      return;
    }
    const string& fileid = args[0];

    const DriveService::FilesResource& rsrc =
        app_->service()->get_files();
    std::unique_ptr<FilesResource_DeleteMethod> remove(
        rsrc.NewDeleteMethod(app_->credential(), fileid));

    cout << "Deleting fileid=" << fileid << "..." << endl;
    remove->Execute().IgnoreError();
    CheckAndLogResponse(remove->http_response());
  }

  void TrashFileHandler(const string& cmd, const std::vector<string>& args) {
    if (args.size() < 1) {
      cout << "Usage: " << cmd << " <fileid>" << endl;
      return;
    }
    const string& fileid = args[0];

    const DriveService::FilesResource& rsrc =
        app_->service()->get_files();
    std::unique_ptr<FilesResource_TrashMethod> trash(
        rsrc.NewTrashMethod(app_->credential(), fileid));

    cout << "Trashing fileid=" << fileid << "..." << endl;
    trash->Execute().IgnoreError();
    CheckAndLogResponse(trash->http_response());
  }

  void FileRevisionsHandler(const string& cmd,
                            const std::vector<string>& args) {
    if (args.size() < 1) {
      cout << "Usage: " << cmd << " <fileid>" << endl;
      return;
    }
    const string& fileid = args[0];

    const DriveService::RevisionsResource& rsrc =
        app_->service()->get_revisions();
    std::unique_ptr<RevisionsResource_ListMethod> list(
        rsrc.NewListMethod(app_->credential(), fileid));

    cout << "Getting evisions for " << fileid << "..." << endl;
    std::unique_ptr<google_drive_api::RevisionList> revision_list(
        google_drive_api::RevisionList::New());
    list->ExecuteAndParseResponse(revision_list.get()).IgnoreError();
    if (CheckAndLogResponse(list->http_response())) {
      const JsonCppArray<google_drive_api::Revision> all_items =
          revision_list->get_items();
      for (JsonCppArray<google_drive_api::Revision>::const_iterator it =
               all_items.begin();
           it != all_items.end();
           ++it) {
        const google_drive_api::Revision& revision = *it;
        cout << "ID: " << revision.get_id() << endl;
        cout << "  FileSize: " << revision.get_file_size() << endl;
        cout << "  Modified on " << revision.get_modified_date().ToString()
             << " by " << revision.get_last_modifying_user_name() << endl;
        if (revision.get_published()) {
          cout << "  Published URL: " << revision.get_published_link() << endl;
        }

        cout << "  Export Links:" << endl;
        const JsonCppAssociativeArray<string>& export_links =
            revision.get_export_links();
        for (JsonCppAssociativeArray<string>::const_iterator it =
                 export_links.begin();
             it != export_links.end();
             ++it) {
          cout << "    " << it.key() << ": " << it.value() << endl;
        }
      }
    }
  }

  void DownloadRevisionHandler(const string& cmd,
                               const std::vector<string>& args) {
    if ((args.size() < 2) || args.size() > 4) {
      cout << "Usage: " << cmd << " <fileid> <path|->"
           << "[<mime-type>] [<revisionid>]" << endl;
      return;
    }

    string url;
    const string kNone;
    const string& fileid = args[0];
    const string& path = args[1];
    const string& mime_type = args.size() > 2 ? args[2] : kNone;
    const string& revisionid = args.size() > 3 ? args[3] : kNone;
    std::unique_ptr<client::JsonCppData> client_response;
    if (revisionid.empty()) {
      const DriveService::FilesResource& rsrc = app_->service()->get_files();
      std::unique_ptr<FilesResource_GetMethod> get(
          rsrc.NewGetMethod(app_->credential(), fileid));
      std::unique_ptr<google_drive_api::File> file(
          google_drive_api::File::New());
      cout << "Downloading file_id=" << fileid << endl;
      get->ExecuteAndParseResponse(file.get()).IgnoreError();
      if (!CheckAndLogResponse(get->http_response())) {
        return;
      }
      client_response.reset(file.release());
    } else {
      const DriveService::RevisionsResource& rsrc =
        app_->service()->get_revisions();
      std::unique_ptr<RevisionsResource_GetMethod> get(
        rsrc.NewGetMethod(app_->credential(), fileid, revisionid));
      std::unique_ptr<google_drive_api::Revision>
          revision(google_drive_api::Revision::New());
      cout << "Downloading revision " <<  revisionid << " of file_id"
           << fileid << endl;
      get->ExecuteAndParseResponse(revision.get()).IgnoreError();
      if (!CheckAndLogResponse(get->http_response())) {
        return;
      }
      client_response.reset(revision.release());
    }


    if (!mime_type.empty()) {
      const Json::Value& storage = client_response->Storage("exportLinks");
      url = storage[mime_type.c_str()].asString();
      if (url.empty()) {
        cout << "*** No mime_type=" << mime_type << " available for download.";
        return;
      }
    } else {
      url = client_response->Storage("downloadUrl").asString();
      if (url.empty()) {
        cout << "Drive gives no downloadUrl so you must give a mime type.";
        return;
      }
    }

    std::unique_ptr<HttpRequest> request(
            app_->service()->transport()->NewHttpRequest(HttpRequest::GET));
    request->set_url(url);
    request->set_credential(app_->credential());

    bool to_file = path != "-";
    if (to_file) {
      client::DataWriter* writer =
          new ProgressMeterDataWriter(path);
      request->set_content_writer(writer);
    }
    request->Execute().IgnoreError();
    HttpResponse* download_response = request->response();
    if (download_response->ok()) {
      if (to_file) {
        cout << "*** Downloaded to: " << path << endl;
      } else {
        string body;
        googleapis::util::Status status = download_response->GetBodyString(&body);
        cout << "*** Here's what I downloaded:" << endl;
        cout << body << endl;
      }
    } else {
      cout << download_response->status().error_message() << endl;
    }
  }

  std::unique_ptr<FilesResource_ListMethodPager> list_pager_;
  DriveUtilApplication* app_;
  DISALLOW_COPY_AND_ASSIGN(DriveCommandProcessor);
};


}  // namespace googleapis

using namespace googleapis;
int main(int argc, char* argv[]) {
google::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_client_secrets_path.empty()) {
    LOG(ERROR) << "--client_secrets_path not set";
    return -1;
  }

  DriveUtilApplication app;
  googleapis::util::Status status = app.Init(FLAGS_client_secrets_path);
  if (!status.ok()) {
    LOG(ERROR) << "Could not initialize application: "
               << status.error_message() << endl;;
    return -1;
  }

  DriveCommandProcessor processor(&app);
  processor.Init();
  processor.set_log_success_bodies(true);
  processor.RunShell();

  return 0;
}

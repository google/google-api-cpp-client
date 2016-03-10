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


#include <algorithm>
#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include <string>
using std::string;
#include <map>
#include <vector>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_response.h"
#include "samples/command_processor.h"
#include <glog/logging.h>
#include "googleapis/strings/ascii_ctype.h"

namespace googleapis {

using std::cin;
using std::cerr;
using std::cout;
using client::HttpResponse;

namespace sample {

CommandProcessor::CommandProcessor()
    : prompt_("> "), log_success_bodies_(false) {
}

CommandProcessor::~CommandProcessor() {
  for (CommandEntryMap::const_iterator it = commands_.begin();
       it != commands_.end();
       ++it) {
    delete it->second;
  }
}

void CommandProcessor::InitCommands() {
  AddBuiltinCommands();
}

void CommandProcessor::AddBuiltinCommands() {
  AddCommand("quit",
     new CommandEntry(
         "", "Quit the program.",
         NewPermanentCallback(this, &CommandProcessor::QuitHandler)));
  AddCommand("help",
     new CommandEntry(
         "", "Show help.",
         NewPermanentCallback(this, &CommandProcessor::HelpHandler)));
  AddCommand("quiet",
     new CommandEntry(
         "", "Dont show successful response bodies.",
         NewPermanentCallback(this, &CommandProcessor::VerboseHandler, 0)));
  AddCommand("verbose",
     new CommandEntry(
         "", "Show successful response bodies.",
         NewPermanentCallback(this, &CommandProcessor::VerboseHandler, 1)));
}

void CommandProcessor::AddCommand(string name, CommandEntry* details) {
  commands_.insert(std::make_pair(name, details));
}

bool CommandProcessor::CheckAndLogResponse(HttpResponse* response) {
  googleapis::util::Status transport_status = response->transport_status();

  // Rewind the stream before we dump it since this could get called after
  // ExecuteAndParseResponse which will have read the result.
  response->body_reader()->SetOffset(0);
  bool response_was_ok;
  if (!transport_status.ok()) {
    cerr << "ERROR: " << transport_status.error_message() << std::endl;
    return false;
  } else if (!response->ok()) {
    string body;
    googleapis::util::Status status = response->GetBodyString(&body);
    if (!status.ok()) {
      body.append("ERROR reading HTTP response body: ");
      body.append(status.error_message());
    }
    cerr << "ERROR(" << response->http_code() << "): " << body << std::endl;
    response_was_ok = false;
  } else {
    cout << "OK(" << response->http_code() << ")" << std::endl;
    if (log_success_bodies_) {
      string body;
      googleapis::util::Status status = response->GetBodyString(&body);
      if (!status.ok()) {
        body.append("ERROR reading HTTP response body: ");
        body.append(status.error_message());
      }
      cout << "----------  [begin response body]  ----------" << std::endl;
      cout << body << std::endl;
      cout << "-----------  [end response body]  -----------" << std::endl;
    }
    response_was_ok = true;
  }

  // Restore offset in case someone downstream wants to read the body again.
  if (response->body_reader()->SetOffset(0) != 0) {
    LOG(WARNING)
        << "Could not reset body offset so future reads (if any) will fail.";
  }
  return response_was_ok;
}

void CommandProcessor::VerboseHandler(
    int level, const string&, const std::vector<string>&) {
  if (level == 0) {
    cout << "Being quiet." << std::endl;
    log_success_bodies_ = false;
  } else {
    cout << "Being verbose." << std::endl;
    log_success_bodies_ = true;
  }
}

void CommandProcessor::QuitHandler(const string&, const std::vector<string>&) {
  VLOG(1) << "Got QUIT";
  done_ = true;
}

void CommandProcessor::HelpHandler(const string&, const std::vector<string>&) {
  std::vector<std::pair<string, const CommandEntry*> > all;
  for (CommandEntryMap::const_iterator it = commands_.begin();
       it != commands_.end();
       ++it) {
    all.push_back(*it);
  }

  std::sort(all.begin(), all.end(), &CommandEntry::CompareEntry);

  string help = "Commands:\n";
  for (auto it = all.begin(); it != all.end(); ++it) {
    help.append(it->first);
    if (!it->second->args.empty()) {
      help.append(" ");
      help.append(it->second->args);
    }
    help.append("\n   ");
    help.append(it->second->help);
    help.append("\n   ");
  }
  cout << help << std::endl;
}

void CommandProcessor::RunShell() {
  if (!commands_.size()) {
    // Not called already, such as in a previous invocation to RunShell.
    InitCommands();
  }
  done_ = false;
  while (!done_) {
    cout << std::endl
         << prompt_;
    string input;
    std::getline(cin, input);

    std::vector<string> args;
    SplitArgs(input, &args);
    if (args.size() == 0) continue;

    string cmd = args[0];
    if (commands_.find(cmd) != commands_.end()) {
      args.erase(args.begin());  // remove command
      commands_[cmd]->runner->Run(cmd, args);
    } else {
      cerr << "Error: Unknown command '" << cmd << "'  (try 'help')\n";
    }
  }
}

// static
bool CommandProcessor::SplitArgs(const string& args,
                                 std::vector<string>* list) {
  bool ok = true;
  string current;
  const char* end = args.data() + args.size();

  for (const char* pc = args.data(); pc < end; ++pc) {
    if (ascii_isspace(*pc)) {
      if (current.empty()) continue;  // skip spaces between tokens
      list->push_back(current);
      current.clear();
      continue;
    } else if (*pc == '"') {
      // end current word and start a new one.
      if (!current.empty()) {
        list->push_back(current);
        current.clear();
      }
      // keep the contents inside double quotes but respect escapes.
      // If there was no close quote, treat it a bad parse.
      for (++pc; pc < end && *pc != '"'; ++pc) {
        if (*pc == '\\') {
          ++pc;
          if (pc == end) {
            ok = false;
            continue;
          }
        }
        current.push_back(*pc);
      }
      list->push_back(current);
      current.clear();
      ok = pc < end;
      continue;
    }
    if (*pc == '\\') {
      ++pc;
      if (pc == end) {
        ok = false;
        continue;
      }
    }
    current.push_back(*pc);
  }
  if (!current.empty()) {
    list->push_back(current);
  }
  return ok;
}

}  // namespace sample

}  // namespace googleapis

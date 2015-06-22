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
// Command Processor class provides a basic shell interpreter for writing apps.
// It might be useful for writing experimental code that makes API calls.
// I use it in some samples.
#ifndef GOOGLEAPIS_SAMPLES_COMMAND_PROCESSOR_H_
#define GOOGLEAPIS_SAMPLES_COMMAND_PROCESSOR_H_

#include <map>
using std::map;
#include <memory>
#include <utility>
using std::make_pair;
using std::pair;
#include <vector>
using std::vector;
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/strings/stringpiece.h"
namespace googleapis {

namespace sample {
using client::HttpResponse;

class CommandProcessor {
 public:
  CommandProcessor();
  virtual ~CommandProcessor();

  // Controller for the application takes input commands, runs them, and
  // prints the output/errors to the console.
  virtual void RunShell();

  void set_prompt(const string& prompt) { prompt_ = prompt; }
  const string& prompt() const { return prompt_; }

  // If true then CheckAndLogResponse will also log the http_response.body()
  // for messages that are http_response.ok()
  void set_log_success_bodies(bool on) { log_success_bodies_ = on; }
  bool log_success_bodies() const { return log_success_bodies_; }

  // Split args into list separating by spaces unless they are escaped
  // or within quotes.
  // Returns true if args were ok. False if they ended prematurely.
  // Even if false is returned, the list will contain the best interpretation
  // of the args.
  static bool SplitArgs(const StringPiece& phrase, vector<string>* list);

 protected:
  typedef Callback2<const string&, const vector<string>&> CommandRunner;
  struct CommandEntry {
    CommandEntry(
        const StringPiece& usage_args,
        const StringPiece& description,
        CommandRunner* callback)
        : args(usage_args), help(description), runner(callback) {
      callback->CheckIsRepeatable();
    }

    static bool CompareEntry(
        const pair<const StringPiece, const CommandEntry*>& a,
        const pair<const StringPiece, const CommandEntry*>& b) {
      return a.first < b.first;
    }

    const StringPiece args;
    const StringPiece help;
    std::unique_ptr<CommandRunner> runner;

   private:
    DISALLOW_COPY_AND_ASSIGN(CommandEntry);
  };

  // Sets up command processor with available commands.
  // Derived classes should override this and call AddCommand with their
  // various commands.
  //
  // The base method adds the builtin commands (calls AddBulitinCommands)
  virtual void InitCommands();

  void AddCommand(StringPiece name, CommandEntry* details);

  // For displaying errors, etc.
  bool CheckAndLogResponse(HttpResponse* response);

  // Handler for "quit" command.
  virtual void QuitHandler(const string&, const vector<string>&);

  // Handler for "help" command.
  virtual void HelpHandler(const string&, const vector<string>&);

  // Handler for "quiet" and "verbose" commands.
  virtual void VerboseHandler(int level, const string&, const vector<string>&);

  // Adds builtin 'help' and 'quit' commands.
  // Called by InitCommands but offered separately so you dont need to
  // propagate InitCommands when overriding it.
  void AddBuiltinCommands();

 private:
  // maps command name to entry map for exeuting it.
  typedef map<StringPiece, CommandEntry*> CommandEntryMap;

  CommandEntryMap commands_;
  string prompt_;
  bool log_success_bodies_;
  bool done_;
  DISALLOW_COPY_AND_ASSIGN(CommandProcessor);
};

}  // namespace sample

}  // namespace googleapis
#endif  // GOOGLEAPIS_SAMPLES_COMMAND_PROCESSOR_H_

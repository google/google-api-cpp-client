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


#include <stdio.h>
#include <string.h>

#include <iostream>
using std::cout;
using std::endl;
using std::ostream;
#include <memory>
#include <string>
using std::string;

#ifdef _MSC_VER
#include <tchar.h>
#endif

#include "googleapis/client/util/test/googleapis_gtest.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"

namespace googleapis {

using std::string;

namespace {

#ifdef _MSC_VER
// Windows gives us a file name by creating a file.
// we want a directory so this is no good.
// Fow now we'll just delete the temporary file and use its name.
static string tempnam(const char* ignore_dir, const char* prefix) {
  TCHAR dir[MAX_PATH + 1];
  int len = GetTempPath(ARRAYSIZE(dir), dir);

  string windows_prefix;
  const TCHAR* name_prefix = ToWindowsString(prefix, &windows_prefix);
  TCHAR path[MAX_PATH + 1];
  UINT result = GetTempFileName(dir, name_prefix, 0, path);
  DeleteFile(path);   // Was created a side effect.

  CHECK_NE(0, result);
  string windows_path = googleapis::FromWindowsStr(path);
  return googleapis::FromWindowsPath(windows_path);
}
#endif

std::unique_ptr<string> default_tempdir_;

void CreateTestingTempDir() {
#if defined(__linux__)
  default_tempdir_.reset(new string(mkdtemp("/tmp/gapi.XXXXXX")));
#else
  default_tempdir_.reset(new string(tempnam(NULL, "gapi")));
#endif

  googleapis::util::Status status =
        File::RecursivelyCreateDirWithPermissions(*default_tempdir_, S_IRWXU);

  // If this fails, maybe there was a race condition.
  // But more likely we have a permissions problem.
  ASSERT_TRUE(status.ok()) << "Could not create " << *default_tempdir_;
  LOG(INFO) << "Using test_tmpdir=" << *default_tempdir_;
}

void DeleteTestingTempDir() {
  LOG(INFO) << "Deleting test_tmpdir=" << *default_tempdir_;
  File::RecursivelyDeleteDir(*default_tempdir_);
}

}  // annoymous namespace

namespace client {

string GetTestingTempDir() {
  return *default_tempdir_;
}

}  // namespace client


}  // namespace googleapis

using namespace googleapis;
int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

#ifdef _MSC_VER
  // Change glog fatal failure function to abort so DEATH tests will see
  // a failure. Otherwise the default handler used by glog does not result
  // in a failure exit in _DEBUG.
  google::InstallFailureFunction(abort);
#endif

  CreateTestingTempDir();
  int result = RUN_ALL_TESTS();
  DeleteTestingTempDir();

  return result;
}

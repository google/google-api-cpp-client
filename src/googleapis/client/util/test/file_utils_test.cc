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


#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
using std::string;

#include "googleapis/client/util/file_utils.h"
#include "googleapis/client/util/test/googleapis_gtest.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/util/file.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/util/canonical_errors.h"
#include "googleapis/util/status_test_util.h"

namespace googleapis {

using client::GetTestingTempDir;
using client::SensitiveFileUtils;

#if HAVE_UGO_PERMSISSONS
static const mode_t kAllPermissionsMask = (S_IRWXU | S_IRWXG | S_IRWXO));
#else
static const mode_t kAllPermissionsMask = S_IRWXU;
#endif

class FileUtilsTestFixture : public testing::Test {
 protected:
  void CheckPermissions(const string& path, mode_t permissions)  {
    struct stat info;
    if (permissions == 0) {
      EXPECT_EQ(-1, stat(path.c_str(), &info));
    } else {
      EXPECT_EQ(0, stat(path.c_str(), &info));
    }
    if (permissions) {
      EXPECT_EQ(permissions, info.st_mode & kAllPermissionsMask);
    }
  }
};

TEST_F(FileUtilsTestFixture, TestCreateDir) {
  const string kRoot = StrCat(GetTestingTempDir(), "/test_create_dir");
  File::Delete(kRoot);
  ASSERT_TRUE(util::IsNotFound(file::Exists(kRoot, file::Defaults())));

  googleapis::util::Status status =
        SensitiveFileUtils::CreateSecureDirectoryRecursively(kRoot);
  EXPECT_TRUE(status.ok()) << status.ToString();
  CheckPermissions(kRoot, S_IRUSR | S_IWUSR | S_IXUSR);

  status = SensitiveFileUtils::CreateSecureDirectoryRecursively(kRoot);
  CheckPermissions(kRoot, S_IRUSR | S_IWUSR | S_IXUSR);
}

TEST_F(FileUtilsTestFixture, TestInvalidDir) {
#if HAVE_UGO_PERMISSIONS
  const string kRoot = StrCat(GetTestingTempDir(), "/test_invalid_dir");
  const mode_t bad_permissions = S_IRUSR | S_IWUSR | S_IXGRP;
EXPECT_TRUE(
       File::RecursivelyCreateDirWithPermissions(kRoot, bad_permissions).ok());
  CheckPermissions(kRoot, bad_permissions);

  googleapis::util::Status status = SensitiveFileUtils::VerifyIsSecureDirectory(kRoot);
  EXPECT_FALSE(status.ok());
#else
  LOG(WARNING) << "Cannot test for invalid dir";
#endif
}

TEST_F(FileUtilsTestFixture, TestStoreFile) {
  const string kPath = StrCat(GetTestingTempDir(), "/test_store");
  File::Delete(kPath);
  ASSERT_TRUE(util::IsNotFound(file::Exists(kPath, file::Defaults())));

  const string kOriginalContent = "Sample test data";
  googleapis::util::Status status = SensitiveFileUtils::WriteSensitiveStringToFile(
       kOriginalContent, kPath, false);
  EXPECT_TRUE(status.ok()) << status.ToString();
  CheckPermissions(kPath, S_IRUSR | S_IWUSR);

  const string kFailedContent = "Failed test data";
  // Cant write file that already exists (overwrite is false)
  status = SensitiveFileUtils::WriteSensitiveStringToFile(
      kFailedContent, kPath, false);
  EXPECT_FALSE(status.ok());
  CheckPermissions(kPath, S_IRUSR | S_IWUSR);

  // Overwrite existing file.
  const string kUpdatedContent = "Updated test data";
  status = SensitiveFileUtils::WriteSensitiveStringToFile(
      kUpdatedContent, kPath, true);
  EXPECT_TRUE(status.ok()) << status.ToString();
  CheckPermissions(kPath, S_IRUSR | S_IWUSR);
}

TEST_F(FileUtilsTestFixture, TestSecureDelete) {
  const string kPath = StrCat(GetTestingTempDir(), "/test_delete");
  File::Delete(kPath);

  googleapis::util::Status status =
        SensitiveFileUtils::WriteSensitiveStringToFile("X", kPath, true);
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_OK(file::Exists(kPath, file::Defaults()));

  // Nonexistant file ok.
  EXPECT_TRUE(SensitiveFileUtils::DeleteSensitiveFile(kPath).ok());
  EXPECT_TRUE(util::IsNotFound(file::Exists(kPath, file::Defaults())));

  // Nonexistant file ok.
  EXPECT_TRUE(SensitiveFileUtils::DeleteSensitiveFile(kPath).ok());
}

TEST_F(FileUtilsTestFixture, TestValidatePermissions) {
  const string kPath = StrCat(GetTestingTempDir(), "/test_validate");
  File::Delete(kPath);
  ASSERT_TRUE(util::IsNotFound(file::Exists(kPath, file::Defaults())));
  googleapis::util::Status status =
        SensitiveFileUtils::WriteSensitiveStringToFile("X", kPath, false);
  EXPECT_TRUE(status.ok()) << status.ToString();

  status = SensitiveFileUtils::VerifyIsSecureFile(kPath, true);
  EXPECT_TRUE(status.ok()) << status.ToString();

  const mode_t kGoodMode = S_IRUSR | S_IWUSR;

#ifdef HAVE_UGO_PERMISSIONS
  EXPECT_EQ(0, chmod(kPath.c_str(), kGoodMode | S_IRGRP));
  EXPECT_FALSE(SensitiveFileUtils::VerifyIsSecureFile(kPath, true).ok());
  EXPECT_EQ(0, chmod(kPath.c_str(), kGoodMode | S_IWGRP));
  EXPECT_FALSE(SensitiveFileUtils::VerifyIsSecureFile(kPath, true).ok());
  EXPECT_EQ(0, chmod(kPath.c_str(), kGoodMode | S_IXGRP));
  EXPECT_FALSE(SensitiveFileUtils::VerifyIsSecureFile(kPath, true).ok());

  EXPECT_EQ(0, chmod(kPath.c_str(), kGoodMode | S_IROTH));
  EXPECT_FALSE(SensitiveFileUtils::VerifyIsSecureFile(kPath, true).ok());
  EXPECT_EQ(0, chmod(kPath.c_str(), kGoodMode | S_IWOTH));
  EXPECT_FALSE(SensitiveFileUtils::VerifyIsSecureFile(kPath, true).ok());
  EXPECT_EQ(0, chmod(kPath.c_str(), kGoodMode | S_IXOTH));
  EXPECT_FALSE(SensitiveFileUtils::VerifyIsSecureFile(kPath, true).ok());
#endif  // have_UGO_PERMISSIONS

  EXPECT_EQ(0, chmod(kPath.c_str(), kGoodMode));
  status = SensitiveFileUtils::VerifyIsSecureFile(kPath, true);
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_FALSE(SensitiveFileUtils::VerifyIsSecureDirectory(kPath).ok());

  const string kLink = StrCat(GetTestingTempDir(), "/link");
  File::Delete(kLink);
  ASSERT_TRUE(util::IsNotFound(file::Exists(kLink, file::Defaults())));
#ifndef _MSC_VER
  EXPECT_EQ(0, symlink(kPath.c_str(), kLink.c_str()));
#else
  string windows_link_string;
  string windows_path_string;
  bool created_link = CreateSymbolicLink(
      googleapis::ToWindowsString(
          googleapis::ToWindowsPath(kLink), &windows_link_string),
      googleapis::ToWindowsString(
          googleapis::ToWindowsPath(kPath), &windows_path_string),
      0x0) != 0;
  if (!created_link) {
    if (GetLastError() == 1314) {
      LOG(WARNING) << "You do not have permissions to create symbolic links."
                   << "\nAborting this test";
      return;
    } else {
      EXPECT_TRUE(created_link) << "Failed with error=" << GetLastError();
    }
  }
#endif

  ASSERT_OK(file::Exists(kLink, file::Defaults()));
  EXPECT_EQ(0, chmod(kLink.c_str(), kGoodMode));
  EXPECT_TRUE(SensitiveFileUtils::VerifyIsSecureFile(kLink, true).ok());
}

}  // namespace googleapis

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
 * @defgroup PlatformLayerFiles Platform Layer - File Support
 *
 * This module within the Platform Layer contains classes and free functions
 * that operate on files. They are specific to the needs of the
 * Google Client for C++ library so defined here rather in base/file for
 * internal management reasons.
 */
#ifndef GOOGLEAPIS_UTIL_FILE_UTILS_H_
#define GOOGLEAPIS_UTIL_FILE_UTILS_H_

#include <sys/stat.h>
#include <string>
using std::string;
#include "googleapis/client/util/status.h"
namespace googleapis {

class StringPiece;

namespace client {

/*
 * Helper functions for managing sensitive files.
 * @ingroup PlatformLayerFiles
 *
 * WARNING(ewiseblatt): 20130304
 * These files are not reliably secure. We are managing OS level
 * permissions and relying on the OS to protect the contents. We
 * make some attempt to securely delete the contents of files but
 * not necessarily robust. For truely sensitive data, consider
 * encrypting the files instead.
 */
class SensitiveFileUtils {
 public:
  /*
   * Checks that the provided path is secure file.
   *
   * Secure paths can only be user read-writable and not a symbolic link.
   *
   * @param[in] path The path to check should be an existing file.
   * @param[in] writable_allowed true if path may be writable.
   *            false if it must be read-only.
   * @return ok status if it is secure,
   *         otherwise an error explaining the concern.
   */
  static googleapis::util::Status
  VerifyIsSecureFile(const string& path, bool writable_allowed);

  /*
   * Checks that the provided path is a secure directory.
   *
   * @param[in] path The path to check should be an existing directoy.
   * @return ok status if it is secure,
   *         otherwise an error explaining the concern.
   */
  static googleapis::util::Status VerifyIsSecureDirectory(const string& path);

  /*
   * Creates a secure directory at the specified path if it does not
   * already exist.
   *
   * Any directories that are created will be created wth secure permissions
   * (user rwx only).
   *
   * @param[in] path The desired directory path.
   * @return ok if the path exists as a secure directory when done.
   *         Otherwise an error indicating why it could not be created.
   */
  static googleapis::util::Status
  CreateSecureDirectoryRecursively(const string& path);

  /*
   * Writes the given data to a secure file at the specified path.
   *
   * @param[in] data The data to write is considered a binary string so
   *                 will not be implicitly null terminated.
   * @param[in] path The path to write to.
   * @param[in] overwrite If true then overwrite any existing file at the
   *            path. Otherwise fail if a file already exists.
   * @return ok or reason for failure to write the file.
   */
  static googleapis::util::Status
  WriteSensitiveStringToFile(
      const StringPiece& data, const string& path, bool overwrite);

  /*
   * Deletes the file, but does not prevent the data from being unrecoverable.
   *
   * This function will make some attempts to prevent the data from being
   * reovered, it is still not secure. There are many ways in which the OS
   * itself may have leaked some data on disk.
   *
   * @param[in] path The to the file to delete
   * @return ok if the file could be deleted,
   *         otherwise an error explaining the failure.
   */
  static googleapis::util::Status DeleteSensitiveFile(const string& path);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_FILE_UTILS_H_

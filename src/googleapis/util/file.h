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
// Basic file IO support.

#ifndef GOOGLEAPIS_FILE_H_
#define GOOGLEAPIS_FILE_H_

#include <sys/types.h>
#include <sys/stat.h>

#if defined(_MSC_VER)
# include <io.h>
# define S_IRUSR mode_t(_S_IREAD)
# define S_IWUSR mode_t(_S_IWRITE)
# define S_IXUSR mode_t(_S_IEXEC)
# define S_IRWXU mode_t(_S_IREAD | _S_IWRITE | _S_IEXEC)
# define S_IRWXG mode_t(0)
# define S_IRWXO mode_t(0)
# define S_ISDIR(mode) ((mode & _S_IFDIR) != 0)
# define S_ISREG(mode) ((mode & _S_IFREG) != 0)
# define S_ISLNK(mode) 0

# define O_NONBLOCK 0  /* ignore */
# define O_SYNC 0      /* ignore */
# define F_OK 0
# define access _access
# define unlink _unlink
# define rmdir _rmdir
# define chmod _chmod
# define open _open
# define close _close
# define read _read
# define write _write
# define lseek _lseek
# define fchmod(fd, mode)  (/* TODO(ewiseblatt) implement this */ 0)

#endif

#include <string>
using std::string;
#include "googleapis/strings/stringpiece.h"
#include "googleapis/util/status.h"
namespace googleapis {

class FileOpenOptions {
 public:
  FileOpenOptions() : permissions_(S_IWUSR | S_IRUSR) {}
  ~FileOpenOptions() {}
  void CopyFrom(const FileOpenOptions& from) { *this = from; }
  void set_permissions(mode_t bits) { permissions_ = bits; }
  mode_t permissions() const { return permissions_; }

 private:
  mode_t permissions_;
};

class File {
 public:
  static StringPiece Basename(const StringPiece& path);
  static StringPiece StripBasename(const StringPiece& path);
  static StringPiece Dirname(const StringPiece& path);
  static bool Exists(const string& path);
  static bool Delete(const string& path);
  static bool DeleteDir(const string& path);
  static bool RecursivelyDeleteDir(const string& path);

  static util::Status RecursivelyCreateDirWithPermissions(
      const string& path, mode_t permissions);
  static util::Status ReadPath(const string& path, string* s);
  static util::Status WritePath(const string& path, const StringPiece& s);

  // Returns the path to the current running program.
  static string GetCurrentProgramFilenamePath();

  // DEPRECATED
  static bool ReadFileToString(const string& path, string* s) {
    return ReadPath(path, s).ok();
  }

  // Desroy the file using Close()
  static File* OpenWithOptions(
     const string& path, const char* mode, const FileOpenOptions& options);
  // Destroys the File instance as a side-effect.
  static File* Open(const string& path, const char* mode) {
    return OpenWithOptions(path, mode, FileOpenOptions());
  }
  bool Close();


  util::Status Flush();
  util::Status Write(const char* buffer, int64 length) {
    return WriteString(StringPiece(buffer, length));
  }
  util::Status WriteString(const StringPiece& bytes);
  util::Status ReadToString(string* output);
  util::Status Read(char* buffer, int64 length, int64* got);
  bool Seek(int64 position);
  int64 Tell();
  int64 Size();

 private:
  explicit File(int fd);
  virtual ~File();

  int fd_;
};

} // namespace googleapis
#endif  // GOOGLEAPIS_FILE_H_

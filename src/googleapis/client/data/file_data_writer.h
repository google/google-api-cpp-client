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
#ifndef GOOGLEAPIS_DATA_FILE_DATA_WRITER_H_
#define GOOGLEAPIS_DATA_FILE_DATA_WRITER_H_

//

#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/util/status.h"
#include "googleapis/util/file.h"
namespace googleapis {

class FileOpenOptions;

namespace client {

class FileDataWriter : public DataWriter {
 public:
  FileDataWriter(const string& path, const FileOpenOptions& options);
  ~FileDataWriter() override;

  googleapis::util::Status DoClear() override;

  googleapis::util::Status DoBegin() override;

  googleapis::util::Status DoEnd() override;

  googleapis::util::Status DoWrite(int64 bytes, const char* buffer) override;

  DataReader* DoNewDataReader(Closure* deleter) override;

 private:
  string path_;
  FileOpenOptions options_;
  File* file_;
};

/*
 * Creates a data writer that rewrites the file at the given path.
 * @ingroup DataLayerRaw
 *
 * @param[in] path The caller will own the file at the given path.
 */
DataWriter* NewFileDataWriter(const string& path);

/*
 * Creates a data writer that rewrites the file at the given path with control
 * over how the file is created.
 * @ingroup DataLayerRaw
 *
 * @param[in] path The caller will own the file at the given path.
 * @param[in] options The options can be used to override the permissions
 *            to given the created file.
 */
DataWriter* NewFileDataWriter(const string& path,
                              const FileOpenOptions& options);

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_FILE_DATA_WRITER_H_

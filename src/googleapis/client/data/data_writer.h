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


#ifndef GOOGLEAPIS_DATA_DATA_WRITER_H_
#define GOOGLEAPIS_DATA_DATA_WRITER_H_

#include <string>
using std::string;
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
namespace googleapis {

class FileOpenOptions;
class StringPiece;

namespace client {
class DataReader;

/*
 * Interface for writing into synchronous binary data stream.
 * @ingroup DataLayerRaw
 *
 * The DataWriter is the base class for writing non-trivial data using
 * a streaming-like interface. Writers can act as reader factories if you
 * need to read-back what had been written from within the same process.
 * For example streaming HTTP responses into files then reading them back
 * to handle them.
 *
 * @see NewStringDataWriter
 * @see NewFileDataWriter
 */
// TODO(user): 20130418
// Consider adding << operator
class DataWriter {
 public:
  /*
   * Standard destructor.
   */
  virtual ~DataWriter();

  /*
   * Returns the number of bytes written into the stream.
   */
  int64 size() const { return size_; }

  /*
   * Determine if we've encountered a hard error or not.
   *
   * @return true if no hard error has been set, false if an error.
   * @see error
   */
  bool ok() const                      { return status_.ok(); }

  /*
   * Returns details for the error on the stream, if any.
   *
   * @return a successful status if the stream is ok, otherwise the
   * error encounteredd.
   */
  const googleapis::util::Status& status() const { return status_; }

  /*
   * Clears any prior data writen into the stream so that it is empty.
   *
   * TODO(user): 20130306
   * I'm not sure what clear means yet.
   * Should it delete files or any other side effects?
   * This is needed to reset a response, such as when retrying.
   */
  void Clear();

  /*
   * Notifies the writer that it is startimg to write a stream.
   *
   * @see DoBegin
   */
  void Begin();

  /*
   * Synchronously write a fixed number of bytes into the stream.
   *
   * If Begin had not been called already then this will call Begin first.
   *
   * @param[in] size The number of bytes to write.
   * @param[in] data The data to write.
   *
   * @see DoWrite
   */
  googleapis::util::Status Write(int64 size, const char* data);

  /*
   * Synchronously write a fixed number of bytes into the stream.
   *
   * @param[in] data The string piece encapsulates the size and data to write.
   *
   * @see DoWrite
   */
  googleapis::util::Status Write(const StringPiece& data);

  /*
   * Synchronously stream a reader's content into a writer.
   *
   * @param[in] reader The reader to read from.
   * @param[in] max_bytes Limits the number of bytes to write.
   *                      If max_bytes < 0 then write all remaining bytes.
   * @return Ok or reason for failure.
   */
  googleapis::util::Status Write(DataReader* reader, int64 max_bytes = -1);

  /*
   * Notifies the writer that it has finished writing a stream.
   *
   * @see DoEnd
   */
  void End();

  /*
   * Returns an unmanaged data reader that will read the content written to
   * this writer's byte stream.
   *
   * Depending on the writer, this may only be valid over the lifetime of
   * the writer. If you arent sure, use instead:
   * <pre>
   *    writer->NewManagedDataReader(DeletePointerClosure(writer))
   * </pre>
   * That effectively transfers ownership of the writer to the reader
   * so can only be requested once.
   */
  DataReader* NewUnmanagedDataReader() {
    return NewManagedDataReader(NULL);
  }

  /*
   * Returns a managed data reader that will read this content.
   */
  DataReader* NewManagedDataReader(Closure* deleter);

 protected:
  /*
   * Standard constructor.
   */
  DataWriter();

  /*
   * Sets the status as a means to pass error details back to the caller.
   *
   * Setting an error implies setting done as well. However clearing an
   * error by setting an ok status will not clear done. To clear the done
   * state you must explicitly call set_done.
   *
   * @param[in] status An error implies a hard failure and will mark the ader
   *            done.
   */
  void set_status(util::Status status)  { status_ = status; }

  /*
   * Hook for specialized writers to respond to Begin.
   *
   * The base class just returns ok.
   */
  virtual googleapis::util::Status DoBegin();

  /*
   * Hook for specialized writes to respond to End.
   *
   * The base class just returns ok.
   */
  virtual googleapis::util::Status DoEnd();

  /*
   * Hook for specialized writers to clear the byte stream.
   *
   * The base class just returns ok.
   */
  virtual googleapis::util::Status DoClear();

  /*
   * Hook for the specialied writers to write into their byte stream.
   *
   * @param[in] bytes The number of bytes in data to write.
   * @param[in] data The data to write into the byte stream.
   *
   * @return success if all the bytes could be written, or an error.
   */
  virtual googleapis::util::Status DoWrite(int64 bytes, const char* data) = 0;

  /*
   * Factory method to create new reader specialized for the writer's
   * byte stream implementation.
   *
   * @param[in] closure If non-NULL then create a managed reader with the
   *            closure.
   */
  virtual DataReader* DoNewDataReader(Closure* closure) = 0;

 private:
  int64 size_;
  bool began_;
  googleapis::util::Status status_;

  DISALLOW_COPY_AND_ASSIGN(DataWriter);
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
DataWriter* NewFileDataWriter(
    const string& path, const FileOpenOptions& options);

/*
 * Creates a data writer that rewrites the given string.
 * @ingroup DataLayerRaw
 *
 * @param[in] s A pointer to a string owned by the caller. This string will
 *              remain valid after the writer is destroyed.
 *              Use <code>s->data()</code> rather
 *              than <code>s->c_str()</code> if you need to access the raw
 *              bytes in the string since the writer may be given binary data.
 */
DataWriter* NewStringDataWriter(string* s);

/*
 * Creates an in-memory data writer that encapsulates the memory it uses.
 * @ingroup DataLayerRaw
 */
DataWriter* NewStringDataWriter();

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_DATA_WRITER_H_

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
 * @defgroup DataLayerRaw Data Layer - Raw Data Management
 *
 * The raw data management module is responsible for access to and manipulation
 * of raw data. It provides abstractions and mechanisms for supplying data for
 * messaging payloads, and for getting the data out of those payloads. As a
 * rule of thumb it provides data support without being tied to the HTTP
 * Transport Layer or involving any inter-process messaging. The concepts it
 * defines are used throughout the Google APIs Client Libraries for C++ to
 * help facilitate passing data through the various components and subsystems.
 */
#ifndef GOOGLEAPIS_DATA_DATA_READER_H_
#define GOOGLEAPIS_DATA_DATA_READER_H_

#include <istream>  // NOLINT
#include <string>
using std::string;
#include <vector>

#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/base/macros.h"
namespace googleapis {

class StringPiece;

namespace client {

/*
 * Interface for reading from an asynchronous binary data stream.
 * @ingroup DataLayerRaw
 *
 * The DataReader is the base class for reading non-trivial data using a
 * streaming-like interface. It is simpler and potentially more efficient
 * than using standard C++ streams.
 *
 * The interface primarily consists of 2 methods:
 * <ul>
 *   <li> Read data from the reader in whole or part.
 *   <li> Set the offset into the byte sequence for the next read.
 * </ul>
 * The done() method indicates there is no more data to be read -- until
 * setting the offset back toward (or at) the beginning for the next read.
 * The error() method indicates premature ending. Errors are
 * considered hard-errors. Setting an error implies it is done. error() can
 * be set even if some bytes are read. Soft errors just return fewer bytes
 * than requested, including 0.
 *
 * Readers can get their data from arbitrary sources, and dont necessarily
 * buffer everything in memory. Therefore you cannot count on being able
 * to read their data over and over again multiple times. Most readers will
 * allow you to but if you have a generic reader then you should not make this
 * assumption.
 *
 * The reader API supports incremental reading, analogous to reading from a
 * stream or file. When you get data back, the reader advances its offset
 * within the data sequence so that the next read will start reading from
 * another offset; Reading data modifies the reader instance. If you want
 * to get all the data and are not sure where the reader came from, check
 * the offset and reset the reader if it is not at the beginning.
 *
 * The DataReaer abstraction is used throughout the interfaces within the
 * Google APIs Client Library for C++ where you might otherwise
 * expect to see a <code>string</code> or even StringPiece. <code>string</code>
 * and StringPiece are used for efficiency and convienence where small or
 * constant data is expected, or the data is consumed by an external library
 * that uses standard strings. SataReader is used in interfaces where that
 * involve opaque data that can be arbitrarily large or might not naturally
 * reside in memory already.
 *
 * A DataReader can be used in conjunction with a DataWriter, however this
 * is not necessary. You can use a DataWriter to write bytes then hvee that
 * writer create a reader to read those bytes back. This is useful if you
 * dont know what that byte stream is, such as an HTTP payload you are
 * receiving from a server. However if you already have access to the bytes
 * then it is easier and more efficeint to create a reader directly from those
 * bytes.
 *
 * It may also be that the writer serves no purpose other than to
 * create a reader, in which case you may as well just create a reader.
 * This is why some objects provide factory methods that return readers
 * rather than define serialization methods that take writers.
 *
 * DataReaders are not thread-safe.
 *
 * @see DataWriter
 * @see NewManagedInMemoryDataReader
 * @see NewManageFileDataReader
 * @see NewManageCompositeiDataReader
 */
class DataReader {
 public:
  /*
   * Standard destructor.
   */
  virtual ~DataReader();

  /*
   * Returns true if the reader is generally seekable.
   *
   * This does not guarantee that SetOffset will always work, but
   * indicates whether it is expected to in general. As a rule of thumb
   * this would be true if the backing store is reliable and false if it
   * is transient.
   *
   * This value is meant as a hint for consumers and not as a constraint
   * to attempting to SetOffset.
   *
   * @return The base class assumes false to be conservative.
   */
  virtual bool seekable() const;

  /*
   * Indicates whether unread bytes still remain in the stream or not.
   *
   * @return true if there will never be more bytes to be read ahead
   *              of the current offset.
   * To distinguish between a successful read and an error, you must check
   * either error() or ok() since hard errors imply done() as well.
   */
  bool done() const  { return done_; }

  /*
   * Determine if we've encountered a hard error or not.
   *
   * Hard errors are sticky and can only be cleared with Reset(), if at all.
   * This is equivalent to !ok().
   *
   * @return true if a hard error was set, false otherwise.
   *
   * @see ok
   * @see Status
   */
  bool error() const { return !status_.ok(); }

  /*
   * Determine if we've encountered a hard error or not.
   *
   * This is equivalent to !error().
   * @return true if no hard error has been set, false if an error.
   * @see error
   */
  bool ok() const { return status_.ok(); }

  /*
   * Returns details for the error on the stream, if any.
   *
   * @return a successful status if the stream is ok, otherwise the
   * error encounteredd.
   */
  googleapis::util::Status status() const { return status_; }

  /*
   * Returns the current offset in the byte sequence.
   * @return The number of bytes read so far. 0 is the beginning.
   */
  int64 offset() const { return offset_; }

  /*
   * Set the offset in the byte sequence for the next read.
   *
   * @param[in] position The new offset. 0 is the beginning. There is no
   * means to specify the end other than by absolute position.
   *
   * @return offset that was set. -1 indicates complete failure.
   * if the result was < offset then the reader will be positioned at the
   * end of the sequence.
   */
  int64 SetOffset(int64 position);

  /*
   * Resets the reader back to the beginning of the byte sequence.
   *
   * If the reset succeeds, the offset will be 0. If the reset fails then the
   * reader will be in an error() state preventing any future reads.
   *
   * @return false on failure. See status() for details.
   *
   * @see SetOffset
   */
  bool Reset() { return SetOffset(0) == 0; }

  /*
   * Keeps reading synchronously until the request is satisfied.
   *
   * If the stream is done before the requested bytes are read, then
   * this will return a partial read to the point where the stream
   * was done. You will need to check ok() or error() if you want
   * to distinguish the partial read being due to the end of the sequence
   * or a hard error.
   *
   * Reading will advance the offset by the number of bytes actually read.
   *
   * @param[in] max_bytes The most bytes to read.
   * @param[in] storage The storage to read into must have at
   *            least max_bytes of capacity allocated.
   * @return the number of bytes actually read might be 0 and the reader may
   *         still not be done(). Negative values are never returned.
   *
   * @see ReadToString
   * @see RemainderToString
   */
  int64 ReadToBuffer(int64 max_bytes, char* storage);

  /*
   * Keeps reading synchronously until the request is satisfied.
   *
   * If the stream is done before the requested bytes are read, then
   * this will return a partial read to the point where the stream
   * was done. You will need to check ok() or error() if you want
   * to distinguish the partial read being due to the end of the sequence
   * or a hard error.
   *
   * Reading will advance the offset by the number of bytes actually read.
   *
   * @param[in] max_bytes The most bytes to read.
   * @param[in,out] append_to The string to append the data into.
   *        If you need to look
   *        at the raw memory, use string::data() rather than string::c_str()
   *        since the byte sequence you've read from may contain binary data.
   * @return the number of bytes actually read might be 0 and the reader may
   *         still not be done(). Negative values are never returned.
   *
   * @see ReadToBuffer
   * @see RemainderToString
   */
  int64 ReadToString(int64 max_bytes, string* append_to);

  /*
   * Keep reading synchronously until done().
   *
   * You should check either done() or error() to determine whether you are
   * done() because of a hard error or because you read all remaining bytes.
   *
   * @return the remaining string from the offset.
   *
   * @see ReadToBuffer
   * @see ReadToString
   */
  string RemainderToString() {
    string result;
    ReadToString(kint64max, &result);
    return result;
  }

  /*
   * Returns the total length of the byte sequence if it is known.
   *
   * @return -1 if the length is not known. Otherwise the total length
   *         from the beginning regardless of the current offset.
   */
  int64 TotalLengthIfKnown() const { return total_length_; }

  /*
   * Reads until the pattern is found or end of stream is hit.
   *
   * @param[in] pattern  The string to search for is null terminated
   * @param[out] consuemed The consumed bytes are copied here. It will end
   *                       with the pattern if found. If the pattern is not
   *                       found then it will be the remainder of the stream.
   * @return true if the pattern was found, false otherwise.
   */
  bool ReadUntilPatternInclusive(const string& pattern, string* consumed);

 protected:
  /*
   * Standard reader constructor.
   *
   * The deleter determines whether the reader will be characterized
   * as begin "managed" or "unmanaged". A NULL reader indicates it is
   * "unmanaged".
   *
   * Managed readers call a closure when they are destroyed. Often this
   * closure is used to free up resources consumed by the byte sequence
   * the reader is reading from. However the closure may do anything.
   *
   * @warning The reader instance will not be valid when teh deleter is
   *          called
   *
   * @param[in] deleter If non-NULL then the caller retains ownership.
   *            Typically this will be a self-managed deleter that
   *            self-destructs once run.
   *
   * @see NewCallback
   */
  explicit DataReader(Closure* deleter);

  /*
   * Implementation hook to read bytes into storage.
   *
   * This method is called by the public Read* methods to perform the actual
   * data reading. The base class will do the adminstrative stuff, including
   * updating the offset. However marking done and error status are left to
   * this method.
   *
   * @param[in] max_bytes Read at most max_bytes into storage.
   * @param[in] storage At least max_bytes of allocated storage.
   *
   * @return Number of bytes actually written into storage. 0 has no special
   *         meaning beyond no byets were written for whatever reason.
   *         0 does not indicate any kind of failure. This method should
   *         never return a negative number.
   *
   * This method is resopnsible for explicitly marking the reader done() when
   * there is no more data to be returned by future calls. It should set the
   * status if a hard error is encountered.
   *
   * This method is required of all concrete readers as the sole point
   * of access.
   *
   * @see set_done
   * @see set_etatus
   */
  virtual int64 DoReadToBuffer(int64 max_bytes, char* storage) = 0;

  /*
   * Sets the offset into the byte sequence.
   *
   * This method is called by the public SetOffset() method. The public method
   * will do the administrative stuff including resetting the offset and
   * updating the status based on the status returned by this method.
   *
   * This method will return the actual offset that the sequence is at after
   * attempting to set. The intent is that if offset is too big then the
   * reader will seek to the end and return the offset of the end. This isnt
   * an ideal definition but is compatible with being able to seek into
   * readers of unknown size.
   *
   * In a complete failure the offset will be -1 (invalid) and require a
   * successful SetOffset to restore.
   *
   * @return the offset that was seeked to or -1 on complete failure. The
   * seek should not exceed the length of the bytestream.
   */
  virtual int64 DoSetOffset(int64 position);

  /*
   * Appends to the consumed string until the pattern is found or done.
   *
   * @param[in] pattern A null-terminated string pattern to search for.
   * @param[in, out] consumed The string to append to.
   * @return true if the pattern is found, false otherwise.
   */
  virtual bool DoAppendUntilPatternInclusive(
      const string& pattern, string* consumed);

  /*
   * Sets the status as a means to pass error details back to the caller.
   *
   * Setting an error implies setting done as well. However clearing an
   * error by setting an ok status will not clear done. To clear the done
   * state you must explicitly call set_done.
   *
   * @param[in] status An error implies a hard failure and will mark the reader
   *            done.
   */
  void set_status(util::Status status);

  /*
   * Indicates whether there is more data to be read.
   *
   * @param[in] done true if there si no more data, false if there is.
   */
  void set_done(bool done)   { done_ = done; }

  /*
   * Sets the total number of bytes in the reader.
   *
   * If calling this method, you should call it from within your constructor if
   * possible.
   *
   * @param[in] length The number of bytes is returned by TotalLengthIfKnown.
   *       -1 indicates unknown. 0 Indicates none. The default is -1 (unknown).
   *
   */
  void set_total_length(int64 length);

 private:
  Closure* deleter_;       // Can be NULL
  int64 total_length_;     // < 0 if unknown
  int64 offset_;           // bytes read so far
  googleapis::util::Status status_;  // ok() unless in an error state.
  bool done_;              // we'll never return any more data (e.g. eof)

  DISALLOW_COPY_AND_ASSIGN(DataReader);
};


/*
 * Returns a data reader that is always in an error state.
 * @ingroup DataLayerRaw
 *
 * @param[in] status The permanent status() to give the reader, expaining
 *            why any access to it will fail.
 * @param[in] deleter If non-NULL this reader will be a "managed"
 *            reader and run teh deleter in its destructor.
 *
 * This reader is meant to be returned by factory methdos that fail.
 * It is a placeholder value so that DataReaders are never NULL.
 */
DataReader* NewManagedInvalidDataReader(
    googleapis::util::Status status, Closure* deleter);

/*
 * Returns an unmanaged invalid data reader.
 * @ingroup DataLayerRaw
 *
 * @param[in] status The permanent status() to give the reader.
 *
 * @see NewManagedInvalidDataReader
 */
inline DataReader* NewUnmanagedInvalidDataReader(util::Status status) {
  return NewManagedInvalidDataReader(status, NULL);
}


/*
 * Reads from a contiguous byte array.
 * @ingroup DataLayerRaw
 *
 * Managed InMemoryDataReader instances are very low overhead if the memory
 * already exists. However, the owner must ensure that the referenced data
 * remains valid and unchanged over the lifetime of the reader. Modifying the
 * data will corrupt the reader.
 *
 * Unmanaged instances can pass memory ownership into the instance itself and
 * have the instance encapsulate it by being the only remaining direct
 * reference. This reduces the chance for the data to get corrupted and
 * guarantees that the data will remain vaid for as long, and only as long,
 * as the reader remains.
 *
 * For brevity only StringPiece variations are provided. If you have an
 * ordinary char*, you can turn it into a StringPiece by passing
 * StringPiece(ptr, len) presuming that you know the length. If you do not
 * know the length then you cannot use the InMemoryDataReader anyway.
 *
 * If your char* is part of a larger object which you only need to support
 * the reader, you can have the reader manage it by passing a
 * DeletePointerClosure as the reader's managing closure.
 *
 * <pre>
 *   char* data = obj->data()
 *   int64 len = obj->data_len()
 *   NewManagedInMemoryDataReader(StringPiece(data, len),
 *                                DeletePointerClosure(obj))
 * </pre>
 *
 * @param[in] data The data used by the reader must remain valid and unchanged
 *            over the lifetime of the readaer.
 * @param[in] deleter If non-NULL then this will be a managed reader calling
 *        the deletre when this object is destroyed. See the base DataReader
 *        class for more information about managed readers.
 */
DataReader* NewManagedInMemoryDataReader(
    const StringPiece& data, Closure* deleter);

/*
 * Creates an unmanaged InMenoryDataReader
 * @ingroup DataLayerRaw
 *
 * This is shorthand for NewManagedInMemoryDataReader(data, NULL)
 * @see NewManagedInMemoryDataReader
 */
DataReader* NewUnmanagedInMemoryDataReader(const StringPiece& data);

/*
 * Returns a managed reader that consumes a dynanic input string.
 * @ingroup DataLayerRaw
 *
 * This is a convienence function to create a managed reader from
 * a string pinter.
 *
 * @param[in] data Takes ownership of the string
 * @see NewManagedInMemoryDataReader
 */
DataReader* NewManagedInMemoryDataReader(string* data);

/*
 * Creates a managed InMemoryDataReader from an existing string.
 * @ingroup DataLayerRaw
 *
 * This function will create a copy of the string then manage the copy.
 *
 * @see NewManagedInMemoryDataReader
 */
inline DataReader* NewManagedInMemoryDataReader(const string& data) {
  return NewManagedInMemoryDataReader(new string(data));
}


/*
 * Returns an InMemoryDataReader that returns the content from another
 * data reader.
 * @ingroup DataLayerRaw
 *
 * This reader is only intended to make unreliable readers reliable
 * when you need to reset them. It is high overhead since you are creating
 * an in-memory copy of the data, which defeats the whole point of having
 * a reader and will cost twice the storage.
 *
 * @param[in] reader Ownership of the wrapped reader will be passed the new
 *            instance to manage.
 * @param[in] buffer_bytes The total number of bytes to expect,
 *            or -1 if unknown.
 */
DataReader* NewManagedBufferedDataReader(
    DataReader* reader, int64 buffer_bytes);

/*
 * Similar to NewUManagedBufferdDataReader but the caller retains ownership
 * of the original reader.
 * @ingroup DataLayerRaw
 *
 * @param[in] reader The caller keeps ownership of the reader being wrapped.
 * @param[in] buffer_bytes The total number of bytes to expect,
 *            or -1 if unknown.
 *
 * @see NewManagedBufferedDataReader
 */
DataReader* NewUnmanagedBufferedDataReader(
    DataReader* reader, int64 buffer_bytes);

/*
 * A general form of a managed BufferedDataReader.
 * @ingroup DataLayerRaw
 *
 * @param[in] reader The caller maintains ownership of this reader,
 *            however the caller may have passed ownership to the deleter.
 * @param[in] buffer_bytes The number of bytes to expect in the reader, or -1
 *            if not known.
 * @param[in] deleter The management closure to call when this instance is
 *            destroyed.
 */
DataReader* NewManagedBufferedDataReader(
    DataReader* reader, int64 buffer_bytes, Closure* deleter);


/*
 * Returns an unmanaged composite DataReader that reads directly from
 * one or more other data readers.
 * @ingroup DataLayerRaw
 *
 * The composite readers are not buffered at all by this reader so little
 * additional overhead is added. The component readers within this may
 * come from different types of sources or might just have different fragments
 * of memory.
 *
 * @param[in] readers The list of readers taht define the byte sequence
 *            returned by  this reader. The caller retains ownership of
 *            each of these and must guarantee they are kept valid and
 *            otherwise unused over the lifetime of this instance.
 *            if data is read from any of these outside the composite reader
 *            then the compsite reader will be corrupted.
 */
DataReader* NewUnmanagedCompositeDataReader(
    const std::vector<DataReader*>& readers);

/*
 * Returns a managed composite DataReader that reads directly from one or
 * more other data readers.
 * @ingroup DataLayerRaw
 *
 * The composite readers are not buffered at all by this reader so little
 * additional overhead is added. The component readers within this may
 * come from different types of sources or might just have different fragments
 * of memory.
 *
 * @param[in] readers The list of readers taht define the byte sequence
 *            returned by this reader. The caller retains ownership of each of
 *            these readers though may pass their ownership to the deleter.
 * @param[in] deleter The management closure to be called when the composite
 *            reader is destroyed.
 */
DataReader* NewManagedCompositeDataReader(
    const std::vector<DataReader*>& readers, Closure* deleter);

/*
 * Creates a managed closure that deletes an entire vector of readers when run.
 * @ingroup DataLayerRaw
 *
 * This is a convienence function for creating a closure to pass to
 * NewManagedCompositeDataReader.
 *
 * @param[in] readers Takes ownership of the vector and its contents are
 *            passed so the vector should hae been allocated with the new
 *            operator.
 */
Closure* NewCompositeReaderListAndContainerDeleter(
    std::vector<DataReader*>* readers);


/*
 * Creates a managed DataReader that reads its byte stream from a file on disk.
 * @ingroup DataLayerRaw
 *
 * If the path does not exist, or cannot be read for some other reason then
 * this will return a reader in the error state. Check its status() to get
 * more details on the error.
 *
 * The caller should ensure the file remains valid and unchanged over the
 * lifetime of the reader. Changing the file contents will corrupt the reader.
 *
 * @param[in] path The path to the file to read from.
 * @param[in] deleter If non-NULL this closure will be called when the reader
 *            is destroyed. The reader itself will no longer be valid when the
 *            closure is called.
 */
DataReader* NewManagedFileDataReader(const StringPiece& path, Closure* deleter);

/*
 * Creates an unmanaged DataReader that reads its byte stream from a file on
 * disk.
 * @ingroup DataLayerRaw
 *
 * If the path does not exist, or cannot be read for some other reason then
 * this will return a reader in the error state. Check its status() to get
 * more details on the error.
 *
 * The caller should ensure the file remains valid and unchanged over the
 * lifetime of the reader. Changing the file contents will corrupt the reader.
 *
 * @param[in] path The path to the file to read from.
 */
DataReader* NewUnmanagedFileDataReader(const StringPiece& path);



/*
 * Creates a managed reader that reads its byte stream from a generic
 * C++ std::istream of unknown length.
 * @ingroup DataLayerRaw
 *
 * If you know how many bytes are in the stream then you should use
 * NewManagedIstreamDataReaderWithLength
 *
 * @param[in] stream The caller retains ownership of the stream to read from,
 *            though the caller may pass ownership to the deleter.
 *            The caller must insure it remains valid over the lifetime of the
 *            reader, and should not read from it outside the returned reader.
 * @param[in] deleter The managing closure is called when the reaer is
 *            destroyed. The reader will not be valid when the deleter is
 *            called.
 *
 * @see NewDeleterClosure
 * @see NewManagedFileDataReader
 */
DataReader* NewManagedIstreamDataReader(std::istream* stream, Closure* deleter);

/*
 * Creates a managed reader that reads its byte stream from a generic
 * C++ std::istream of unknown length.
 * @ingroup DataLayerRaw
 *
 * @param[in] stream The caller retains ownership of the stream to read from,
 *            though the caller may pass ownership to the deleter.
 *            The caller must insure it remains valid over the lifetime of the
 *            reader, and should not read from it outside the returned reader.
 * @param[in] length The length if known, or -1 can be used if unknown.
 *            A length of 0 indicates and empty stream.
 * @param[in] deleter The managing closure is called when the reaer is
 *            destroyed. The reader will not be valid when the deleter is
 *            called.
 *
 * @see NewDeleterClosure
 * @see NewMmanagedFileDataReader
 */
DataReader* NewManagedIstreamDataReaderWithLength(
    std::istream* stream, int64 length, Closure* deleter);

/*
 * Creates an unmanaged reader that reads its byte stream from a generic
 * C++ std::istream of unknown length.
 * @ingroup DataLayerRaw
 *
 * This is similar to NewManagedIstreamDataReader, but with a NULL deleter.
 *
 * @see NewManagedIStreamDataReader
 * @see NewUnmanagedFileDataReader
 */
DataReader* NewUnmanagedIstreamDataReader(std::istream* stream);

/*
 * Creates an unmanaged reader that reads its byte stream from a generic
 * C++ std::istream of a known length.
 * @ingroup DataLayerRaw
 *
 * This is similar to NewManagedIstreamDataReaderWithLength,
 * but with a NULL deleter.
 *
 * @see NewManagedIStreamDataReaderWIthLength
 * @see NewUnmanagedFileDataReader
 */
DataReader* NewUnmanagedIstreamDataReaderWithLength(
    std::istream* stream, int64 length);

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_DATA_READER_H_

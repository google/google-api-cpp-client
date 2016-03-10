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


#include <vector>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {

class CompositeDataReader : public DataReader {
 public:
  CompositeDataReader(
      const std::vector<DataReader*>& readers, Closure* deleter)
      : DataReader(deleter),
        readers_(readers), reader_index_(0), seekable_(true) {
    int64 sum = 0;

    // Keep starting offset for each segment. -1 is unknown and we'll
    // fill it in as we discovery it.
    start_offset_.resize(readers_.size(), -1);
    for (int i = 0; i < readers_.size(); ++i) {
      start_offset_[i] = sum;
      if (!readers_[i]->seekable()) {
        seekable_ = false;
      }
      if (sum >= 0) {
        int64 part_len = readers_[i]->TotalLengthIfKnown();
        if (part_len < 0) {
          sum = -1;
        } else {
          sum += part_len;
        }
      }
    }
    if (sum >= 0) {
      set_total_length(sum);
    }
  }

  ~CompositeDataReader() {
  }

  virtual bool seekable() const { return seekable_; }

 protected:
  virtual int64 DoSetOffset(int64 position) {
    if (readers_.size() == 0) {
      set_done(true);
      return 0;
    }

    if (reader_index_ >= start_offset_.size()
        || position < start_offset_[reader_index_]) {
      return SeekBack(position);
    } else {
      return SeekAhead(position);
    }
  }

  int64 DoReadToBuffer(int64 max_bytes, char* storage) {
    // Advance through the readers until we find something not empty.
    int64 total_read = 0;
    for (;
         reader_index_ < readers_.size() && total_read < max_bytes;
         ++reader_index_) {
      total_read += readers_[reader_index_]->ReadToBuffer(
          max_bytes - total_read, storage + total_read);
      if (readers_[reader_index_]->error()) {
        set_status(readers_[reader_index_]->status());
        return total_read;
      }
      if (!readers_[reader_index_]->done()) {
        // Return early to keep the logic simpler. The DataReader::ReadToBuffer
        // method that calls this will keep looping for us to take another
        // crack at this reader_index_.
        return total_read;
      }

      // Update the start_offset_ for the next segment if it wasnt already set.
      if (reader_index_ < readers_.size() - 1) {
        int64 offset_now = offset() + total_read;
        int next_index = reader_index_ + 1;
        if (start_offset_[next_index] < 0) {
          start_offset_[next_index] = offset_now;
        } else {
          CHECK_EQ(start_offset_[next_index], offset_now);
        }
      }
    }

    if (reader_index_ >= readers_.size() - 1) {
      set_done(readers_[readers_.size() - 1]->done());
    }

    return total_read;
  }

 private:
  std::vector<DataReader*> readers_;
  std::vector<int64> start_offset_;
  int reader_index_;
  bool seekable_;

  int64 SeekAhead(int64 position) {
    if (readers_.size() == 0) {
      return 0;
    }

    // We'll stop just before the last index and handle that outside the loop.
    // That way we'll know there is always a next index to lookahead in the
    // loop.
    const int last_index = readers_.size() - 1;

    // Advance toward end.
    for (; reader_index_ < readers_.size(); ++reader_index_) {
      // The -1 here is to force a seek in the last element so we
      // return the last position in the byte sequence if the request position
      // was beyond it.
      int64 next_start = reader_index_ < last_index
          ? start_offset_[reader_index_ + 1]
          : -1;
      if (next_start > 0 && next_start < position) {
        // Position is after this segment
        continue;
      }

      // If next_start > 0 then the position will be in this sequence.
      // If next_start < 0 then this sequence is of unknown length so may
      // or may not contain the position. Either way we'll attempt to seek
      // within it.
      //
      // The component does not know its base offset relative to this
      // composite sequence so we'll seek within the component using a
      // relative position to the starting point in the outer sequence of
      // this composite reader.
      int64 rel_offset = position - start_offset_[reader_index_];
      CHECK_LE(0, rel_offset);

      int64 at = readers_[reader_index_]->SetOffset(rel_offset);
      if (!readers_[reader_index_]->ok()) {
        set_status(readers_[reader_index_]->status());
        return -1;
      }
      if (at == rel_offset) return position;

      // We hit the end of this segment before hitting the desired position.
      // Before we continue into the next, we'll update the starting position
      // since we now know it having discovered the end of this segment.
      if (reader_index_ < last_index) {
        start_offset_[reader_index_ + 1] = start_offset_[reader_index_] + at;
      }
    }

    // If we got this far we hit the end of file before finding the position.
    // Return the offset of the last reader, which should have seeked to the
    // end of its sequence to give us the final offset.
    return start_offset_[last_index] + readers_[last_index]->offset();
  }

  int64 SeekBack(int64 position) {
    if (readers_.size() == 0) {
      return 0;
    }

    // Rewind toward front
    if (reader_index_ >= start_offset_.size()) {
      reader_index_ = start_offset_.size() - 1;
    }
    for (; start_offset_[reader_index_] > position
             && reader_index_ > 0; --reader_index_) {
      readers_[reader_index_]->SetOffset(0);
      if (!readers_[reader_index_]->ok()) {
        set_status(readers_[reader_index_]->status());
        return -1;
      }
      CHECK(readers_[reader_index_ - 1]->done());
      CHECK(readers_[reader_index_ - 1]->ok());
    }
    // We should have resolved this as we advanced forward before we
    // could have even attempted to seek backward.
    CHECK_LE(0, start_offset_[reader_index_]);

    int64 rel_offset = position - start_offset_[reader_index_];
    LOG(INFO) << "REL_OFFSET="<< rel_offset;
    int64 result = readers_[reader_index_]->SetOffset(rel_offset);
    set_done(false);
    set_status(readers_[reader_index_]->status());
    if (result < 0) return -1;

    return result + start_offset_[reader_index_];
  }

  DISALLOW_COPY_AND_ASSIGN(CompositeDataReader);
};

DataReader* NewUnmanagedCompositeDataReader(
    const std::vector<DataReader*>& readers) {
  return new CompositeDataReader(readers, NULL);
}

DataReader* NewManagedCompositeDataReader(
    const std::vector<DataReader*>& readers, Closure* deleter) {
  return new CompositeDataReader(readers, deleter);
}

static void DeleteCompositeReadersAndContainer(
    std::vector<DataReader*>* readers) {
  for (std::vector<DataReader*>::iterator it = readers->begin();
       it != readers->end();
       ++it) {
    delete *it;
  }
  delete readers;
}

Closure* NewCompositeReaderListAndContainerDeleter(
    std::vector<DataReader*>* readers) {
  return NewCallback(&DeleteCompositeReadersAndContainer, readers);
}

}  // namespace client

}  // namespace googleapis

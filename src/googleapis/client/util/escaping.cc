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


#include <cassert>
#include <string>
using std::string;

#include "googleapis/strings/ascii_ctype.h"
#include "googleapis/client/util/escaping.h"

namespace googleapis {

namespace googleapis_util {


namespace {

static const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char kWebSafeBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// Taken from stl_util.h
inline char* string_as_array(string* str) {
  return str->empty() ? nullptr : &*str->begin();
}

}  // namespace

// ===========================================================================
// Base64 encoding and decoding.

static int Base64EscapeInternal(const unsigned char* src, int szsrc, char* dest,
                                int szdest, const char* base64,
                                bool do_padding) {
  static const char kPad64 = '=';

  if (szsrc <= 0) return 0;

  char *cur_dest = dest;
  const unsigned char *cur_src = src;

  // Three bytes of data encodes to four characters of cyphertext.
  // So we can pump through three-byte chunks atomically.
  while (szsrc > 2) { /* keep going until we have less than 24 bits */
    if ((szdest -= 4) < 0) return 0;
    cur_dest[0] = base64[cur_src[0] >> 2];
    cur_dest[1] = base64[((cur_src[0] & 0x03) << 4) + (cur_src[1] >> 4)];
    cur_dest[2] = base64[((cur_src[1] & 0x0f) << 2) + (cur_src[2] >> 6)];
    cur_dest[3] = base64[cur_src[2] & 0x3f];

    cur_dest += 4;
    cur_src += 3;
    szsrc -= 3;
  }

  /* now deal with the tail (<=2 bytes) */
  switch (szsrc) {
    case 0:
      // Nothing left; nothing more to do.
      break;
    case 1:
      // One byte left: this encodes to two characters, and (optionally)
      // two pad characters to round out the four-character cypherblock.
      if ((szdest -= 2) < 0) return 0;
      cur_dest[0] = base64[cur_src[0] >> 2];
      cur_dest[1] = base64[(cur_src[0] & 0x03) << 4];
      cur_dest += 2;
      if (do_padding) {
        if ((szdest -= 2) < 0) return 0;
        cur_dest[0] = kPad64;
        cur_dest[1] = kPad64;
        cur_dest += 2;
      }
      break;
    case 2:
      // Two bytes left: this encodes to three characters, and (optionally)
      // one pad character to round out the four-character cypherblock.
      if ((szdest -= 3) < 0) return 0;
      cur_dest[0] = base64[cur_src[0] >> 2];
      cur_dest[1] = base64[((cur_src[0] & 0x03) << 4) + (cur_src[1] >> 4)];
      cur_dest[2] = base64[(cur_src[1] & 0x0f) << 2];
      cur_dest += 3;
      if (do_padding) {
        if ((szdest -= 1) < 0) return 0;
        cur_dest[0] = kPad64;
        cur_dest += 1;
      }
      break;
    default:
      // Should not be reached: blocks of 3 bytes are handled
      // in the while loop before this switch statement.
      assert(szsrc >= 0 && szsrc <= 2);
      break;
  }
  return (cur_dest - dest);
}


int WebSafeBase64Escape(const unsigned char* src, int szsrc, char* dest,
                        int szdest, bool do_padding) {
  return Base64EscapeInternal(src, szsrc, dest, szdest,
                              kWebSafeBase64Chars, do_padding);
}


int Base64Escape(const unsigned char* src, int szsrc, char* dest, int szdest) {
  return Base64EscapeInternal(src, szsrc, dest, szdest, kBase64Chars, true);
}

void Base64EscapeInternal(const unsigned char* src, int szsrc,
                          string* dest, bool do_padding,
                          const char* base64_chars) {
  const int calc_escaped_size =
    CalculateBase64EscapedLen(szsrc, do_padding);
  dest->clear();
  dest->resize(calc_escaped_size, '\0');
#ifndef NDEBUG
  const int escaped_len =
#endif
      Base64EscapeInternal(src, szsrc, string_as_array(dest), dest->size(),
                           base64_chars, do_padding);
  assert(calc_escaped_size == escaped_len);
}

void Base64Escape(const unsigned char *src, int szsrc,
                  string* dest, bool do_padding) {
  Base64EscapeInternal(src, szsrc, dest, do_padding, kBase64Chars);
}

void Base64Escape(const string& src, string* dest) {
  Base64Escape(reinterpret_cast<const unsigned char*>(src.data()),
               src.size(), dest, true);
}

void WebSafeBase64Escape(const unsigned char *src, int szsrc,
                         string *dest, bool do_padding) {
  Base64EscapeInternal(src, szsrc, dest, do_padding, kWebSafeBase64Chars);
}

int CalculateBase64EscapedLen(int input_len, bool do_padding) {
  // Base64 encodes each three bytes of input into four bytes of output.
  int len = (input_len / 3) * 4;

  if (input_len % 3 == 0) {
    // (from http://www.ietf.org/rfc/rfc3548.txt)
    // (1) the final quantum of encoding input is an integral multiple of 24
    // bits; here, the final unit of encoded output will be an integral
    // multiple of 4 characters with no "=" padding,
  } else if (input_len % 3 == 1) {
    // (from http://www.ietf.org/rfc/rfc3548.txt)
    // (2) the final quantum of encoding input is exactly 8 bits; here, the
    // final unit of encoded output will be two characters followed by two
    // "=" padding characters, or
    len += 2;
    if (do_padding) {
      len += 2;
    }
  } else {  // (input_len % 3 == 2)
    // (from http://www.ietf.org/rfc/rfc3548.txt)
    // (3) the final quantum of encoding input is exactly 16 bits; here, the
    // final unit of encoded output will be three characters followed by one
    // "=" padding character.
    len += 3;
    if (do_padding) {
      len += 1;
    }
  }

  assert(len >= input_len);  // make sure we didn't overflow
  return len;
}


static int Base64UnescapeInternal(const char* src, int szsrc, char* dest,
                                  int szdest, const signed char* unbase64) {
  static const char kPad64 = '=';

  int decode = 0;
  int destidx = 0;
  int state = 0;
  unsigned int ch = 0;
  unsigned int temp = 0;

  // The GET_INPUT macro gets the next input character, skipping
  // over any whitespace, and stopping when we reach the end of the
  // string or when we read any non-data character.  The arguments are
  // an arbitrary identifier (used as a label for goto) and the number
  // of data bytes that must remain in the input to avoid aborting the
  // loop.
#define GET_INPUT(label, remain)                 \
  label:                                         \
    --szsrc;                                     \
    ch = *src++;                                 \
    decode = unbase64[ch];                       \
    if (decode < 0) {                            \
      if (ascii_isspace(ch) && szsrc >= remain)  \
        goto label;                              \
      state = 4 - remain;                        \
      break;                                     \
    }

  // if dest is null, we're just checking to see if it's legal input
  // rather than producing output.  (I suspect this could just be done
  // with a regexp...).  We duplicate the loop so this test can be
  // outside it instead of in every iteration.

  if (dest) {
    // This loop consumes 4 input bytes and produces 3 output bytes
    // per iteration.  We can't know at the start that there is enough
    // data left in the string for a full iteration, so the loop may
    // break out in the middle; if so 'state' will be set to the
    // number of input bytes read.

    while (szsrc >= 4)  {
      // We'll start by optimistically assuming that the next four
      // bytes of the string (src[0..3]) are four good data bytes
      // (that is, no nulls, whitespace, padding chars, or illegal
      // chars).  We need to test src[0..2] for nulls individually
      // before constructing temp to preserve the property that we
      // never read past a null in the string (no matter how long
      // szsrc claims the string is).

      if (!src[0] || !src[1] || !src[2] ||
          (temp = ((unbase64[src[0] & 0xff] << 18) |
                   (unbase64[src[1] & 0xff] << 12) |
                   (unbase64[src[2] & 0xff] << 6) |
                   (unbase64[src[3] & 0xff]))) & 0x80000000) {
        // Iff any of those four characters was bad (null, illegal,
        // whitespace, padding), then temp's high bit will be set
        // (because unbase64[] is -1 for all bad characters).
        //
        // We'll back up and resort to the slower decoder, which knows
        // how to handle those cases.

        GET_INPUT(first, 4);
        temp = decode;
        GET_INPUT(second, 3);
        temp = (temp << 6) | decode;
        GET_INPUT(third, 2);
        temp = (temp << 6) | decode;
        GET_INPUT(fourth, 1);
        temp = (temp << 6) | decode;
      } else {
        // We really did have four good data bytes, so advance four
        // characters in the string.

        szsrc -= 4;
        src += 4;
        decode = -1;
        ch = '\0';
      }

      // temp has 24 bits of input, so write that out as three bytes.

      if (destidx+3 > szdest) return -1;
      dest[destidx+2] = temp;
      temp >>= 8;
      dest[destidx+1] = temp;
      temp >>= 8;
      dest[destidx] = temp;
      destidx += 3;
    }
  } else {
    while (szsrc >= 4)  {
      if (!src[0] || !src[1] || !src[2] ||
          (temp = ((unbase64[src[0] & 0xff] << 18) |
                   (unbase64[src[1] & 0xff] << 12) |
                   (unbase64[src[2] & 0xff] << 6) |
                   (unbase64[src[3] & 0xff]))) & 0x80000000) {
        GET_INPUT(first_no_dest, 4);
        GET_INPUT(second_no_dest, 3);
        GET_INPUT(third_no_dest, 2);
        GET_INPUT(fourth_no_dest, 1);
      } else {
        szsrc -= 4;
        src += 4;
        decode = -1;
        ch = '\0';
      }
      destidx += 3;
    }
  }

#undef GET_INPUT

  // if the loop terminated because we read a bad character, return
  // now.
  if (decode < 0 && ch != '\0' && ch != kPad64 && !ascii_isspace(ch))
    return -1;

  if (ch == kPad64) {
    // if we stopped by hitting an '=', un-read that character -- we'll
    // look at it again when we count to check for the proper number of
    // equals signs at the end.
    ++szsrc;
    --src;
  } else {
    // This loop consumes 1 input byte per iteration.  It's used to
    // clean up the 0-3 input bytes remaining when the first, faster
    // loop finishes.  'temp' contains the data from 'state' input
    // characters read by the first loop.
    while (szsrc > 0)  {
      --szsrc;
      ch = *src++;
      decode = unbase64[ch];
      if (decode < 0) {
        if (ascii_isspace(ch)) {
          continue;
        } else if (ch == '\0') {
          break;
        } else if (ch == kPad64) {
          // back up one character; we'll read it again when we check
          // for the correct number of equals signs at the end.
          ++szsrc;
          --src;
          break;
        } else {
          return -1;
        }
      }

      // Each input character gives us six bits of output.
      temp = (temp << 6) | decode;
      ++state;
      if (state == 4) {
        // If we've accumulated 24 bits of output, write that out as
        // three bytes.
        if (dest) {
          if (destidx+3 > szdest) return -1;
          dest[destidx+2] = temp;
          temp >>= 8;
          dest[destidx+1] = temp;
          temp >>= 8;
          dest[destidx] = temp;
        }
        destidx += 3;
        state = 0;
        temp = 0;
      }
    }
  }

  // Process the leftover data contained in 'temp' at the end of the input.
  int expected_equals = 0;
  switch (state) {
    case 0:
      // Nothing left over; output is a multiple of 3 bytes.
      break;

    case 1:
      // Bad input; we have 6 bits left over.
      return -1;

    case 2:
      // Produce one more output byte from the 12 input bits we have left.
      if (dest) {
        if (destidx+1 > szdest) return -1;
        temp >>= 4;
        dest[destidx] = temp;
      }
      ++destidx;
      expected_equals = 2;
      break;

    case 3:
      // Produce two more output bytes from the 18 input bits we have left.
      if (dest) {
        if (destidx+2 > szdest) return -1;
        temp >>= 2;
        dest[destidx+1] = temp;
        temp >>= 8;
        dest[destidx] = temp;
      }
      destidx += 2;
      expected_equals = 1;
      break;

    default:
      // state should have no other values at this point.
      assert(state >= 0 && state <= 4);
  }

  // The remainder of the string should be all whitespace, mixed with
  // exactly 0 equals signs, or exactly 'expected_equals' equals
  // signs.  (Always accepting 0 equals signs is an extension
  // not covered in the RFC.)

  int equals = 0;
  while (szsrc > 0 && *src) {
    if (*src == kPad64)
      ++equals;
    else if (!ascii_isspace(*src))
      return -1;
    --szsrc;
    ++src;
  }

  return (equals == 0 || equals == expected_equals) ? destidx : -1;
}

static bool Base64UnescapeInternal(const char* src, int szsrc,
                                   string* dest,
                                   const signed char* unbase64) {
  // Determine the size of the output string.  Base64 encodes every 3 bytes into
  // 4 characters.  any leftover chars are added directly for good measure.
  // This is documented in the base64 RFC: http://www.ietf.org/rfc/rfc3548.txt
  const int dest_len = 3 * (szsrc / 4) + (szsrc % 4);

  dest->clear();
  dest->resize(dest_len);

  // We are getting the destination buffer by getting the beginning of the
  // string and converting it into a char *.
  const int len =
      Base64UnescapeInternal(
          src, szsrc, string_as_array(dest), dest->size(), unbase64);
  if (len < 0) {
    dest->clear();
    return false;
  }

  // could be shorter if there was padding
  assert(len <= dest_len);
  dest->resize(len);

  return true;
}

static const signed char kUnBase64[] = {
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      62/*+*/, -1,      -1,      -1,      63/*/ */,
  52/*0*/, 53/*1*/, 54/*2*/, 55/*3*/, 56/*4*/, 57/*5*/, 58/*6*/, 59/*7*/,
  60/*8*/, 61/*9*/, -1,      -1,      -1,      -1,      -1,      -1,
  -1,       0/*A*/,  1/*B*/,  2/*C*/,  3/*D*/,  4/*E*/,  5/*F*/,  6/*G*/,
  7/*H*/,  8/*I*/,  9/*J*/, 10/*K*/, 11/*L*/, 12/*M*/, 13/*N*/, 14/*O*/,
  15/*P*/, 16/*Q*/, 17/*R*/, 18/*S*/, 19/*T*/, 20/*U*/, 21/*V*/, 22/*W*/,
  23/*X*/, 24/*Y*/, 25/*Z*/, -1,      -1,      -1,      -1,      -1,
  -1,      26/*a*/, 27/*b*/, 28/*c*/, 29/*d*/, 30/*e*/, 31/*f*/, 32/*g*/,
  33/*h*/, 34/*i*/, 35/*j*/, 36/*k*/, 37/*l*/, 38/*m*/, 39/*n*/, 40/*o*/,
  41/*p*/, 42/*q*/, 43/*r*/, 44/*s*/, 45/*t*/, 46/*u*/, 47/*v*/, 48/*w*/,
  49/*x*/, 50/*y*/, 51/*z*/, -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1
};

static const signed char kUnWebSafeBase64[] = {
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      62/*-*/, -1,      -1,
  52/*0*/, 53/*1*/, 54/*2*/, 55/*3*/, 56/*4*/, 57/*5*/, 58/*6*/, 59/*7*/,
  60/*8*/, 61/*9*/, -1,      -1,      -1,      -1,      -1,      -1,
  -1,       0/*A*/,  1/*B*/,  2/*C*/,  3/*D*/,  4/*E*/,  5/*F*/,  6/*G*/,
  7/*H*/,  8/*I*/,  9/*J*/, 10/*K*/, 11/*L*/, 12/*M*/, 13/*N*/, 14/*O*/,
  15/*P*/, 16/*Q*/, 17/*R*/, 18/*S*/, 19/*T*/, 20/*U*/, 21/*V*/, 22/*W*/,
  23/*X*/, 24/*Y*/, 25/*Z*/, -1,      -1,      -1,      -1,      63/*_*/,
  -1,      26/*a*/, 27/*b*/, 28/*c*/, 29/*d*/, 30/*e*/, 31/*f*/, 32/*g*/,
  33/*h*/, 34/*i*/, 35/*j*/, 36/*k*/, 37/*l*/, 38/*m*/, 39/*n*/, 40/*o*/,
  41/*p*/, 42/*q*/, 43/*r*/, 44/*s*/, 45/*t*/, 46/*u*/, 47/*v*/, 48/*w*/,
  49/*x*/, 50/*y*/, 51/*z*/, -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1
};


bool Base64Unescape(const char* src, int szsrc, string* dest) {
  return Base64UnescapeInternal(src, szsrc, dest, kUnBase64);
}

bool WebSafeBase64Unescape(const char* src, int szsrc, string* dest) {
  return Base64UnescapeInternal(src, szsrc, dest, kUnWebSafeBase64);
}

}  // namespace googleapis_util

}  // namespace googleapis

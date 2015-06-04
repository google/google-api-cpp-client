#ifndef GOOGLEAPIS_UTIL_CANONICAL_ERRORS_H_
#define GOOGLEAPIS_UTIL_CANONICAL_ERRORS_H_

// This file declares a set of functions for working with Status objects from
// the canonical error space. There are functions to easily generate such
// status object and function for classifying them.

#include "googleapis/util/status.h"

namespace googleapis {
namespace util {

// Each of the functions below creates a canonical error with the given
// message. The error code of the returned status object matches the name of
// the function.
Status AbortedError(StringPiece message);
Status AlreadyExistsError(StringPiece message);
Status CancelledError(StringPiece message);
Status DataLossError(StringPiece message);
Status DeadlineExceededError(StringPiece message);
Status FailedPreconditionError(StringPiece message);
Status InternalError(StringPiece message);
Status InvalidArgumentError(StringPiece message);
Status NotFoundError(StringPiece message);
Status OutOfRangeError(StringPiece message);
Status PermissionDeniedError(StringPiece message);
Status UnauthenticatedError(StringPiece message);
Status ResourceExhaustedError(StringPiece message);
Status UnavailableError(StringPiece message);
Status UnimplementedError(StringPiece message);
Status UnknownError(StringPiece message);

// Each of the functions below returns true if the given status matches the
// canonical error code implied by the function's name. If necessary, the
// status will be converted to the canonical error space to perform the
// comparison.
bool IsAborted(const Status& status);
bool IsAlreadyExists(const Status& status);
bool IsCancelled(const Status& status);
bool IsDataLoss(const Status& status);
bool IsDeadlineExceeded(const Status& status);
bool IsFailedPrecondition(const Status& status);
bool IsInternal(const Status& status);
bool IsInvalidArgument(const Status& status);
bool IsNotFound(const Status& status);
bool IsOutOfRange(const Status& status);
bool IsPermissionDenied(const Status& status);
bool IsUnauthenticated(const Status& status);
bool IsResourceExhausted(const Status& status);
bool IsUnavailable(const Status& status);
bool IsUnimplemented(const Status& status);
bool IsUnknown(const Status& status);

}  // namespace util
}  // namespace googleapis

#endif  // GOOGLEAPIS_UTIL_CANONICAL_ERRORS_H_

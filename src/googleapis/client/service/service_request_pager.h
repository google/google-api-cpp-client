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
// A service request pager acts as a high level iterator for paging
// through results. Each page involves a round-trip request the the server.

#ifndef GOOGLEAPIS_SERVICE_SERVICE_REQUEST_PAGER_H_
#define GOOGLEAPIS_SERVICE_SERVICE_REQUEST_PAGER_H_

#include <memory>

#include "googleapis/base/macros.h"
#include "googleapis/client/service/client_service.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/stringpiece.h"
namespace googleapis {

namespace client {

class ClientServiceRequest;
class HttpRequest;
class HttpResponse;

/*
 * Base class for component that pages through REST results.
 * @ingroup ClientServiceLayer
 *
 * This class is abstract requiring ExecuteNextPage to be implemented
 * to instruct the pager how to specify (and determine) the next page.
 *
 * Normally the concrete ServiceRequestPager is used.
 */
class BaseServiceRequestPager {
 public:
  /*
   * Standard constructor.
   *
   * @param[in] request A reference to a prototype request used to fetch
   *                    the next page. Ownership is retained by the caller.
   */
  explicit BaseServiceRequestPager(ClientServiceRequest* request);

  /*
   * Standard destructor.
   */
  virtual ~BaseServiceRequestPager();

  /*
   * Returns the current page request.
   *
   * @return Ownership is retained by this instance.
   */
  ClientServiceRequest* request() { return request_; }

  /*
   * Returns the current page response.
   *
   * @return Ownership is retained by this instance.
   */
  HttpResponse* http_response()   { return request_->http_response(); }

  /*
   * Determine if this was the last known page.
   * @return true if we are done, false if not.
   *         We might still be done even if false is returned if the end
   *         was on a page boundary.
   */
  bool is_done() const { return done_; }

  /*
   * Fetch the next page.
   *
   * @return true if we could fetch another page, false if we are done.
   */
  bool NextPage();

  /*
   * Resets the pager back to the start.
   *
   * The next iteration may be different than the previous one depending
   * on the backend service.
   */
  void Reset();

 protected:
  /*
   * Does the actual messaging to the service to get the next page.
   *
   * @return ok or reason for failure.
   */
  virtual googleapis::util::Status ExecuteNextPage() = 0;

  /*
   * Returns the token parameter to use when fetching the next page.
   */
  const string& next_page_token() const { return next_page_token_; }

  /*
   * Sets the [scalar] request token identifying the next desired page.
   *
   * This is for service APIs that use scalar token values.
   *
   * @param[in] token Specifies the desired page.
   * @see Reset
   */
  void set_next_page_token(int64 token) {
    if (token == 0) {
      set_next_page_token("");
    } else {
      set_next_page_token(std::to_string(token));
    }
  }

  /*
   * Sets the [string] request token identifying the next desired page.
   *
   * This is for service APIs that use string token values.
   *
   * @param[in] token Specifies the desired page.
   * @see Reset
   */
  void set_next_page_token(const StringPiece& token) {
    done_ = token.empty();
    next_page_token_ = token.as_string();
  }

 private:
  ClientServiceRequest* request_;
  string next_page_token_;

  bool done_;

  DISALLOW_COPY_AND_ASSIGN(BaseServiceRequestPager);
};

/*
 * A pager over referenced REST APIs having a standard paging interface.
 * @ingroup ClientServiceLayer
 *
 * This template relies on the existence of REQUEST.set_page_token and
 * RESPONSE.get_next_page_token methods to control the page iteration.
 *
 * This class does not own the request or data objects. See the
 * EncapsulatedServiceRequestPager as a variant that adds memory management.
 *
 * @tparam REQUEST must be a subclass of ClientServiceRequest
 *                  and have a set_page_token method.
 * @tparam DATA must be a subclass of SerializableJson and have a
 *               get_next_page_token method.
 */
template<class REQUEST, class DATA>
class ServiceRequestPager : public BaseServiceRequestPager {
 public:
  /*
   * Standard constructor.
   *
   * @param[in] request The prototype request used to fetch pages.
   *                    The caller retains ownershp.
   * @param[in] page_data_storage Holds the underlying response data returned
   *            for the last requested page. The claeer retains ownership.
   */
  ServiceRequestPager(REQUEST* request, DATA* page_data_storage)
    : BaseServiceRequestPager(request), page_data_storage_(page_data_storage) {
  }

  /*
   * Standard destructor.
   */
  virtual ~ServiceRequestPager() {}

  /*
   * Returns the current page data.
   *
   * @return Ownership is retained by this instance as far at the caller is
   *         concerned.
   */
  DATA* data() { return page_data_storage_; }

  /*
   * Returns the current page request.
   *
   * @return Ownership is retained by this instance as far at the caller is
   *         concerned.
   */
  REQUEST* request() {
    return static_cast<REQUEST*>(BaseServiceRequestPager::request());
  }

  /*
   * Fetches the next page, if any.
   *
   * To distinguish the difference between a failure and no more pages,
   * check the http_response()->http_status().
   *
   * @return false on failure or when there are no more pages.
   */
  virtual googleapis::util::Status ExecuteNextPage() {
    // This method is called by the base class which guards with is_done
    // so we dont need to check here. But we'll do so anyway just to be
    // sure it didnt get here through some other route.
    if (is_done()) {
      return StatusOutOfRange("Finished Paging");
    }

    if (next_page_token().empty()) {
      request()->clear_page_token();
    } else {
      request()->set_page_token(next_page_token());
    }

    googleapis::util::Status status =
          request()->mutable_http_request()->PrepareToReuse();
    if (!status.ok()) return status;

    status = request()->ExecuteAndParseResponse(page_data_storage_);
    if (!status.ok()) return status;

    set_next_page_token(page_data_storage_->get_next_page_token());
    return status;
  }

 private:
  DATA* page_data_storage_;

  DISALLOW_COPY_AND_ASSIGN(ServiceRequestPager);
};

/*
 * A ServiceRequestPager that owns the request and data objects.
 * @ingroup ClientServiceLayer
 *
 * The request instance still needs to be injected since requests do not have
 * standard constructors.
 */
template<class REQUEST, class DATA>
class EncapsulatedServiceRequestPager
  : public ServiceRequestPager<REQUEST, DATA> {
 public:
  /*
   * Standard constructor
   *
   * @param[in] request The request prototype used to ask for pages.
   */
  explicit EncapsulatedServiceRequestPager(REQUEST* request)
      : ServiceRequestPager<REQUEST, DATA>(request, DATA::New()) {
    request_.reset(request);
    data_storage_.reset(ServiceRequestPager<REQUEST, DATA>::data());
  }

  /*
   * Standard destructor.
   */
  virtual ~EncapsulatedServiceRequestPager() {}

 private:
  std::unique_ptr<REQUEST> request_;    // access through base class
  std::unique_ptr<DATA> data_storage_;  // access through base class

  DISALLOW_COPY_AND_ASSIGN(EncapsulatedServiceRequestPager);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_SERVICE_SERVICE_REQUEST_PAGER_H_

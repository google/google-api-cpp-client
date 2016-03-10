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
 * @defgroup DataLayerJsonCpp Data Layer - JSON Support (using JsonCpp)
 *
 * The current implementation of JSON support uses the third-party JsonCpp
 * lirary (http://jsoncpp.sourceforge.net/) which has some issues that leak
 * into the interface. Therefore, I'm grouping these into a 'JsonCpp' module.
 *
 * The design of the JsonCpp sub-module makes pragmatic tradeoffs in order
 * to use this existing implementation. The main thing to be aware of is that
 * the signatures of the class methods in this  module often use call by
 * value-result semantics rather than call by reference as you would expect.
 * However, the internal implementation is often actually call by reference.
 * So the semantics (and performance) are similar to call-by reference but
 * the code feels like cal-by value. This is unfortunate and I hope to address
 * it in future releases.
 */

#ifndef GOOGLEAPIS_DATA_JSONCPP_DATA_H_
#define GOOGLEAPIS_DATA_JSONCPP_DATA_H_
#include <string>
using std::string;
#include <utility>
#include <vector>

#include "googleapis/client/data/serializable_json.h"
#include "googleapis/client/data/jsoncpp_data_helpers.h"
#include "googleapis/base/macros.h"
#include <json/json.h>
namespace googleapis {

namespace client {

class DataReader;

// Forward declaration
template<typename T> class JsonCppConstIndexIterator;
template<typename T> class JsonCppConstAssociativeIterator;

/*
 * Base class for data objects using JsonCpp for underlying state.
 * @ingroup DataLayerJsonCpp
 *
 * Class instances are either const or mutable depending on how they are
 * constructed. If constructed with a const reference, the instance will not
 * allow direct mutation. If constructed with a pointer, the instance will
 * allow mutation. It doesnt necessarily have to be like this, just seems like
 * a reasonable safety mechanism given the wrapper ipmlementation strategy.
 *
 * The objects are intended to be used in a call-by-value / value-result syntax
 * however the "values" are actually references so semantics are actually
 * call-by-reference. Admitedly this is unfortunate (i.e. weird) but it is to
 * allow object wrappers to be used in a complete and uniform way when
 * accessing and modifying complex composite objects through their various
 * parts. Since the wrapper object state is just a pointer (and a bool) the
 * copying syntax should not add much runtime overhead.
 *
 * Keeping all the state in the Json::Value object will likely add runtime
 * overhead however. Rather than attributes (of derived classes) being inlined
 * member variables, they will be [] operator lookups on string names. The
 * tradeoff here is for completeness, correctness, and internal implementation
 * convienence in the short term to get things up and running more quickly to
 * play with and understand strategic issues beyond performance.
 *
 * Keeping all the state together allows unknown attributes to be first-class
 * citizens along with known ones. Serialization, iteration, and so forth are
 * already handled (and in an open soure library) by jsoncpp. Presumably since
 * this is cheap to implement, it will be cheap to throw away and replace,
 * investing the effort for runtime efficiency (or finding another package)
 * at some future point down the road when it is a more pressing impedement.
 *
 * This class is not thread-safe.
 */
class JsonCppData : public SerializableJson {
 public:
  /*
   * Standard constructor for an immutable instances.
   *
   * @param[in] value Provides the underlying JsonCpp storage.
   *            The lifetime of 'value' must be retained over the lifetime of
   *            this instance, however the state can change internally.
   */
  explicit JsonCppData(const Json::Value& value);

  /*
   * Standard constructor for mutable instances.
   *
   * @param[in] value Provides the underlying JsonCpp storage.
   *                  Ownership of 'value' is retained by the caller and
   *                  must live beyond the lifecycle of this instance.
   */
  explicit JsonCppData(Json::Value* value);

  /*
   * Standard destructor.
   */
  virtual ~JsonCppData();

  /*
   * Restores the state of this object from the JSON in the given std::istream.
   *
   * @param[in] stream The stream to read from.
   * @return ok or explaination of failure.
   *
   * @see LoadFromJsonReader
   */
  virtual googleapis::util::Status LoadFromJsonStream(std::istream* stream);

  /*
   * Stores the state of this object as JSON in the given std::ostream.
   *
   * @param[in] stream The stream to write to.
   * @return ok or explaination of failure.
   *
   * @see MakeJsonReader
   */
  virtual googleapis::util::Status StoreToJsonStream(std::ostream* stream) const;

  /*
   * Clears this instance state to an empty object.
   */
  virtual void Clear();

  /*
   * Restores the state of this object from the JSON in the given reader.
   *
   * @param[in] reader The byte-sequence to read from.
   * @return ok or explaination of failure.
   */
  virtual googleapis::util::Status LoadFromJsonReader(DataReader* reader);

  /*
   * Serializes the object into a JSON byte stream.
   *
   * @return A standard reader interface around the JSON encoded object.
   */
  virtual DataReader* MakeJsonReader() const;

  /*
   * Copies state from another instance into this one.
   *
   * This will preserve the existing IsMutable-ness of this instance.
   *
   * @param[in] from The state to copy from.
   */
  void CopyFrom(const JsonCppData& from) {
    *MutableStorage() = from.Storage();
  }

  /*
   * Determines if this instance is mutable or not.
   *
   * Mutability is determined by which constructor was used.
   *
   * @return true if the instance is mutable.
   */
  bool IsMutable() const { return is_mutable_; }

  /*
   * Determines if the represented JSON value is NULL.
   *
   * @return true if the underlying Json::Value denotes a null value.
   */
  bool IsNull() const { return value_->isNull(); }

  /*
   * Ensures the instance is mutable or terminate if not.
   *
   * If the instance is not mutable this call will CHECK-fail and
   * terminate the program. It is only intended to make assertions
   * that indicate programming errors.
   */
  void CheckIsMutable() const;

  /*
   * Returns reference to the Json::Value instance used for storage.
   *
   * @return The instance bound in the constructor.
   */
  const Json::Value& Storage() const                { return *value_; }

  /*
   * Returns reference to the Json::Value instance used for storage.
   *
   * This will create a new subcomponent if one did not already exist.
   *
   * @param[in] key The JSON name of the desired subcomponent.
   *
   * @return The instance might be Json::Value::isNull but will be valid.
   */
  const Json::Value& Storage(const char* key) const { return (*value_)[key]; }

  /*
   * Returns a pointer to the storage bound in the constructor.
   *
   * This method will CHECK fail if the instance is immutable.
   */
  Json::Value* MutableStorage() {
    CheckIsMutable();
    return value_;
  }

  /*
   * Returns a pointer to the storage for the named subcomponent.
   *
   * This method will CHECK fail if the instance is immutable.
   */
  Json::Value* MutableStorage(const char* key) {
    CheckIsMutable();
    return &(*value_)[key];
  }

  /*
   * Determines if the object instances are equivalent.
   */
  bool operator==(const JsonCppData& other) const {
    return *value_ == *other.value_;
  }

 private:
  bool is_mutable_;
  Json::Value* value_;  // reference is not owned

  // Disallow assigning but preserve copying because we'll need to return
  // wrapped object instances from the APIs due to the call-by-value
  // constraints inherited from the underlying JsonCpp API.
  void operator=(const JsonCppData& obj);
};

/*
 * Base class template for a JsonCppData object that is an array.
 * @ingroup DataLyaerJsonCpp
 *
 * Arrays currently follow the JsonCpp implementation where they grow on
 * demand. Accessing an element wil create it if it did not already exist.
 *
 * This class is not thread-safe.
 *
 * @todo(user): 20120825
 * Probably change this so the arrays are fixed size unless you resize the
 * array or append/remove from it. That means get/set should rangecheck.
 */
template<typename T>
class JsonCppArray : public JsonCppData {
 public:
  typedef JsonCppConstIndexIterator<T> const_iterator;

  /*
   * Standard constructor for an immutable array.
   *
   * @param[in] value Provides the underlying storage for the array.
   */
  explicit JsonCppArray(const Json::Value& value)
    : JsonCppData(value) {
  }

  /*
   * Standard constructor for a mutable array.
   *
   * @param[in] value Provides the underlying storage for the array.
   */
  explicit JsonCppArray(Json::Value* value)
    : JsonCppData(value) {
  }

  /*
   * Determines if array is empty or not.
   */
  bool empty() const { return Storage().size() == 0; }

  /*
   * Determines the number of elements currently in the array.
   */
  int size() const { return Storage().size(); }

  /*
   * Gets the [immutable] underlying JsonCpp storage for a given array element.
   *
   * @see as_object
   * @see get
   */
  const Json::Value& as_value(int i) const { return Storage()[i]; }

  /*
   * Gets the [mutable] underlying JsonCpp storage for a given array element.
   *
   * This method will CHECK fail if the array is not mutable.
   *
   * @see as_mutable_object
   * @see mutable_get
   */
  Json::Value* as_mutable_value(int i)    { return &(*MutableStorage())[i]; }

  /*
   * Gets the [immutable] JsonCppData instance wrapping a given array element.
   *
   * @see as_value
   * @see get
   */
  const JsonCppData as_object(int i) const {
    return JsonCppData(as_value(i));
  }

  /*
   * Gets the [mutable] JsonCppData instance wrapping a given array element.
   *
   * @see as_mutable_value
   * @see mutable_get
   */
  JsonCppData as_mutable_object(int i) {
    return JsonCppData(as_mutable_value(i));
  }

  /*
   * Returns the starting point for iterating over the elements.
   *
   * Unfortunately this data model only permits const iterators
   * because of the call-by-value-result nature of the api. We cant modify
   * the elements unless we create a storage iterator that iterates over the
   * storage (which you can already do by saying MutableStorage()->begin/end()
   */
  const_iterator begin() const { return const_iterator(Storage().begin()); }

  /*
   * Returns the ending poitn when iterating over the elements
   */
  const_iterator end() const   { return const_iterator(Storage().end()); }

  /*
   * Changes the value for the given element.
   *
   * This method will CHECK-fail if the array is not mutable.
   *
   * @param[in] i The index to change will be added if it was not present.
   * @param[in] value The value to copy.
   */
  void set(int i, const T& value) {
    SetJsonValueFromCppValueHelper<T>(value, as_mutable_value(i));
  }

  /*
   * Returns the value for the given index.
   * @param[in] i The element index to get.
   */
  const T get(int i) const {
    return JsonValueToCppValueHelper<T>(as_value(i));
  }

  /*
   * Returns a mutable value for the given index.
   *
   * This method will CHECK-fail if the array is not mutable.
   *
   * @return The value as opposed to a pointer. See the class description
   *         for more information about the calling conventions.
   *         Although the value is not a pointer, it will be mutable if
   *         <code>T</code> denotes a <code>JsonCppObject</code>. For
   *         primitive types (and strings) this method will return copy
   *         that will not actually mutate the underlying element.
   */
  T mutable_get(int i) {
    return JsonValueToMutableCppValueHelper<T>(as_mutable_value(i));
  }

  /*
   * Provides syntactic sugar for get using [] operator overloading.
   */
  const T operator[](int i) const { return get(i); }

  /*
   * Provides syntactic sugar for mutable_get using [] operator overloading.
   */
  T operator[](int i) { return mutable_get(i); }


  /*
   * Imports data from a C++ vector into this instance.
   *
   * This method will clear the current data before importing the new.
   *
   * @param[in] array The vector to import from.
   */
  void Import(const std::vector<T>& array) {
    Clear();
    MutableStorage()->resize(array.size());
    for (int i = 0; i < array.size(); ++i) {
      set(i, array[i]);
    }
  }

  /*
   * Imports data from a C++ aray into this instance.
   *
   * This method will clear the current data before importing the new.
   *
   * @param[in] array The array to import from.
   * @param[in] len The number of elements int he array to import.
   */
  void Import(const T* array, int len) {
    Clear();
    for (int i = 0; i < len; ++i) {
      set(i, array[i]);
    }
  }

  /*
   * Exports data from this instance into a given C++ vector.
   *
   * This will clear the array before exporting.
   */
  void Export(std::vector<T>* array) const { Export(0, size(), array); }

  /*
   * Exports a range of data from this instance into a given C++ vector.
   *
   * This will clear the array before exporting.
   *
   * @param[in] offset The first index in this array to eport
   * @param[in] count The number of elements to export
   * @param[out] array The array to write to will be cleared before writing.
   *             The element at offset will be written into the 0'th element.
   * @return false if the existing bounds of this array are not consistent
   * with the arguments.
   */
  bool Export(int offset, int count, std::vector<T>* array) const {
    const Json::Value& json = Storage();
    if (offset + count > json.size()) return false;

    array->clear();
    array->resize(count);
    for (int i = 0; i < count; ++i) {
      (*array)[i] = get(offset + i);
    }
    return true;
  }

  /*
   * Exports a range of data from this instance into a given C++ array.
   *
   * @param[in] offset The first index in this array to eport
   * @param[in] count The number of elements to export
   * @param[out] array The caller should ensure that there are count elements
   *             allocated in the array.
   * @return false if the existing bounds of this array are not consistent
   * with the arguments.
   */
  bool Export(int offset, int count, T* array) const {
    const Json::Value& json = Storage();
    if (offset + count > json.size()) return false;

    for (int i = 0; i < count; ++i) {
      array[i] = get(offset + i);
    }
    return true;
  }
};


/*
 * Denotes a JsonCppData instance that is a dictionary.
 * @ingroup DataLayerJsonCpp
 *
 * The JsonCppData wraps a shared reference to jsoncpp values.
 * Multiple objects may reference the same values. A dictionary
 * does not have homogenous value types. Usually you want to use
 * an associative array instead.
 *
 * This class is not thread-safe.
 *
 * @see JsonCppAssociativeArray
 */
class JsonCppDictionary : public JsonCppData {
 public:
  explicit JsonCppDictionary(const Json::Value& value);
  explicit JsonCppDictionary(Json::Value* value);

  int size() const { return Storage().size(); }

  bool has(const char* key) const {
    return Storage().isMember(key);
  }

  void remove(const char* key) {
    MutableStorage()->removeMember(key);
  }

  // NOTE(user): 20120827
  // The data value here is mutable. Currently there isnt a way to have
  // just const values in a dictionary.
  void put_value(const char* key, const Json::Value& data) {
    *MutableStorage(key) = data;
  }

  void put_object(const char* key, const JsonCppData& value) {
    *MutableStorage(key) = value.Storage();
  }

  const Json::Value& as_value(const char* key) const { return Storage(key); }
  const JsonCppData as_object(const char* key) const {
    return JsonCppData(Storage(key));
  }

  JsonCppData mutable_object(const char* key) {
    return JsonCppData(MutableStorage(key));
  }
};

/*
 * Denotes an associative array from string to values of type <code>T</code>.
 * @ingroup DataLayerJsonCpp
 *
 * This class is not thread-safe.
 */
template<typename T>
class JsonCppAssociativeArray : public JsonCppDictionary {
 public:
  typedef JsonCppConstAssociativeIterator<T> const_iterator;

  explicit JsonCppAssociativeArray(const Json::Value& value)
    : JsonCppDictionary(value) {
  }

  explicit JsonCppAssociativeArray(Json::Value* value)
    : JsonCppDictionary(value) {
  }

  bool get(const char* key, T* value) const {
    CHECK_NOTNULL(value);
    Json::Value v = Storage().get(key, Json::Value::null);
    if (v == Json::Value::null) {
      ClearCppValueHelper(value);
      return false;
    }
    SetCppValueFromJsonValueHelper(v, value);
    return true;
  }

  void put(const char* key, const T& value) {
    SetJsonValueFromCppValueHelper(value, MutableStorage(key));
  }

  const_iterator begin() const { return const_iterator(Storage().begin()); }
  const_iterator end() const   { return const_iterator(Storage().end()); }
};


/*
 * Helper class for constructing new top-level JsonCppData instances.
 * @ingroup DataLayerJsonCpp
 *
 * The base JsonCppData class requires storage managed externally. When
 * creating new top-level instances you do not necessarily have a Json::Value
 * instance already nor do you want to worry about managing one.
 *
 * This capsule creates a specialization of any standard class derived from
 * JsonCppData that provides its own storage for a default constructor.It is
 * only inteded for actually instantiating new instances. When passing them
 * around, the BASE class should be used in the interfaces.
 */
template<typename BASE>
class JsonCppCapsule : public BASE {
 public:
  /*
   * Default constructor
   */
  JsonCppCapsule() : BASE(&storage_) {}

  /*
   * Standard destructor
   */
  virtual ~JsonCppCapsule() {}

 private:
  Json::Value storage_;
  DISALLOW_COPY_AND_ASSIGN(JsonCppCapsule);
};

/*
 * A helper class for index or key-based iterators.
 *
 * It just delegates to the base JsonCpp iterator.
 *
 * Note that because the data model is call-by-value-result theiterator can
 * only return copies and not references. Therefore we'll not be able to
 * modify values in place (i.e. this will not support a non-const iterator)
 */
template<typename DATA, typename ITERATOR>
class JsonCppAbstractIterator {
 public:
  typedef typename ITERATOR::size_t size_t;
  typedef JsonCppAbstractIterator<DATA, ITERATOR> SelfType;

  explicit JsonCppAbstractIterator(const SelfType& copy) : it_(copy.it_) {}
  ~JsonCppAbstractIterator() {}

  SelfType& operator=(const JsonCppAbstractIterator& copy) {
    it_ = copy.it_;
    return *this;
  }
  SelfType& operator++() {
    ++it_;
    return *this;
  }
  bool operator==(const SelfType& it) const {
    return it_ == it.it_;
  }
  bool operator!=(const SelfType& it) const {
    return it_ != it.it_;
  }

 protected:
  explicit JsonCppAbstractIterator(const ITERATOR& it) : it_(it) {}
  ITERATOR it_;
};

/*
 * Iterator for index-based JsonCppDataArray.
 * @ingroup DataLayerJsonCpp
 *
 * Provides access to the index in the array which might be helpful for
 * making modifications directly into the array if needed,
 * since we cannot support non-const iterators.
 */
template<typename T>
class JsonCppConstIndexIterator
    : public JsonCppAbstractIterator<T, Json::Value::const_iterator> {
 public:
  typedef JsonCppConstIndexIterator<T> SelfType;
  typedef T reference;  // Note we're using copy-by-value references

  JsonCppConstIndexIterator(const JsonCppConstIndexIterator<T>& copy)
      : JsonCppAbstractIterator<T, Json::Value::const_iterator>(copy) {}
  ~JsonCppConstIndexIterator() {}

  reference operator*() const {
    return JsonValueToCppValueHelper<T>(
        *JsonCppAbstractIterator<T, Json::Value::const_iterator>::it_);
  }
  int index() const {
    return JsonCppAbstractIterator<
      T, Json::Value::const_iterator>::it_.index(); }

 private:
  friend class JsonCppArray<T>;
  explicit JsonCppConstIndexIterator(const Json::Value::const_iterator& it)
      : JsonCppAbstractIterator<T, Json::Value::const_iterator>(it) {}
};

/*
 * Iterator for JsonCppDataAssociativeArray.
 * @ingroup DataLayerJsonCpp
 */
template<typename T>
class JsonCppConstAssociativeIterator
    : public JsonCppAbstractIterator<std::pair<const string, T>,
                                     Json::Value::const_iterator> {
 public:
  typedef JsonCppConstAssociativeIterator<T> SelfType;
  typedef std::pair<const string, T> reference;
  typedef JsonCppAbstractIterator<std::pair<const string, T>,
                                  Json::Value::const_iterator> SuperClass;

  JsonCppConstAssociativeIterator(
      const JsonCppConstAssociativeIterator<T>& copy) : SuperClass(copy) {
  }
  ~JsonCppConstAssociativeIterator() {}

  // If you only care about one part of the pair, consider key() or value().
  reference operator*() const { return std::make_pair(key(), value()); }

  // This returns a copy of the string, not a reference to the string.
  // Unfortunately this is an implementation detail caused by limitations
  // of the underlying data model.
  const string key() const {
    // The iterator retuns a local object so the CString/string values
    // disappear ones it leaves scope, forcing us to have to return a copy.
    const Json::Value& temp = SuperClass::it_.key();
    return temp.asString();
  }

  T value() const { return JsonValueToCppValueHelper<T>(*SuperClass::it_); }

 private:
  friend class JsonCppAssociativeArray<T>;
  explicit JsonCppConstAssociativeIterator(
      const Json::Value::const_iterator& it) : SuperClass(it) {
  }
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_DATA_JSONCPP_DATA_H_

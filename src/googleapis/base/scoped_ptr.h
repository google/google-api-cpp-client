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
// Copyright 2007 Google Inc.
//
//
#ifndef BASE_SCOPED_PTR_H__
#define BASE_SCOPED_PTR_H__

//  This implementation was designed to match the then-anticipated TR2
//  implementation of the scoped_ptr class, and its closely-related brethren,
//  scoped_array and scoped_ptr_malloc. The anticipated  standardization of
//  scoped_ptr has been superseded by unique_ptr, and the APIs in this file are
//  being revised to be a subset of unique_ptr, as a step towards replacing them
//
//  drove this file.

#include <assert.h>
#include <stdlib.h>
#include <cstddef>

#include "googleapis/base/port.h"
#include "googleapis/base/scoped_ptr_internals.h"
#include "googleapis/base/type_traits.h"
namespace googleapis {

#ifdef OS_EMBEDDED_QNX
// NOTE(user):
// The C++ standard says that <stdlib.h> declares both ::foo and std::foo
// But this isn't done in QNX version 6.3.2 200709062316.
using std::free;
using std::malloc;
using std::realloc;
#endif

template <class C, class D> class scoped_ptr;
template <class C, class Free> class scoped_ptr_malloc;
template <class C> class scoped_array;

namespace base {

// Function object which deletes its parameter, which must be a pointer.
// If C is an array type, invokes 'delete[]' on the parameter; otherwise,
// invokes 'delete'. The default deleter for scoped_ptr<T>.
template <class C>
struct DefaultDeleter {
  inline void operator()(C* ptr) const {
    enum { type_must_be_complete = sizeof(C) };
    delete ptr;
  }
};

// Specialization of DefaultDeleter for array types.
template <class C>
struct DefaultDeleter<C[]> {
  inline void operator()(C* ptr) const {
    enum { type_must_be_complete = sizeof(C) };
    delete[] ptr;
  }
};

// Function object which invokes 'free' on its parameter, which must be
// a pointer. Can be used to store malloc-allocated pointers in scoped_ptr:
//
// scoped_ptr<int, base::FreeDeleter> foo_ptr(
//     static_cast<int*>(malloc(sizeof(int))));
struct FreeDeleter {
  inline void operator()(void* ptr) const {
    free(ptr);
  }
};

}  // namespace base

// A scoped_ptr<T> is like a T*, except that the destructor of scoped_ptr<T>
// automatically deletes the pointer it holds (if any).
// That is, scoped_ptr<T> owns the T object that it points to.
// Like a T*, a scoped_ptr<T> may hold either NULL or a pointer to a T object.
// Also like T*, scoped_ptr<T> is thread-compatible, and once you
// dereference it, you get the threadsafety guarantees of T.
//
// By default, scoped_ptr deletes its stored pointer using 'delete', but
// this behavior can be customized via the second template parameter:
// A scoped_ptr<T,D> invokes D::operator() on the stored pointer when the
// scoped_ptr is destroyed. For example, a scoped_ptr<T, base::FreeDeleter>
// can be used to store pointers to memory allocated with malloc().
// Note that scoped_ptr will not invoke D on a NULL pointer.
//
// If D is an empty class (i.e. has no non-static data members), then
// on most compilers, scoped_ptr is the same size as a plain pointer.
// Otherwise, it will be at least as large as sizeof(C*) + sizeof(D).
template <class C, class D = base::DefaultDeleter<C> >
class scoped_ptr {
 public:

  // The element type
  typedef C element_type;
  typedef D deleter_type;

  // Constructor.  Defaults to initializing with NULL.
  // There is no way to create an uninitialized scoped_ptr.
  explicit scoped_ptr(C* p = NULL) : impl_(p) { }

#ifdef LANG_CXX11
  // Constructor.  Initializes with NULL.  There is no way to create an
  // uninitialized scoped_ptr.
  explicit scoped_ptr(std::nullptr_t) : impl_(NULL) {}
#endif

  // Reset.  Deletes the current owned object, if any.
  // Then takes ownership of a new object, if given.
  // this->reset(this->get()) works, but this behavior is DEPRECATED, and
  void reset(C* p = NULL) {
    impl_.reset(p);
  }

  // Accessors to get the owned object.
  // operator* and operator-> will assert() if there is no current object.
  C& operator*() const {
    assert(impl_.get() != NULL);
    return *impl_.get();
  }
  C* operator->() const  {
    assert(impl_.get() != NULL);
    return impl_.get();
  }
  C* get() const { return impl_.get(); }

  // Comparison operators.
  // These return whether a scoped_ptr and a raw pointer refer to
  // the same object, not just to two different but equal objects.
  bool operator==(const C* p) const { return impl_.get() == p; }
  bool operator!=(const C* p) const { return impl_.get() != p; }

  // Swap two scoped pointers.
  void swap(scoped_ptr& p2) {
    impl_.swap(p2.impl_);
  }

  // Release a pointer.
  // The return value is the current pointer held by this object.
  // If this object holds a NULL pointer, the return value is NULL.
  // After this operation, this object will hold a NULL pointer,
  // and will not own the object any more.
  //
  // CAVEAT: It is incorrect to use and release a pointer in one statement, eg.
  //   objects[ptr->name()] = ptr.release();
  // as it is undefined whether the .release() or ->name() runs first.
  C* release() {
    return impl_.release();
  }

 private:
  base::internal::scoped_ptr_impl<C,D> impl_;

  // Forbid construction with explicit NULL or 0 (either use the no-argument
  // constructor or use nullptr). This is because in C++11 it is ambiguous to
  // construct unique_ptr with NULL or 0, and we're in the process of
  // transitioning from scoped_ptr to unique_ptr. This struct and constructor
  // make it ambiguous to construct a scoped_ptr with NULL in C++98. A struct
  // with an implicit constructor which takes a pointer is used instead of just
  // a pointer so that it is not ambiguous to construct a scoped_ptr with the
  // result of util::gtl::NewContainer().
  struct do_not_construct_scoped_ptr_with_explicit_NULL_or_0 {
    do_not_construct_scoped_ptr_with_explicit_NULL_or_0(
        do_not_construct_scoped_ptr_with_explicit_NULL_or_0*) {}
  };
  explicit scoped_ptr(do_not_construct_scoped_ptr_with_explicit_NULL_or_0)
      : impl_(NULL) {}

  // Forbid comparison of scoped_ptr types.  If C2 != C, it totally doesn't
  // make sense, and if C2 == C, it still doesn't make sense because you should
  // never have the same object owned by two different scoped_ptrs.
  template <class C2, class D2> bool operator==(
      scoped_ptr<C2, D2> const& p2) const;
  template <class C2, class D2> bool operator!=(
      scoped_ptr<C2, D2> const& p2) const;

  // Disallow copy and assignment.
  scoped_ptr(const scoped_ptr&);
  void operator=(const scoped_ptr&);
};

// Free functions
template <class C, class D>
inline void swap(scoped_ptr<C, D>& p1, scoped_ptr<C, D>& p2) {
  p1.swap(p2);
}

template <class C, class D>
inline bool operator==(const C* p1, const scoped_ptr<C, D>& p2) {
  return p1 == p2.get();
}

template <class C, class D>
inline bool operator==(const C* p1, const scoped_ptr<const C, D>& p2) {
  return p1 == p2.get();
}

template <class C, class D>
inline bool operator!=(const C* p1, const scoped_ptr<C, D>& p2) {
  return p1 != p2.get();
}

template <class C, class D>
inline bool operator!=(const C* p1, const scoped_ptr<const C, D>& p2) {
  return p1 != p2.get();
}

// Specialization of scoped_ptr used for holding arrays:
//
// scoped_ptr<int[]> array(new int[10]);
//
// This specialization provides operator[] instead of operator* and
// operator->, and by default it deletes the stored array using 'delete[]'
// rather than 'delete'. It also provides some additional type-safety:
// the pointer used to initialize a scoped_ptr<T[]> must have type T* and
// not, for example, some class derived from T; this helps avoid
// accessing an array through a pointer whose dynamic type is different
// from its static type, which can lead to undefined behavior.
template <class C, class D>
class scoped_ptr<C[], D> {
 public:

  // The element type
  typedef C element_type;
  typedef D deleter_type;

  // Default constructor. Initializes stored pointer to NULL.
  // There is no way to create an uninitialized scoped_ptr.
  scoped_ptr() : impl_(NULL) { }

  // Constructor. Stores the given array. Note that the type of 'array' must
  // be exactly C* (possibly with top-level 'const' or 'volatile' removed).
  // In particular:
  // - It cannot be a pointer to a type derived from C, because it is
  //   inherently unsafe to access an array through a pointer whose dynamic
  //   type does not match its static type.
  // - It cannot be NULL, because NULL is an integral expression, not a C*.
  //   Use the no-argument version instead of explicitly passing NULL.
  // - Similarly, it cannot be nullptr, because nullptr has type nullptr_t.
  // - It cannot be const-qualified differently from C. There is no principled
  //   reason for this; it's an artifact of how the above restrictions are
  //   implemented. As a convenience exception, if C has a top-level 'const' or
  //   'volatile' qualifier, we permit the argument to remove that qualifier,
  //   so you can do things like
  //
  //   scoped_ptr<const Foo[]> arr(new Foo[10]);
  //
  //   If this exception does not cover your case, you can work around it
  //   with implicit_cast (from base/casts.h):
  //
  //   const Foo** i;
  //   ...
  //   scoped_ptr<const Foo*> arr(implicit_cast<const Foo*>(new Foo*[10]));
  template <typename T>
  explicit scoped_ptr(T* array) : impl_(array) {
    static_assert((base::is_same<typename base::remove_cv<C>::type,
                                 typename base::remove_cv<T>::type>::value),
                  "scoped_ptr<T[]> initialized from type other than T*.");
  }

  // Reset.  Deletes the current owned object, if any, then takes ownership of
  // the new object, if given. Note that the type of 'array' must exactly
  // match C; see the comments on the constructor for details.
  // this->reset(this->get()) works, but this behavior is DEPRECATED, and
  template <typename T>
  void reset(T* array) {
    static_assert((base::is_same<typename base::remove_cv<C>::type,
                                 typename base::remove_cv<T>::type>::value),
                  "scoped_ptr<T[]> initialized from type other than T*.");
    impl_.reset(array);
  }

  void reset() {
    impl_.reset(NULL);
  }

  // Array indexing operation. Returns the specified element of the underlying
  // array. Will assert if no array is currently stored.
  C& operator[] (size_t i) const {
    assert(impl_.get() != NULL);
    return impl_.get()[i];
  }

  C* get() const { return impl_.get(); }

  // Comparison operators.
  // These return whether a scoped_ptr and a raw pointer refer to
  // the same object, not just to two different but equal objects.
  bool operator==(const C* array) const { return impl_.get() == array; }
  bool operator!=(const C* array) const { return impl_.get() != array; }

  // Swap two scoped pointers.
  void swap(scoped_ptr& p2) {
    impl_.swap(p2.impl_);
  }

  // Release a pointer.
  // The return value is a pointer to the array currently held by this object.
  // If this object holds a NULL pointer, the return value is NULL.
  // After this operation, this object will hold a NULL pointer,
  // and will not own the array any more.
  //
  // CAVEAT: It is incorrect to use and release a pointer in one statement, eg.
  //   objects[ptr->name()] = ptr.release();
  // as it is undefined whether the .release() or ->name() runs first.
  C* release() {
    return impl_.release();
  }

 private:
  // Force C to be a complete type.
  enum { type_must_be_complete = sizeof(C) };

  base::internal::scoped_ptr_impl<C,D> impl_;

  // Forbid comparison of scoped_ptr types.  If C2 != C, it totally doesn't
  // make sense, and if C2 == C, it still doesn't make sense because you should
  // never have the same object owned by two different scoped_ptrs.
  template <class C2, class D2> bool operator==(
      scoped_ptr<C2, D2> const& p2) const;
  template <class C2, class D2> bool operator!=(
      scoped_ptr<C2, D2> const& p2) const;

  // Disallow copy and assignment.
  scoped_ptr(const scoped_ptr&);
  void operator=(const scoped_ptr&);
};

template <class C, class D>
inline bool operator==(const C* p1, const scoped_ptr<C[], D>& p2) {
  return p1 == p2.get();
}

template <class C, class D>
inline bool operator==(const C* p1, const scoped_ptr<const C[], D>& p2) {
  return p1 == p2.get();
}

template <class C, class D>
inline bool operator!=(const C* p1, const scoped_ptr<C[], D>& p2) {
  return p1 != p2.get();
}

template <class C, class D>
inline bool operator!=(const C* p1, const scoped_ptr<const C[], D>& p2) {
  return p1 != p2.get();
}

// scoped_array<C> is like scoped_ptr<C>, except that the caller must allocate
// with new [] and the destructor deletes objects with delete [].
//
// As with scoped_ptr<C>, a scoped_array<C> either points to an object
// or is NULL.  A scoped_array<C> owns the object that it points to.
// scoped_array<T> is thread-compatible, and once you index into it,
// the returned objects have only the threadsafety guarantees of T.
//
// Size: sizeof(scoped_array<C>) == sizeof(C*)
//
// aware of the following differences:
// - The pointers passed into scoped_ptr<C[]> must have type C* exactly.
//   See the comments on scoped_ptr<C[]>'s constructor for details.
// - The type C must be complete (i.e. it must have a full definition, not
//   just a forward declaration) at the point where the scoped_ptr<C[]> is
//   declared.
template <class C>
class scoped_array {
 public:

  // The element type
  typedef C element_type;

  // Default constructor. Initializes stored array to NULL.
  // There is no way to create an uninitialized scoped_array.
  scoped_array() : array_(NULL) {}

#ifdef LANG_CXX11
  // Constructor.  Initializes with NULL.  There is no way to create an
  // uninitialized scoped_array.
  explicit scoped_array(std::nullptr_t) : array_(NULL) {}
#endif

  // There is no way to create an uninitialized scoped_array.
  // The input parameter must be allocated with new [].
  template <typename T>
  explicit scoped_array(T* p) : array_(p) {
    // Disable potentially unsafe conversions of input pointer, as in
    // scoped_ptr<T[]>
    static_assert((base::is_same<typename base::remove_cv<C>::type,
                                 typename base::remove_cv<T>::type>::value),
                  "scoped_array<T> initialized from type other than T*.");
  }

  // Destructor.  If there is a C object, delete it.
  // We don't need to test ptr_ == NULL because C++ does that for us.
  ~scoped_array() {
    enum { type_must_be_complete = sizeof(C) };
    delete[] array_;
  }

  // Reset.  Deletes the current owned object, if any.
  // Then takes ownership of a new object, if given.
  // this->reset(this->get()) works.
  template <typename T>
  void reset(T* p) {
    // Disable potentially unsafe conversions of input pointer, as in
    // scoped_ptr<T[]>
    static_assert((base::is_same<typename base::remove_cv<C>::type,
                                 typename base::remove_cv<T>::type>::value),
                  "scoped_array<T> initialized from type other than T*.");
    if (p != array_) {
      enum { type_must_be_complete = sizeof(C) };
      delete[] array_;
      array_ = p;
    }
  }

  void reset() {
    reset<C>(NULL);
  }

  // Get one element of the current object.
  // Will assert() if there is no current object, or index i is negative.
  C& operator[](std::ptrdiff_t i) const {
    assert(i >= 0);
    assert(array_ != NULL);
    return array_[i];
  }

  // Get a pointer to the zeroth element of the current object.
  // If there is no current object, return NULL.
  C* get() const {
    return array_;
  }

  // Comparison operators.
  // These return whether a scoped_array and a raw pointer refer to
  // the same array, not just to two different but equal arrays.
  bool operator==(const C* p) const { return array_ == p; }
  bool operator!=(const C* p) const { return array_ != p; }

  // Swap two scoped arrays.
  void swap(scoped_array& p2) {
    C* tmp = array_;
    array_ = p2.array_;
    p2.array_ = tmp;
  }

  // Release an array.
  // The return value is the current pointer held by this object.
  // If this object holds a NULL pointer, the return value is NULL.
  // After this operation, this object will hold a NULL pointer,
  // and will not own the object any more.
  C* release() {
    C* retVal = array_;
    array_ = NULL;
    return retVal;
  }

 private:
  C* array_;

  // Forbid construction with explicit NULL or 0 (either use the no-argument
  // constructor or use nullptr). This is because in C++11 it is ambiguous to
  // construct unique_ptr with NULL or 0, and we're in the process of
  // transitioning from scoped_array to unique_ptr. This struct and constructor
  // make it ambiguous to construct a scoped_array with NULL in C++98.
  struct do_not_construct_scoped_array_with_explicit_NULL_or_0 {};
  explicit scoped_array(do_not_construct_scoped_array_with_explicit_NULL_or_0*)
      : array_(NULL) {}

  // Forbid comparison of different scoped_array types.
  template <class C2> bool operator==(scoped_array<C2> const& p2) const;
  template <class C2> bool operator!=(scoped_array<C2> const& p2) const;

  // Disallow copy and assignment.
  scoped_array(const scoped_array&);
  void operator=(const scoped_array&);
};

// Free functions
template <class C>
inline void swap(scoped_array<C>& p1, scoped_array<C>& p2) {
  p1.swap(p2);
}

template <class C>
inline bool operator==(const C* p1, const scoped_array<C>& p2) {
  return p1 == p2.get();
}

template <class C>
inline bool operator==(const C* p1, const scoped_array<const C>& p2) {
  return p1 == p2.get();
}

template <class C>
inline bool operator!=(const C* p1, const scoped_array<C>& p2) {
  return p1 != p2.get();
}

template <class C>
inline bool operator!=(const C* p1, const scoped_array<const C>& p2) {
  return p1 != p2.get();
}

// scoped_ptr_malloc<> is similar to scoped_ptr<>, but it accepts a
// second template argument, the functor used to free the object.
//
template<class C, class D = base::FreeDeleter>
class scoped_ptr_malloc {
 public:
  // The element type
  typedef C element_type;

  // Construction with no arguments sets ptr_ to NULL.
  // There is no way to create an uninitialized scoped_ptr.
  // The input parameter must be allocated with an allocator that matches the
  // Free functor.  For the default Free functor, this is malloc, calloc, or
  // realloc.
  scoped_ptr_malloc(): impl_(NULL) { }

  // Construct with a C*, and provides an error with a D*.
  template<class must_be_C>
  explicit scoped_ptr_malloc(must_be_C* p): impl_(p) { }

  // Destructor.  If there is a C object, call the Free functor.
  ~scoped_ptr_malloc() {
    reset();
  }

  // Reset.  Calls the Free functor on the current owned object, if any.
  // Then takes ownership of a new object, if given.
  // this->reset(this->get()) works.
  void reset(C* p = NULL) {
    impl_.reset(p);
  }

  // Get the current object.
  // operator* and operator-> will cause an assert() failure if there is
  // no current object.
  C& operator*() const {
    assert(impl_.get() != NULL);
    return *impl_.get();
  }

  C* operator->() const {
    assert(impl_.get() != NULL);
    return impl_.get();
  }

  C* get() const { return impl_.get(); }

  // Comparison operators.
  // These return whether a scoped_ptr_malloc and a plain pointer refer
  // to the same object, not just to two different but equal objects.
  // For compatibility with the boost-derived implementation, these
  // take non-const arguments.
  bool operator==(C* p) const { return impl_.get() == p; }

  bool operator!=(C* p) const { return impl_.get() != p; }

  // Swap two scoped pointers.
  void swap(scoped_ptr_malloc & b) {
    impl_.swap(b.impl_);
  }

  // Release a pointer.
  // The return value is the current pointer held by this object.
  // If this object holds a NULL pointer, the return value is NULL.
  // After this operation, this object will hold a NULL pointer,
  // and will not own the object any more.
  C* release() {
    return impl_.release();
  }

 private:
  base::internal::scoped_ptr_impl<C, D> impl_;

  // no reason to use these: each scoped_ptr_malloc should have its own object
  template <class C2, class GP>
  bool operator==(scoped_ptr_malloc<C2, GP> const& p) const;
  template <class C2, class GP>
  bool operator!=(scoped_ptr_malloc<C2, GP> const& p) const;

  // Disallow copy and assignment.
  scoped_ptr_malloc(const scoped_ptr_malloc&);
  void operator=(const scoped_ptr_malloc&);
};

template<class C, class D> inline
void swap(scoped_ptr_malloc<C, D>& a, scoped_ptr_malloc<C, D>& b) {
  a.swap(b);
}

template<class C, class D> inline
bool operator==(C* p, const scoped_ptr_malloc<C, D>& b) {
  return p == b.get();
}

template<class C, class D> inline
bool operator!=(C* p, const scoped_ptr_malloc<C, D>& b) {
  return p != b.get();
}

} // namespace googleapis
#endif  // BASE_SCOPED_PTR_H__

// RUN: %clang_cc1 -fcxx-exceptions -fsyntax-only -Wlifetime -Wlifetime-debug -verify %s
#include "../Analysis/Inputs/system-header-simulator-cxx.h"
template <typename T>
bool __lifetime_pset(const T &) { return true; }

template <typename T>
bool __lifetime_pset_ref(const T &) { return true; }

template <typename T>
void __lifetime_type_category() {}

template <typename T>
bool __lifetime_contracts(const T &) { return true; }

struct [[gsl::Pointer]] my_pointer {
  my_pointer();
  my_pointer &operator=(const my_pointer &);
  int &operator*();
};

namespace gsl {
template <typename T>
using nullable = T;

template <typename T>
struct not_null {
  constexpr operator T() const { return T(); }
  constexpr T operator->() const { return T(); }
};
} // namespace gsl

namespace gsl {
// These classes are marked Owner so the lifetime analysis will not look into
// the bodies of these methods. Maybe we need an annotation that will not mark
// classes owner but still tell the analyzer to skip these classes?
// Or whouls we use gsl::suppress for that (even on classes)?
struct [[gsl::Owner]] null_t {
  int operator*() const;
  template <typename T>
  operator T() const { return T(nullptr); }
} Null;
struct [[gsl::Owner]] static_t {
  int operator*() const;
  template <typename T>
  operator T() const { return (T)(void *)this; }
} Static;
struct [[gsl::Owner]] invalid_t {
  int operator*() const;
  template <typename T>
  operator T() const { return (T)(void *)this; }
} Invalid;
struct [[gsl::Owner]] return_t {
  int operator*() const;
  template <typename T>
  operator T() const { return (T)(void *)this; }
} Return;

template <typename T>
struct CheckSingle {
  CheckSingle(const T &t) : data(t) {}
  const T &data;
  template <typename S>
  operator CheckSingle<S>() { return CheckSingle<S>(S(data)); }
};

template <typename T>
struct CheckVariadic {
  CheckVariadic(std::initializer_list<T> ptrs) : ptrs(ptrs) {}
  // We expect this to live only for a single expr.
  std::initializer_list<T> ptrs;
};

template <typename T, typename S>
bool operator==(CheckSingle<T> lhs, CheckSingle<S> rhs) {
  // TODO: these cannot be checked right?
  if ((void *)lhs.data == (void *)&Static || (void *)lhs.data == (void *)&Invalid || (void *)lhs.data == (void *)&Return ||
      (void *)rhs.data == (void *)&Static || (void *)rhs.data == (void *)&Invalid || (void *)rhs.data == (void *)&Return)
    return true;
  // TODO: maybe make this a customization point?
  //       user defined gsl::Pointers might not have operator==.
  //       Alternative: fall back to &deref(UserPtr).
  //       Also for an array and a Ptr pointing into the array
  //       this should yield true. This is not the case now.
  //       Also, checking if two iterators are pointing to the same
  //       object is not possible.
  return lhs.data == rhs.data;
}

template <typename T, typename S>
bool operator==(const CheckVariadic<T> &lhs, CheckSingle<S> rhs) {
  return std::any_of(lhs.ptrs.begin(), lhs.ptrs.end(), [&rhs](const T &ptr) {
    return CheckSingle<T>(ptr) == rhs;
  });
}

template <typename T, typename S>
bool operator==(const CheckSingle<T> &lhs, CheckVariadic<S> rhs) {
  return rhs == lhs;
}

template <typename T>
CheckSingle<T> pset(const T &t) {
  return t;
}

template <typename T>
CheckVariadic<T> pset(std::initializer_list<T> ptrs) {
  return CheckVariadic<T>(ptrs);
}

// TODO: support deref
// TODO: support member selection (change in Attr representation)
// TODO: handle references (auto deref and address of?)
} // namespace gsl

using namespace gsl;

void basic(int *a, int *b) [[gsl::pre(pset(b) == pset(a))]] {
  __lifetime_pset(b); // expected-warning {{((*a), (null))}}
}

void specials(int *a, int *b, int *c)
    [[gsl::pre(pset(a) == pset(Null))]]
    [[gsl::pre(pset(b) == pset(Static))]]
    [[gsl::pre(pset(c) == pset(Invalid))]] {
  __lifetime_pset(a); // expected-warning {{((null))}}
  __lifetime_pset(b); // expected-warning {{((static))}}
  __lifetime_pset(c); // expected-warning {{((invalid))}}
}

void variadic(int *a, int *b, int *c)
    [[gsl::pre(pset(b) == pset({a, c}))]] {
  __lifetime_pset(b); // expected-warning {{((*a), (*c), (null))}}
}

void variadic_swapped(int *a, int *b, int *c)
    [[gsl::pre(pset({a, c}) == pset(b))]] {
  __lifetime_pset(b); // expected-warning {{((*a), (*c), (null))}}
}

/* For std::initializer_list conversions will not work.
   Maybe use type and no conversions required?
void variadic_special(int *a, int *b, int *c)
    [[gsl::pre(pset(b) == pset({a, Null}))]] {
  __lifetime_pset(b); // TODOexpected-warning {{((*a), (null))}}
}
*/

/* Will not compile! What should this mean for the state of the analysis?
   The source of the problem is that the following constraint can 
   be satisfied multiple ways:
   pset(a, b) == pset(c, d)
   Possible solution #1:
    pset(a) == {*a}
    pset(b) == {*a, b}
    pset(c) == {*a}
    pset(d) == {*b}
   Possible solution #2:
    pset(a) == {*a}
    pset(b) == {*a}
    pset(c) == {*a}
    pset(d) == {*a}
   And so on...
void double_variadic(int *a, int *b, int *c)
    [[gsl::pre(pset({a, b}) == pset({b, c}))]] {
}
*/

void multiple_annotations(int *a, int *b, int *c)
    [[gsl::pre(pset(b) == pset(a))]]
    [[gsl::pre(pset(c) == pset(a))]] {
  __lifetime_pset(b); // expected-warning {{((*a), (null))}}
  __lifetime_pset(c); // expected-warning {{((*a), (null))}}
}

void multiple_annotations_chained(int *a, int *b, int *c)
    [[gsl::pre(pset(b) == pset(a))]]
    [[gsl::pre(pset(c) == pset(b))]] {
  __lifetime_pset(b); // expected-warning {{((*a), (null))}}
  __lifetime_pset(c); // expected-warning {{((*a), (null))}}
}

void annotate_forward_decl(int *a, int *b)
    [[gsl::pre(pset(b) == pset(a))]];

void annotate_forward_decl(int *c, int *d) {
  __lifetime_pset(d); // expected-warning {{((*c), (null))}}
}

// Repeated annotations on redeclarations are not checked as
// they will automatically be checked with contracts.

namespace dump_contracts {
// Need to have bodies to fill the lifetime attr.
void p(int *a) {}
void p2(int *a, int &b) {}
void p3(int *a, int *&b) {}
void parameter_psets(int value,
                     char *const *in,
                     int &int_ref,
                     const int &const_int_ref,
                     std::unique_ptr<int> owner_by_value,
                     const std::unique_ptr<int> &owner_const_ref,
                     std::unique_ptr<int> &owner_ref,
                     my_pointer ptr_by_value,
                     const my_pointer &ptr_const_ref,
                     my_pointer &ptr_ref,
                     my_pointer *ptr_ptr,
                     const my_pointer *ptr_const_ptr) {}
void p4(int *a, int *b, int *&c)
    [[gsl::pre(pset(b) == pset(a))]] {}
int *p5(int *a, int *b) { return a; }
int *p6(int *a, int *b)
    [[gsl::post(pset(Return) == pset(a))]] { return a; }
struct S{
  int *f(int * a, int *b, int *&c) { return a; }
  S *g(int * a, int *b, int *&c) { return this; }
};
// TODO: contracts for function pointers?

void f() {
  __lifetime_contracts(p);
  // expected-warning@-1 {{pset(Pre(a)) = ((*a), (null))}}
  __lifetime_contracts(p2);
  // expected-warning@-1 {{pset(Pre(a)) = ((*a), (null))}}
  // expected-warning@-2 {{pset(Pre(b)) = ((*b))}}
  __lifetime_contracts(p3);
  // expected-warning@-1 {{pset(Pre(a)) = ((*a), (null))}}
  // expected-warning@-2 {{pset(Pre(b)) = ((*b))}}
  // expected-warning@-3 {{pset(Pre(*b)) = ((invalid))}}
  // expected-warning@-4 {{pset(Post(*b)) = ((*a), (null))}}
  __lifetime_contracts(parameter_psets);
  // expected-warning@-1 {{pset(Pre(owner_by_value)) = ((*owner_by_value))}}
  // expected-warning@-2 {{pset(Pre(owner_ref)) = ((*owner_ref))}}
  // expected-warning@-3 {{pset(Pre(*owner_ref)) = ((*(*owner_ref)))}}
  // expected-warning@-4 {{pset(Pre(ptr_ref)) = ((*ptr_ref))}}
  // expected-warning@-5 {{pset(Pre(*ptr_ref)) = ((invalid))}}
  // expected-warning@-6 {{pset(Pre(ptr_const_ref)) = ((*ptr_const_ref))}}
  // expected-warning@-7 {{pset(Pre(*ptr_const_ref)) = ((*(*ptr_const_ref)), (null))}}
  // expected-warning@-8 {{pset(Pre(ptr_const_ptr)) = ((*ptr_const_ptr), (null))}}
  // expected-warning@-9 {{pset(Pre(*ptr_const_ptr)) = ((*(*ptr_const_ptr)), (null))}}
  // expected-warning@-10 {{pset(Pre(in)) = ((*in), (null))}}
  // expected-warning@-11 {{pset(Pre(*in)) = ((*(*in)), (null))}}
  // expected-warning@-12 {{pset(Pre(owner_const_ref)) = ((*owner_const_ref))}}
  // expected-warning@-13 {{pset(Pre(*owner_const_ref)) = ((*(*owner_const_ref)))}}
  // expected-warning@-14 {{pset(Pre(int_ref)) = ((*int_ref))}}
  // expected-warning@-15 {{pset(Pre(const_int_ref)) = ((*const_int_ref))}}
  // expected-warning@-16 {{pset(Pre(ptr_ptr)) = ((*ptr_ptr), (null))}}
  // expected-warning@-17 {{pset(Pre(*ptr_ptr)) = ((invalid))}}
  // expected-warning@-18 {{pset(Pre(ptr_by_value)) = ((*ptr_by_value), (null))}}
  // expected-warning@-19 {{pset(Post(*ptr_ref)) = ((*(*owner_ref)), (*(*ptr_const_ref)), (*int_ref), (*ptr_by_value), (null))}}
  // expected-warning@-20 {{pset(Post(*ptr_ptr)) = ((*(*owner_ref)), (*(*ptr_const_ref)), (*int_ref), (*ptr_by_value), (null))}}
  __lifetime_contracts(p4);
  // expected-warning@-1 {{pset(Pre(a)) = ((*a), (null))}}
  // expected-warning@-2 {{pset(Pre(b)) = ((*a), (null))}}
  // expected-warning@-3 {{pset(Pre(c)) = ((*c))}}
  // expected-warning@-4 {{pset(Pre(*c)) = ((invalid))}}
  // expected-warning@-5 {{pset(Post(*c)) = ((*a), (null))}}
  __lifetime_contracts(p5);
  // expected-warning@-1 {{pset(Pre(a)) = ((*a), (null))}}
  // expected-warning@-2 {{pset(Pre(b)) = ((*b), (null))}}
  // expected-warning@-3 {{pset(Post(Return)) = ((*a), (*b), (null))}}
  __lifetime_contracts(p6);
  // expected-warning@-1 {{pset(Pre(a)) = ((*a), (null))}}
  // expected-warning@-2 {{pset(Pre(b)) = ((*b), (null))}}
  // expected-warning@-3 {{pset(Post(Return)) = ((*a), (null))}}
  __lifetime_contracts(&S::f);
  // expected-warning@-1 {{pset(Pre(a)) = ((*a), (null))}}
  // expected-warning@-2 {{pset(Pre(b)) = ((*b), (null))}}
  // expected-warning@-3 {{pset(Pre(c)) = ((*c))}}
  // expected-warning@-4 {{pset(Pre(*c)) = ((invalid))}}
  // expected-warning@-5 {{pset(Pre(This)) = ((*this))}}
  // expected-warning@-6 {{pset(Post(*c)) = ((*a), (*b), (null))}}
  // expected-warning@-7 {{pset(Post(Return)) = ((*a), (*b), (null))}}
  __lifetime_contracts(&S::g);
  // expected-warning@-1 {{pset(Pre(a)) = ((*a), (null))}}
  // expected-warning@-2 {{pset(Pre(b)) = ((*b), (null))}}
  // expected-warning@-3 {{pset(Pre(c)) = ((*c))}}
  // expected-warning@-4 {{pset(Pre(*c)) = ((invalid))}}
  // expected-warning@-5 {{pset(Pre(This)) = ((*this))}}
  // expected-warning@-6 {{pset(Post(*c)) = ((*a), (*b), (null))}}
  // expected-warning@-7 {{pset(Post(Return)) = ((*this))}}
}
} // namespace dump_contracts
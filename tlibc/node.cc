#define _DISALLOW_COPY(classname)                                              \
  classname &operator=(const classname &other) = delete;                       \
  classname(const classname &other) = delete;

#include <stdio.h>
#include <stdlib.h>

#include "malloc.h"
#include "move.h"

struct allocator {
  void *malloc(unsigned long size) { return ::cpp_alloc(size); }
  void free(void *pt, unsigned long size) { ::cpp_free(pt, size); }
};

struct allocator_trace {
  static size_t alloc_size;
  static size_t free_size;
  void *malloc(unsigned long size) {
    alloc_size += size;
    return ::malloc(size);
  }
  void free(void *pt, unsigned long size) {
    ::free(pt);
    free_size += size;
  }
};

template <typename _Tp, typename _Alloc = allocator> class Node {
public:
  /* DO NOT USE this */
  _Tp *value_;

public:
  Node() : value_((_Tp *)_Alloc().malloc(sizeof(_Tp))) {
    ::new ((void *)value_) _Tp();
  }

  ~Node() { reset(); }

  explicit Node(_Tp &&other) : value_((_Tp *)_Alloc().malloc(sizeof(_Tp))) {
    ::new ((void *)value_) _Tp((_Tp &&)other);
  }

  template <typename... _Args>
  explicit Node(_Args &&...args) : value_((_Tp *)_Alloc().malloc(sizeof(_Tp))) {
    ::new ((void *)value_) _Tp(into<_Args>(args)...);
  }

  _DISALLOW_COPY(Node)

  template <typename _Derived /* extends _Tp */>
  Node(Node<_Derived, _Alloc> &&other) : value_(other.value_) {
    other.value_ = nullptr;
  }

  template <typename _Derived /* extends _Tp */>
  Node &operator=(Node<_Derived, _Alloc> &&other) {
    reset();
    this->value_ = other.value_;
    other.value_ = nullptr;
    return *this;
  }

  _Tp &operator*() { return *value_; }
  _Tp *operator->() { return value_; }

  void drop() { reset(); }

private:
  void reset() {
    if (!value_) {
      return;
    }
    value_->~_Tp();
    _Alloc().free((void *)value_, sizeof(_Tp));
    value_ = nullptr;
  }
};

size_t allocator_trace::alloc_size = 0;
size_t allocator_trace::free_size = 0;

struct Person {
  int age_;
  char gender_;
  Node<int> property_;
  Person(int age, char gender, Node<int> &&p)
      : age_(age), gender_(gender), property_(into<Node<int>>(p)) {}
  virtual ~Person() { /* This person dies. */
    printf("%p dies at %d\n", this, age_);
  }

  void birthday() { age_++; }
  virtual const char *feeling() const = 0;
};

struct Man : public Person {
  Man(int age, Node<int> &&p) : Person(age, 'M', into<Node<int>>(p)) {}
  ~Man() { printf("oops 😭😭\n"); }
  const char *feeling() const { return "😃"; }
};

struct Woman : public Person {
  Woman(int age, Node<int> &&p) : Person(age, 'F', into<Node<int>>(p)) {}
  ~Woman() { printf("😭~😭~😭\n"); }
  const char *feeling() const { return "😭"; }
};

int main(int argc, char **argv) {

  Node<Person, allocator_trace> nodp =
      Node<Man, allocator_trace>(12, Node<int>(42));
  nodp->birthday();
  printf("%s\n", nodp->feeling());
  nodp = Node<Woman, allocator_trace>(41, Node<int>(42));
  printf("%s\n", nodp->feeling());
  /* As expected, this will not work: */
  // nodp = Node<int, allocator_trace>(42);

  Node<Person, allocator_trace> node =
      Node<Man, allocator_trace>(22, Node<int>(42));
  node = move(nodp);

  return 0;
}

__attribute__((destructor)) static void allocator_trace_stat() {
  printf("alloced size = %ld\n", allocator_trace::alloc_size);
  printf("free size = %ld\n", allocator_trace::free_size);
}

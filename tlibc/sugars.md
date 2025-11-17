# Run C++ in Freestanding Environments

## Preface

Good programming languages make elegant programs. Unfortunately,
they may have few low-level development tools, or require
specific runtime support. I have recently attempted to make
C++ programs work without linking its standard library(`-lstdc++`),
and I'm sharing my findings in this document.


## Compile without libstdc++

To compiling C++ source code without runtime library support, you have to 
use the `-fno-rtti`(disable RunTime Type Identification) and `-fno-exceptions` flag.
Also, it is impossible to 

> See also: [OSDev Wiki](https://wiki.osdev.org/Bare_Bones#Writing_a_kernel_in_C++)

## Override `new` and `delete` Operator

You probably have to implement your own `malloc` and `free` - `mymalloc` and
`myfree`. After that, override `new` and `delete` like this:

> Your `myfree` function can take two arguments: a pointer and 
> size of its buffer. This may simplify some of your design.

```c++
void *operator new(unsigned long size) { return myalloc(size); }

void *operator new[](unsigned long size) { return myalloc(size); }

void operator delete(void *ptr, unsigned long size) {
  myfree(ptr, size);
}

void operator delete[](void *ptr, unsigned long size) {
  myfree(ptr, size);
}
```

It is impossible to throw `std::bad_alloc` when your `mymalloc` fails,
for exceptions are not supported without the standard library.

> Take a look at the "new" header file as well. It is located at `/usr/include/c++/11/new`
> on my machine.


## Use Template Class

I haven't found limitations of using template classes/structs. Use them
as you feel fit.

## Use Custom Allocators for Different Classes

> Please do not treat anything mentioned in this subsection
> as my original work. They are not. They are simply my
> discoveries when trying read the standard C++ library 
> to code something of my own.

In this section we will go through a design of a `Node` class,
which manages a single pointer and frees it in its destructor:

```c++
/* These are needed later. */
inline void *operator new(size_t, void *__p) { return __p; }
inline void *operator new[](size_t, void *__p) { return __p; }

/* An allocator class, which simply pack our malloc/free.  */
struct allocator {
  void *malloc(unsigned long size) { return ::mymalloc(size); }
  void free(void *pt, unsigned long size) { ::myfree(pt); }
};

/* Our template Node class. */
template <typename _Tp, typename _Alloc = allocator> class Node {
private:
  _Tp *value_;

public:
  Node() : value_((_Tp *)_Alloc().malloc(sizeof(_Tp))) {
    /* explicitly call the contructor of _Tp. */
    ::new ((void *)value_) _Tp();
  }

  ~Node() { reset(); }
private:
  /* Explicitly calls the destructor, and frees the memory. */
  void reset() {
    if (!value_) {
      return;
    }
    value_->~_Tp();
    _Alloc().free((void *)value_, sizeof(_Tp));
    value_ = nullptr;
  }
};
```

### Deal with Absence of a Default Constructor

If your class unfortunately does not have a default constructor, 
but it has a copy/move constructor, this shall work as well:

```c++
  /* --snip-- */
  Node(_Tp &&other) : value_((_Tp *) _Alloc().malloc(sizeof(_Tp))) {
    ::new ((void *) value_) _Tp((_Tp &&) other);
  }
```

### Explicitly Call the Contructor with Arbitray Argument List

> Caveat: this only works with -std=c++17 or higher.

Even if our class have a move constructor, our `Node` class
still lacks flexibility of constructing more complex types.
We want it to use any given constructor of its underlying type.

Here's how to achieve this:

```c++
  template <typename... _Args> /* possibly variable length arglist */
  Node (_Args&&... args) : value_((_Tp *) _Alloc().malloc(sizeof(_Tp))) {
    ::new ((void *) value_) _Tp(into<_Args>(args) ...);
  }
```

This is impossible without a `into` template function:

```c++
/* These are adapted from std::remove_reference, defined in <type_traits> */
template <typename _Tp> struct _rm_ref {
  typedef _Tp type;
};
template <typename _Tp> struct _rm_ref<_Tp &> {
  typedef _Tp type;
};
template <typename _Tp> struct _rm_ref<_Tp &&> {
  typedef _Tp type;
};

/* These are adapted from std::forward, defined in  <bits/move.h> */
template <typename _Tp> constexpr _Tp &&into(typename _rm_ref<_Tp>::type &t) {
  return (_Tp &&)t;
}
template <typename _Tp> constexpr _Tp &&into(typename _rm_ref<_Tp>::type &&t) {
  return (_Tp &&)t;
}

/* Adapted from std::move() */
template <typename _Tp>
constexpr typename _rm_ref<_Tp>::type &&move(_Tp &&obj) noexcept {
  return static_cast<typename _rm_ref<_Tp>::type &&>(obj);
}
```

### Inheritance

Consider the following classes:

```c++
struct Person {
  int age_;
  char gender_;
  Node<int> property_;
  Person(int age, char gender, Node<int> &&p)
      : age_(age), gender_(gender), property_(into<Node<int>>(p)) {}
  virtual ~Person() { /* This person dies. */ }
  virtual const char *feeling() const = 0;
};

struct Man : public Person {
  Man(int age, Node<int> &&p) : Person(age, 'M', into<Node<int>>(p)) {}
  ~Man() = default;
  const char *feeling() const { return "😃"; }
};

struct Woman : public Person {
  Woman(int age, Node<int> &&p) : Person(age, 'F', into<Node<int>>(p)) {}
  ~Woman() = default;
  const char *feeling() const { return "😭"; }
};
```

And what we want is to use `Node<Person>` to store 
both `Man` and `Woman`:

```c++
  Node<Person> node = Node<Man>(12, Node<int>(42));
  node = Node<Woman>(41, Node<int>(42));
  node = Node<int>(42); /* This should be forbidden. */
```

To achieve this, a template move constructor is needed:

```c++
  template <typename _Derived /* extends _Tp */>
  Node(Node<_Derived, _Alloc> &&other) : value_(other.value_) {
    other.value_ = nullptr;
  }

  template <typename _Derived /* extends _Tp */>
  Node &operator=(Node<_Derived, _Alloc> &&other) {
    drop(); /* Frees the pointer its currently keeping. */
    this->value_ = other.value_;
    other.value_ = nullptr;
    return *this;
  }
```

> However, this constructor won't work unless we
> make `value_` a public member. An annoying issue(😭).

## Conclusion

Most of C++'s features are supported without its runtime libraries.
Make good use of them for your low-level(OS, system software, etc.) 
development. The end.


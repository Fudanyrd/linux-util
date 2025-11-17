#ifndef _INTO_H_
#define _INTO_H_ 1

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

template <typename _Tp>
constexpr typename _rm_ref<_Tp>::type &&move(_Tp &&obj) noexcept {
  return static_cast<typename _rm_ref<_Tp>::type &&>(obj);
}

#endif /* _INTO_H_ 1 */

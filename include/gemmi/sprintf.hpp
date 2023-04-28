// Copyright 2017 Global Phasing Ltd.
//
// interface to stb_sprintf: gstb_snprintf, to_str(float|double)

#ifndef GEMMI_SPRINTF_HPP_
#define GEMMI_SPRINTF_HPP_

#include <string>
#include <version>  // for __cpp_lib_to_chars

#if __cpp_lib_to_chars >= 201611L
# include <charconv>
#endif

#include "fail.hpp"  // for GEMMI_DLL

namespace gemmi {

// On MinGW format(printf) doesn't support %zu.
#if (defined(__GNUC__) && !defined(__MINGW32__)) || defined(__clang__)
# define GEMMI_ATTRIBUTE_FORMAT(fmt,va) __attribute__((format(printf,fmt,va)))
#else
# define GEMMI_ATTRIBUTE_FORMAT(fmt,va)
#endif
GEMMI_DLL int gstb_sprintf(char *buf, char const *fmt, ...) GEMMI_ATTRIBUTE_FORMAT(2,3);
GEMMI_DLL int gstb_snprintf(char *buf, int count, char const *fmt, ...)
                                                            GEMMI_ATTRIBUTE_FORMAT(3,4);

inline std::string to_str(double d) {
  char buf[24];
  int len = gstb_sprintf(buf, "%.9g", d);
  return std::string(buf, len > 0 ? len : 0);
}

inline std::string to_str(float d) {
  char buf[16];
  int len = gstb_sprintf(buf, "%.6g", d);
  return std::string(buf, len > 0 ? len : 0);
}

template<int Prec>
std::string to_str_prec(double d) {
  static_assert(Prec >= 0 && Prec < 7, "unsupported precision");
  char buf[16];
  int len = d > -1e8 && d < 1e8 ? gstb_sprintf(buf, "%.*f", Prec, d)
                                : gstb_sprintf(buf, "%g", d);
  return std::string(buf, len > 0 ? len : 0);
}

/// zero-terminated to_chars()
inline char* to_chars_z(char* first, char* last, int value) {
#if __cpp_lib_to_chars >= 201611L
  auto result = std::to_chars(first, last-1, value);
  *result.ptr = '\0';
  return result.ptr;
#else
  int n = gstb_snprintf(first, int(last - first), "%d", value);
  return std::min(first + n, last - 1);
#endif
}
inline char* to_chars_z(char* first, char* last, size_t value) {
#if __cpp_lib_to_chars >= 201611L
  auto result = std::to_chars(first, last-1, value);
  *result.ptr = '\0';
  return result.ptr;
#else
  int n = gstb_snprintf(first, int(last - first), "%zu", value);
  return std::min(first + n, last - 1);
#endif
}

} // namespace gemmi
#endif

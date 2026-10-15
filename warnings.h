// language: C, file: warnings.h
#ifndef WARNINGS_H
#define WARNINGS_H

#define PUSH_WARNINGS _Pragma("GCC diagnostic push")
#define POP_WARNINGS  _Pragma("GCC diagnostic pop")
#define DISABLE_VS_WARNINGS(w)

#if defined(__clang__)
#  define DISABLE_GCC_WARNING(w)
#  define DISABLE_CLANG_WARNING(w) _Pragma("clang diagnostic ignored \"-W" #w "\"")
#  define DISABLE_GCC_AND_CLANG_WARNING(w) _Pragma("clang diagnostic ignored \"-W" #w "\"")
#else
#  define DISABLE_GCC_WARNING(w) _Pragma("GCC diagnostic ignored \"-W" #w "\"")
#  define DISABLE_CLANG_WARNING(w)
#  define DISABLE_GCC_AND_CLANG_WARNING(w) _Pragma("GCC diagnostic ignored \"-W" #w "\"")
#endif

#endif

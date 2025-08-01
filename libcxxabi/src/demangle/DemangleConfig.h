//===--- DemangleConfig.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This file is contains a subset of macros copied from
// llvm/include/llvm/Demangle/DemangleConfig.h
//===----------------------------------------------------------------------===//

#ifndef LIBCXXABI_DEMANGLE_DEMANGLE_CONFIG_H
#define LIBCXXABI_DEMANGLE_DEMANGLE_CONFIG_H

// Must be defined before pulling in headers from libc++. Allow downstream
// build systems to override this value.
// https://libcxx.llvm.org/UsingLibcxx.html#enabling-the-safe-libc-mode
#ifndef _LIBCPP_VERBOSE_ABORT
#define _LIBCPP_VERBOSE_ABORT(...) __abort_message(__VA_ARGS__)
#include "../abort_message.h"
#endif

#ifndef _LIBCPP_LOG_HARDENING_FAILURE
// Libc++abi does not have any functionality to log and continue, so we drop
// error messages when we build the demangler with `observe` assertion semantic.
// Once the layering with libc++ is improved, this could use the libc++
// functionality to log hardening failures.
#define _LIBCPP_LOG_HARDENING_FAILURE(message) ((void)0)
#endif

#include <version>

#ifdef _MSC_VER
// snprintf is implemented in VS 2015
#if _MSC_VER < 1900
#define snprintf _snprintf_s
#endif
#endif

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#ifndef __has_cpp_attribute
#define __has_cpp_attribute(x) 0
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifndef DEMANGLE_GNUC_PREREQ
#if defined(__GNUC__) && defined(__GNUC_MINOR__) && defined(__GNUC_PATCHLEVEL__)
#define DEMANGLE_GNUC_PREREQ(maj, min, patch)                           \
  ((__GNUC__ << 20) + (__GNUC_MINOR__ << 10) + __GNUC_PATCHLEVEL__ >=          \
   ((maj) << 20) + ((min) << 10) + (patch))
#elif defined(__GNUC__) && defined(__GNUC_MINOR__)
#define DEMANGLE_GNUC_PREREQ(maj, min, patch)                           \
  ((__GNUC__ << 20) + (__GNUC_MINOR__ << 10) >= ((maj) << 20) + ((min) << 10))
#else
#define DEMANGLE_GNUC_PREREQ(maj, min, patch) 0
#endif
#endif

#if __has_attribute(used) || DEMANGLE_GNUC_PREREQ(3, 1, 0)
#define DEMANGLE_ATTRIBUTE_USED __attribute__((__used__))
#else
#define DEMANGLE_ATTRIBUTE_USED
#endif

#if __has_builtin(__builtin_unreachable) || DEMANGLE_GNUC_PREREQ(4, 5, 0)
#define DEMANGLE_UNREACHABLE __builtin_unreachable()
#elif defined(_MSC_VER)
#define DEMANGLE_UNREACHABLE __assume(false)
#else
#define DEMANGLE_UNREACHABLE
#endif

#if __has_attribute(noinline) || DEMANGLE_GNUC_PREREQ(3, 4, 0)
#define DEMANGLE_ATTRIBUTE_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define DEMANGLE_ATTRIBUTE_NOINLINE __declspec(noinline)
#else
#define DEMANGLE_ATTRIBUTE_NOINLINE
#endif

#if !defined(NDEBUG)
#define DEMANGLE_DUMP_METHOD DEMANGLE_ATTRIBUTE_NOINLINE DEMANGLE_ATTRIBUTE_USED
#else
#define DEMANGLE_DUMP_METHOD DEMANGLE_ATTRIBUTE_NOINLINE
#endif

#if __cplusplus > 201402L && __has_cpp_attribute(fallthrough)
#define DEMANGLE_FALLTHROUGH [[fallthrough]]
#elif __has_cpp_attribute(gnu::fallthrough)
#define DEMANGLE_FALLTHROUGH [[gnu::fallthrough]]
#elif !__cplusplus
// Workaround for llvm.org/PR23435, since clang 3.6 and below emit a spurious
// error when __has_cpp_attribute is given a scoped attribute in C mode.
#define DEMANGLE_FALLTHROUGH
#elif __has_cpp_attribute(clang::fallthrough)
#define DEMANGLE_FALLTHROUGH [[clang::fallthrough]]
#else
#define DEMANGLE_FALLTHROUGH
#endif

#ifndef DEMANGLE_ASSERT
#include <cassert>
#define DEMANGLE_ASSERT(__expr, __msg) assert((__expr) && (__msg))
#endif

#define DEMANGLE_NAMESPACE_BEGIN namespace { namespace itanium_demangle {
#define DEMANGLE_NAMESPACE_END } }

// The DEMANGLE_ABI macro resolves to nothing when building libc++abi. Only
// the llvm copy defines DEMANGLE_ABI as a visibility attribute.
#define DEMANGLE_ABI

#endif // LIBCXXABI_DEMANGLE_DEMANGLE_CONFIG_H

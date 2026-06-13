#pragma once

#if defined(_MSC_VER)
#define COMPILER_MSVC
#if defined(_M_IX86)
#define ARCH_X86
#elif defined(_M_X64)
#define ARCH_X86_64
#elif defined(_M_ARM)
#define ARCH_ARM
#endif
#elif defined(__llvm__)
#define COMPILER_LLVM
#elif defined(__GNUC__)
#define COMPILER_GCC
#endif

#if defined(COMPILER_LLVM) || defined(COMPILER_GCC)
#if defined(__i386__)
#define ARCH_X86
#elif defined(__x86_64__)
#define ARCH_X86_64
#elif defined(__arm__)
#define ARCH_ARM
#endif
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
#define ARCH_X86_GENERIC
#endif

#ifdef ARCH_X86_GENERIC
#ifdef COMPILER_MSVC
#if defined(ARCH_X86) && _M_IX86_FP != 2
#error Auto timing module requires /arch:SSE2 on x86
#endif
#else
#ifndef __SSE2_MATH__
#error Auto timing module requires -mfpmath=sse -msse2 on x86
#endif
#endif
#endif
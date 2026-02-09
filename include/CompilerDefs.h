#ifndef LIBOTHER_COMPILERDEFS_H
#define LIBOTHER_COMPILERDEFS_H

#define LIBOTHER_PLATFORM_WINDOWS          0
#define LIBOTHER_PLATFORM_UNIX             1
#define LIBOTHER_PLATFORM_APPLE            2

#if defined( _WIN32 )
#  define LIBOTHER_PLATFORM          LIBOTHER_PLATFORM_WINDOWS
#elif defined( __APPLE__ )
#  define LIBOTHER_PLATFORM          LIBOTHER_PLATFORM_APPLE
#else
#  define LIBOTHER_PLATFORM          LIBOTHER_PLATFORM_UNIX
#endif

#define LIBOTHER_COMPILER_MICROSOFT          0
#define LIBOTHER_COMPILER_GNU                1
#define LIBOTHER_COMPILER_INTEL              2

#ifdef _MSC_VER
#  define LIBOTHER_COMPILER          LIBOTHER_COMPILER_MICROSOFT
#elif defined( __INTEL_COMPILER )
#  define LIBOTHER_COMPILER          LIBOTHER_COMPILER_INTEL
#elif defined( __GNUC__ )
#  define LIBOTHER_COMPILER          LIBOTHER_COMPILER_GNU
#  define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
#  error "FATAL ERROR: Unknown compiler."
#endif

#define LIBOTHER_UNUSED(x) (void)(x)

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#ifndef LIBOTHER_DEBUG
#   if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#       define LIBOTHER_DEBUG 1
#   else
#       define LIBOTHER_DEBUG 0
#   endif
#endif

#endif

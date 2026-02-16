#ifndef LIBOTHER_COMPILERDEFS_H
#define LIBOTHER_COMPILERDEFS_H

// Числовой идентификатор для платформы Windows.
#define LIBOTHER_PLATFORM_WINDOWS          0
// Числовой идентификатор для Unix/Linux-подобных систем.
#define LIBOTHER_PLATFORM_UNIX             1
// Числовой идентификатор для платформ Apple (macOS/iOS toolchain).
#define LIBOTHER_PLATFORM_APPLE            2

// Определяем текущую платформу на этапе препроцессора.
#if defined( _WIN32 )
#  define LIBOTHER_PLATFORM          LIBOTHER_PLATFORM_WINDOWS
#elif defined( __APPLE__ )
#  define LIBOTHER_PLATFORM          LIBOTHER_PLATFORM_APPLE
#else
#  define LIBOTHER_PLATFORM          LIBOTHER_PLATFORM_UNIX
#endif

// Числовой идентификатор для компилятора Microsoft Visual C++.
#define LIBOTHER_COMPILER_MICROSOFT          0
// Числовой идентификатор для компилятора GCC.
#define LIBOTHER_COMPILER_GNU                1
// Числовой идентификатор для компилятора Intel.
#define LIBOTHER_COMPILER_INTEL              2

// Определяем используемый компилятор.
#ifdef _MSC_VER
#  define LIBOTHER_COMPILER          LIBOTHER_COMPILER_MICROSOFT
#elif defined( __INTEL_COMPILER )
#  define LIBOTHER_COMPILER          LIBOTHER_COMPILER_INTEL
#elif defined( __GNUC__ )
#  define LIBOTHER_COMPILER          LIBOTHER_COMPILER_GNU
// Числовая версия GCC в формате major*10000 + minor*100 + patch.
#  define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
#  error "FATAL ERROR: Unknown compiler."
#endif

// На Windows отключаем предупреждения CRT о "небезопасных" C-функциях.
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

// Если режим отладки явно не задан, вычисляем его по стандартным макросам.
#ifndef LIBOTHER_DEBUG
#   if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
#       define LIBOTHER_DEBUG 1
#   else
#       define LIBOTHER_DEBUG 0
#   endif
#endif

#endif

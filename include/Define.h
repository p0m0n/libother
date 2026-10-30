#ifndef LIBOTHER_DEFINE_H
#define LIBOTHER_DEFINE_H

#pragma once

#include "CompilerDefs.h"

// ==========================================
// 1. СТАНДАРТНЫЕ ПОТОКИ И ВВОД/ВЫВОД
// ==========================================
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>

// ==========================================
// 2. КОНТЕЙНЕРЫ И СТРОКОВЫЕ АЛГОРИТМЫ
// ==========================================
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <numeric>
#include <regex>

// ==========================================
// 3. МНОГОПОТОЧНОСТЬ И СИНХРОНИЗАЦИЯ
// ==========================================
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>

// ==========================================
// 4. ВРЕМЯ И ВСПОМОГАТЕЛЬНЫЕ СРЕДСТВА
// ==========================================
#include <ctime>
#include <chrono>
#include <stdexcept>
#include <cassert>

// ==========================================
// 5. ИЗОЛЯЦИЯ ПЛАТФОРМ И МАКРОСЫ СОВМЕСТИМОСТИ
// ==========================================
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#include <windows.h>

// Убираем токсичные макросы Windows, конфликтующие со стандартной библиотекой C++
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef ARRAYSIZE
#undef ARRAYSIZE
#endif
#ifdef _ARRAYSIZE
#undef _ARRAYSIZE
#endif

// Безопасные аналоги функций для MSVC
#define m_sscanf sscanf_s
#define m_printf printf_s
#else
    // POSIX-совместимые макросы для Linux / macOS
#define m_sscanf sscanf
#define m_printf printf
#endif

// ==========================================
// 6. УНИВЕРСАЛЬНЫЕ ВСПОМОГАТЕЛЬНЫЕ МАКРОСЫ
// ==========================================

/**
 * @brief Безопасное вычисление количества элементов встроенного C-массива на этапе компиляции.
 */
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

 /**
  * @brief Подавляет предупреждения компилятора C4100 / -Wunused-parameter для неиспользуемых аргументов.
  */
#define LIBOTHER_UNUSED(x) (void)(x)

#endif // LIBOTHER_DEFINE_H

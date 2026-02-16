#ifndef LIBOTHER_DEFINE_H
#define LIBOTHER_DEFINE_H

#include "CompilerDefs.h"

// Базовые потоки и форматирование ввода/вывода.
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

// Базовые контейнеры и алгоритмы строковой обработки.
#include <string>
#include <vector>
#include <map>
#include <numeric>
#include <regex>

// Многопоточность, синхронизация и атомарные операции.
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

// Работа со временем и базовые исключения/проверки.
#include <ctime>
#include <chrono>
#include <stdexcept>
#include <cassert>


// Убираем конфликтные макросы.
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#   include <windows.h>
#   undef min
#   undef max
#   undef ARRAYSIZE
#   undef _ARRAYSIZE
#   define m_sscanf                    sscanf_s
#else
#   define m_sscanf                    sscanf
#endif

// Универсальный макрос для вычисления количества элементов C-массива.
#define ARRAY_SIZE(A)    (sizeof(A)/sizeof((A)[0]))

// Явно помечает параметр как неиспользуемый, чтобы подавить предупреждения.
#define LIBOTHER_UNUSED(x) (void)(x)

#endif

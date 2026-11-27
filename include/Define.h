#ifndef LIBOTHER_DEFINE_H
#define LIBOTHER_DEFINE_H

// ==========================================
// 1. КОНФИГУРАЦИЯ ПРОЕКТА И ПЛАТФОРМЫ
// ==========================================
#include "CompilerDefs.h"    // Макросы определения платформы, компилятора и системных опций проекта

// ==========================================
// 2. СТАНДАРТНЫЙ ВВОД / ВЫВОД И ПОТОКИ
// ==========================================
#include <iostream>    // Стандартные потоки ввода-вывода в консоль (std::cin, std::cout, std::cerr)
#include <fstream>     // Работа с файловыми потоками для чтения и записи (std::ifstream, std::ofstream)
#include <sstream>     // Форматирование и преобразование строк через потоки (std::stringstream)
#include <iomanip>     // Манипуляторы формата вывода потоков (std::setw, std::setprecision, std::hex)
#include <cstdio>      // Низкоуровневые C-функции ввода-вывода (printf, sscanf, fopen)

// ==========================================
// 3. КОНТЕЙНЕРЫ И СТРУКТУРЫ ДАННЫХ (STL)
// ==========================================
#include <array>       // Фиксированный массив статичной длины C++ (std::array)
#include <vector>      // Динамический массив с автоматическим изменением размера (std::vector)
#include <map>         // Упорядоченный ассоциативный массив "ключ-значение" (std::map)
#include <queue>       // Контейнеры-адаптеры: очередь (std::queue) и очередь с приоритетом (std::priority_queue)
#include <memory>      // Умные указатели (std::shared_ptr, std::unique_ptr, std::make_shared)

// ==========================================
// 4. СТРОКИ И РЕГУЛЯРНЫЕ ВЫРАЖЕНИЯ
// ==========================================
#include <string>      // Динамические C++ строки с управлением памятью (std::string, std::wstring)
#include <string_view> // Легковесная ссылка на строку без копирования данных (std::string_view, C++17)
#include <cstring>     // Низкоуровневые C-функции для байтовых массивов и C-строк (memcpy, memset, strlen)
#include <cctype>      // Функции классификации символов (std::isdigit, std::isspace)
#include <regex>       // Шаблоны регулярных выражений для поиска и валидации текста (std::regex)

// ==========================================
// 5. АЛГОРИТМЫ И МАТЕМАТИКА
// ==========================================
#include <algorithm>   // Стандартные алгоритмы сортировки, поиска и трансформации (std::sort, std::find, std::clamp)
#include <numeric>     // Математические и числовые алгоритмы (std::accumulate, std::gcd, std::iota)
#include <cmath>       // Математические функции (std::isnan, std::isfinite)
#include <limits>      // Числовые пределы типов (std::numeric_limits)
#include <bit>         // Битовые операции и конвертация типов (std::bit_cast, подсчет установленных бит, C++20)

// ==========================================
// 6. МНОГОПОТОЧНОСТЬ И СИНХРОНИЗАЦИЯ
// ==========================================
#include <thread>             // Создание и управление потоками исполнения (std::thread, std::jthread)
#include <mutex>              // Примитивы взаимного исключения и блокировки (std::mutex, std::lock_guard)
#include <atomic>             // Атомарные типы данных для потокобезопасных операций без мьютексов (std::atomic)
#include <condition_variable> // Условные переменные для синхронизации ожидания между потоками

// ==========================================
// 7. ВРЕМЯ И ЛОКАЛИЗАЦИЯ
// ==========================================
#include <ctime>       // C-стиль работы со временем и датой (time_t, clock(), strftime)
#include <chrono>      // Современные C++ часы, интервалы и точки времени высокой точности (std::chrono)
#include <locale>      // Настройки локали, кодировок, региональных форматов чисел и дат (std::locale)

// ==========================================
// 8. МЕТАПРОГРАММИРОВАНИЕ И ТИПЫ
// ==========================================
#include <cstdint>     // Целочисленные типы с гарантированной шириной бит (int32_t, uint64_t, uint8_t)
#include <type_traits> // Инструменты метапрограммирования и проверки свойств типов на этапе компиляции (std::is_same)
#include <functional>  // Универсальные обертки функций, лямбд и связывание аргументов (std::function, std::bind)

// ==========================================
// 9. ОБРАБОТКА ОШИБОК И ОТЛАДКА
// ==========================================
#include <stdexcept>   // Стандартные классы исключений (std::runtime_error, std::invalid_argument)
#include <cassert>     // Макрос assert() для проверки условий в Debug-сборке

#include <limits.h>

// ==========================================
// 10. ИЗОЛЯЦИЯ ПЛАТФОРМ И МАКРОСЫ СОВМЕСТИМОСТИ
// ==========================================
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
    // Исключаем из windows.h редко используемые API (включая старый winsock.h).
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>

    // Удаляем конфликтные макросы Windows, нарушающие работу C++ STL
    #ifdef min
        #undef min
    #endif
    #ifdef max
        #undef max
    #endif

    #ifdef MIN
        #undef MIN
    #endif
    #ifdef MAX
        #undef MAX
    #endif

    #ifdef ARRAYSIZE
        #undef ARRAYSIZE
    #endif
    #ifdef _ARRAYSIZE
        #undef _ARRAYSIZE
    #endif

    // Перенаправление на безопасные (secure) версии функций ввода/вывода MSVC.
    // Внимание: для спецификаторов %s/%c/%[ у sscanf_s отличается сигнатура (требуется размер буфера).
    #define m_sscanf sscanf_s
    #define m_printf printf_s
#else
    // Стандартные POSIX-версии функций для Linux / macOS
    #define m_sscanf sscanf
    #define m_printf printf
#endif

// ==========================================
// 11. УНИВЕРСАЛЬНЫЕ ВСПОМОГАТЕЛЬНЫЕ МАКРОСЫ
// ==========================================

/**
 * @brief Безопасное вычисление количества элементов статического C-массива на этапе компиляции.
 */
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

/**
 * @brief Подавляет предупреждения компилятора (C4100 / -Wunused-parameter) для неиспользуемых параметров.
 */
#define LIBOTHER_UNUSED(x) (void)(x)

#endif // LIBOTHER_DEFINE_H

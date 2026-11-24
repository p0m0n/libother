#ifndef LIBOTHER_PRIVATEGLOBAL_HPP
#define LIBOTHER_PRIVATEGLOBAL_HPP

#include "Define.h"

namespace other
{
    /**
     * @class NumericCLocale
     * @brief Потокобезопасный RAII-хранитель классической числовой C-локали (`LC_NUMERIC = "C"`).
     *
     * Гарантирует форматирование вещественных чисел в `StreamSocket::WriteFormat()` строго с точкой `'.'`,
     * независимо от текущей локали приложения (`setlocale`). Создаётся один раз за время жизни процесса.
     */
    class NumericCLocale
    {
    private:
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        _locale_t _locale = nullptr; ///< Дескриптор числовой C-локали для `_vsnprintf_l` / `_vscprintf_l`.
#else
        locale_t  _locale = static_cast<locale_t>(0); ///< Дескриптор POSIX-локали для `uselocale()`.
#endif

        NumericCLocale() noexcept
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            _locale = ::_create_locale(LC_NUMERIC, "C");
#else
            _locale = ::newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(0));
#endif
        }

        ~NumericCLocale() noexcept
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            if (_locale != nullptr)
                ::_free_locale(_locale);
#else
            if (_locale != static_cast<locale_t>(0))
                ::freelocale(_locale);
#endif
        }

    public:
        NumericCLocale(const NumericCLocale&) = delete;
        NumericCLocale& operator=(const NumericCLocale&) = delete;

        /**
         * @brief Возвращает синглтон числовой C-локали (инициализация потокобезопасна с C++11).
         * @return Константная ссылка на экземпляр `NumericCLocale`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        static const NumericCLocale& Instance() noexcept
        {
            static const NumericCLocale instance;
            return instance;
        }

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        /**
         * @brief Возвращает дескриптор `_locale_t` для локально-независимых функций CRT.
         * @return Валидный `_locale_t` или `nullptr` при сбое инициализации.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        _locale_t Handle() const noexcept { return _locale; }
#else
        /**
         * @brief Возвращает дескриптор `locale_t` для потоково-локального переключения `uselocale()`.
         * @return Валидный `locale_t` или `0` при сбое инициализации.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        locale_t Handle() const noexcept { return _locale; }
#endif
    };

#if LIBOTHER_PLATFORM != LIBOTHER_PLATFORM_WINDOWS
    /**
     * @class ScopedThreadCLocale
     * @brief RAII-переключатель локали текущего потока на `"C"` (Linux/POSIX) с гарантированным откатом.
     */
    class ScopedThreadCLocale
    {
    private:
        locale_t _prev = static_cast<locale_t>(0); ///< Предыдущая локаль потока для восстановления в деструкторе.

    public:
        /**
         * @brief Переключает числовую локаль только текущего потока на `"C"`.
         * @param [in] cLoc Дескриптор `"C"`-локали из `NumericCLocale::Instance().Handle()`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        explicit ScopedThreadCLocale(locale_t cLoc) noexcept
        {
            if (cLoc != static_cast<locale_t>(0))
                _prev = ::uselocale(cLoc);
        }

        /**
         * @brief Восстанавливает исходную локаль текущего потока (в том числе при выбросе исключения).
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        ~ScopedThreadCLocale() noexcept
        {
            if (_prev != static_cast<locale_t>(0))
                ::uselocale(_prev);
        }

        ScopedThreadCLocale(const ScopedThreadCLocale&) = delete;
        ScopedThreadCLocale& operator=(const ScopedThreadCLocale&) = delete;
    };
#endif

    // ... остальные функции анонимного namespace (ParseSignedInt, ParseUnsignedInt, ParseFiniteFloat, FormatFloatLine) ...

} // !End namespace other

#endif // LIBOTHER_PRIVATEGLOBAL_HPP

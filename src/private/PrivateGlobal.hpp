/**
 * @file PrivateGlobal.hpp
 * @brief Общие внутренние утилиты библиотеки для локально-независимой работы с числами.
 *
 * @details
 * Этот заголовок предназначен исключительно для внутреннего использования внутри реализации
 * модулей библиотеки и не является частью публичного API.
 *
 * Во многих приложениях пользовательский код меняет глобальную локаль процесса (например, на русскую
 * или немецкую, где разделителем дробной части выступает запятая вместо точки). Сетевые протоколы,
 * сериализаторы и парсеры чисел при этом должны всегда работать по предсказуемым правилам классической
 * C-локали (разделитель — точка). Данный файл предоставляет потокобезопасное RAII-управление
 * числовой C-локалью для Windows и POSIX-систем без изменения глобальной локали всего процесса.
 */

#ifndef LIBOTHER_PRIVATEGLOBAL_HPP
#define LIBOTHER_PRIVATEGLOBAL_HPP

#include "Define.h"


namespace other
{
    /**
     * @class NumericCLocale
     * @brief Потокобезопасный RAII-синглтон, владеющий дескриптором классической числовой C-локали.
     *
     * Создаёт системный объект числовой локали `"C"` один раз при первом обращении и автоматически
     * освобождает его при завершении процесса. Позволяет форматировать и разбирать числа с точкой `'.'`
     * в качестве десятичного разделителя вне зависимости от региональных настроек приложения.
     *
     * Копирование запрещено; экземпляр доступен только через статический метод `Instance()`.
     */
    class NumericCLocale
    {
    private:
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        _locale_t _locale = nullptr; ///< Системный дескриптор числовой C-локали для Windows CRT.
#else
        locale_t  _locale = static_cast<locale_t>(0); ///< Системный дескриптор числовой C-локали для POSIX.
#endif

        /**
         * @brief Приватный конструктор: инициализирует системный объект числовой локали `"C"`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        NumericCLocale() noexcept
        {
#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
            _locale = ::_create_locale(LC_NUMERIC, "C");
#else
            _locale = ::newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(0));
#endif
        }

        /**
         * @brief Приватный деструктор: освобождает системный дескриптор локали, если он был успешно создан.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
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
         * @brief Возвращает единственный экземпляр `NumericCLocale` для текущего процесса.
         * @return Константная ссылка на потокобезопасно инициализированный синглтон `NumericCLocale`.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        static const NumericCLocale& Instance() noexcept
        {
            static const NumericCLocale instance;
            return instance;
        }

#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
        /**
         * @brief Возвращает нативный дескриптор числовой C-локали для Windows CRT.
         * @return Валидный дескриптор `_locale_t` при успехе или `nullptr`, если ОС не смогла создать локаль.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        _locale_t Handle() const noexcept { return _locale; }
#else
        /**
         * @brief Возвращает нативный дескриптор числовой C-локали для POSIX.
         * @return Валидный дескриптор `locale_t` при успехе или `0`, если ОС не смогла создать локаль.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        locale_t Handle() const noexcept { return _locale; }
#endif
    };

#if LIBOTHER_PLATFORM != LIBOTHER_PLATFORM_WINDOWS
    /**
     * @class ScopedThreadCLocale
     * @brief RAII-переключатель числовой локали текущего потока на `"C"` (для POSIX/Linux).
     *
     * Временно активирует переданную локаль только для вызывающего потока, не затрагивая остальные
     * потоки программы, и гарантированно восстанавливает предыдущую локаль при выходе из области
     * видимости (в том числе при раскрутке стека после исключения).
     *
     * Копирование запрещено; предназначен для размещения исключительно на стеке.
     */
    class ScopedThreadCLocale
    {
    private:
        locale_t _prev = static_cast<locale_t>(0); ///< Сохранённая предыдущая локаль текущего потока для отката в деструкторе.

    public:
        /**
         * @brief Активирует указанную локаль для текущего потока и запоминает предыдущую.
         * @param [in] cLoc Дескриптор локали (обычно из `NumericCLocale::Instance().Handle()`); при `0` переключение не выполняется.
         * @note Гарантированно не выбрасывает исключений (`noexcept`).
         */
        explicit ScopedThreadCLocale(locale_t cLoc) noexcept
        {
            if (cLoc != static_cast<locale_t>(0))
                _prev = ::uselocale(cLoc);
        }

        /**
         * @brief Восстанавливает исходную локаль текущего потока, сохранённую в конструкторе.
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

#ifndef LIBOTHER_UTILS_H
#define LIBOTHER_UTILS_H

#include "Define.h"


namespace other::utils
{
    /**
     * @brief Создает таблицу быстрого поиска (Lookup Table).
     * Заполняет фиксированный массив из 256 элементов индексами символов алфавита (или -1).
     */
    void BuildInverseMap(const std::string& input, size_t size, int8_t* result);

    namespace utf8
    {
        /**
         * @brief Кодирует одну кодовую точку Unicode (UTF-32) в 1..4 байта UTF-8 и дописывает в out.
         * @param [in]  codePoint Кодовая точка Unicode (U+0000..U+10FFFF, исключая суррогаты U+D800..U+DFFF).
         * @param [out] out       Целевая UTF-8 строка, в конец которой добавляются байты.
         * @return true при успехе; false, если кодовая точка недопустима.
         */
        bool Utf8EncodeCodePoint(char32_t codePoint, std::string& out);
        /**
         * @brief Декодирует следующую кодовую точку Unicode из UTF-8 строки начиная с позиции offset.
         * @param [in]     text         Исходная UTF-8 строка.
         * @param [in,out] offset       Текущий индекс байта (при успехе сдвигается на длину символа).
         * @param [out]    outCodePoint Декодированная кодовая точка Unicode.
         * @return true при успехе; false при невалидной UTF-8 последовательности или выходе за границы.
         */
        bool Utf8DecodeCodePoint(std::string_view text, size_t& offset, char32_t& outCodePoint);
        /**
         * @brief Проверяет, является ли строка корректной последовательностью UTF-8.
         */
        bool IsValidUtf8(std::string_view text);
        /**
         * @brief Подсчитывает количество символов (кодовых точек Unicode) в валидной UTF-8 строке.
         * @return Количество символов или static_cast<size_t>(-1), если строка содержит невалидный UTF-8.
         */
        size_t Utf8Length(std::string_view text);
        /**
         * @brief Кодирует строку UTF-32 (std::u32string_view) в UTF-8 (std::string).
         */
        bool Utf32ToUtf8(std::u32string_view input, std::string& output);
        /**
         * @brief Декодирует строку UTF-8 (std::string_view) в UTF-32 (std::u32string).
         */
        bool Utf8ToUtf32(std::string_view input, std::u32string& output);
        /**
         * @brief Кодирует строку UTF-16 (std::u16string_view, включая суррогатные пары) в UTF-8.
         */
        bool Utf16ToUtf8(std::u16string_view input, std::string& output);
        /**
         * @brief Декодирует строку UTF-8 (std::string_view) в UTF-16 (std::u16string, с суррогатными парами).
         */
        bool Utf8ToUtf16(std::string_view input, std::u16string& output);
        /**
         * @brief Кодирует кроссплатформенную широкую строку (std::wstring_view: UTF-16 на Windows, UTF-32 на Linux/macOS) в UTF-8.
         */
        bool WideToUtf8(std::wstring_view input, std::string& output);
        /**
         * @brief Декодирует UTF-8 строку в кроссплатформенную std::wstring (UTF-16 на Windows, UTF-32 на Linux/macOS).
         */
        bool Utf8ToWide(std::string_view input, std::wstring& output);
    }
}

#endif // LIBOTHER_UTILS_H

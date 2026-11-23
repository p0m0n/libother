#include "Utils.h"

namespace other
{
    void utils::BuildInverseMap(const std::string& input, size_t size, int8_t* result)
    {
        std::fill(result, result + 256, -1);
        for (size_t i = 0; i < size; i++)
        {
            result[static_cast<uint8_t>(input[i])] = static_cast<int8_t>(i);
        }
    }

    // Кодирует одну кодовую точку Unicode (UTF-32) в 1..4 байта UTF-8 и дописывает в out.
    bool utils::utf8::Utf8EncodeCodePoint(char32_t codePoint, std::string& out)
    {
        // Отклоняем суррогаты UTF-16 и значения выше U+10FFFF.
        if (codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
            return false;

        if (codePoint <= 0x7FU)
        {
            // 1 байт: 0xxxxxxx
            out.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7FFU)
        {
            // 2 байта: 110xxxxx 10xxxxxx
            out.push_back(static_cast<char>(0xC0U | ((codePoint >> 6) & 0x1FU)));
            out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        else if (codePoint <= 0xFFFFU)
        {
            // 3 байта: 1110xxxx 10xxxxxx 10xxxxxx
            out.push_back(static_cast<char>(0xE0U | ((codePoint >> 12) & 0x0FU)));
            out.push_back(static_cast<char>(0x80U | ((codePoint >> 6) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        else
        {
            // 4 байта: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            out.push_back(static_cast<char>(0xF0U | ((codePoint >> 18) & 0x07U)));
            out.push_back(static_cast<char>(0x80U | ((codePoint >> 12) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | ((codePoint >> 6) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }

        return true;
    }

    // Декодирует следующую кодовую точку Unicode из UTF-8 строки начиная с позиции offset.
    bool utils::utf8::Utf8DecodeCodePoint(std::string_view text, size_t& offset, char32_t& outCodePoint)
    {
        const size_t n = text.size();
        if (offset >= n)
            return false;

        const uint8_t c0 = static_cast<uint8_t>(text[offset]);

        if (c0 <= 0x7FU)
        {
            outCodePoint = static_cast<char32_t>(c0);
            offset += 1;
            return true;
        }
        else if ((c0 & 0xE0U) == 0xC0U)
        {
            // 2-байтовая последовательность; c0 < 0xC2 — запрещённый overlong.
            if (offset + 1 >= n || c0 < 0xC2U)
                return false;

            const uint8_t c1 = static_cast<uint8_t>(text[offset + 1]);
            if ((c1 & 0xC0U) != 0x80U)
                return false;

            outCodePoint = (static_cast<char32_t>(c0 & 0x1FU) << 6) |
                static_cast<char32_t>(c1 & 0x3FU);
            offset += 2;
            return true;
        }
        else if ((c0 & 0xF0U) == 0xE0U)
        {
            // 3-байтовая последовательность; отсекаем overlong (E0 80..9F) и суррогаты (ED A0..BF).
            if (offset + 2 >= n)
                return false;

            const uint8_t c1 = static_cast<uint8_t>(text[offset + 1]);
            const uint8_t c2 = static_cast<uint8_t>(text[offset + 2]);
            if ((c1 & 0xC0U) != 0x80U || (c2 & 0xC0U) != 0x80U)
                return false;
            if ((c0 == 0xE0U && c1 < 0xA0U) || (c0 == 0xEDU && c1 >= 0xA0U))
                return false;

            outCodePoint = (static_cast<char32_t>(c0 & 0x0FU) << 12) |
                (static_cast<char32_t>(c1 & 0x3FU) << 6) |
                static_cast<char32_t>(c2 & 0x3FU);
            offset += 3;
            return true;
        }
        else if ((c0 & 0xF8U) == 0xF0U)
        {
            // 4-байтовая последовательность; отсекаем overlong (F0 80..8F) и значения > U+10FFFF.
            if (offset + 3 >= n || c0 > 0xF4U)
                return false;

            const uint8_t c1 = static_cast<uint8_t>(text[offset + 1]);
            const uint8_t c2 = static_cast<uint8_t>(text[offset + 2]);
            const uint8_t c3 = static_cast<uint8_t>(text[offset + 3]);
            if ((c1 & 0xC0U) != 0x80U || (c2 & 0xC0U) != 0x80U || (c3 & 0xC0U) != 0x80U)
                return false;
            if ((c0 == 0xF0U && c1 < 0x90U) || (c0 == 0xF4U && c1 > 0x8FU))
                return false;

            outCodePoint = (static_cast<char32_t>(c0 & 0x07U) << 18) |
                (static_cast<char32_t>(c1 & 0x3FU) << 12) |
                (static_cast<char32_t>(c2 & 0x3FU) << 6) |
                static_cast<char32_t>(c3 & 0x3FU);
            offset += 4;
            return true;
        }

        return false;
    }

    // Проверяет, является ли строка корректной последовательностью UTF-8.
    bool utils::utf8::IsValidUtf8(std::string_view text)
    {
        size_t offset = 0;
        char32_t cp = 0;
        while (offset < text.size())
        {
            if (!Utf8DecodeCodePoint(text, offset, cp))
                return false;
        }
        return true;
    }

    // Подсчитывает количество символов (кодовых точек Unicode) в валидной UTF-8 строке.
    size_t utils::utf8::Utf8Length(std::string_view text)
    {
        size_t offset = 0;
        size_t count = 0;
        char32_t cp = 0;
        while (offset < text.size())
        {
            if (!Utf8DecodeCodePoint(text, offset, cp))
                return static_cast<size_t>(-1);
            ++count;
        }
        return count;
    }

    // Кодирует строку UTF-32 (std::u32string_view) в UTF-8 (std::string).
    bool utils::utf8::Utf32ToUtf8(std::u32string_view input, std::string& output)
    {
        output.clear();
        output.reserve(input.size());
        for (char32_t cp : input)
        {
            if (!Utf8EncodeCodePoint(cp, output))
            {
                output.clear();
                return false;
            }
        }
        return true;
    }

    // Декодирует строку UTF-8 (std::string_view) в UTF-32 (std::u32string).
    bool utils::utf8::Utf8ToUtf32(std::string_view input, std::u32string& output)
    {
        output.clear();
        output.reserve(input.size());
        size_t offset = 0;
        char32_t cp = 0;
        while (offset < input.size())
        {
            if (!Utf8DecodeCodePoint(input, offset, cp))
            {
                output.clear();
                return false;
            }
            output.push_back(cp);
        }
        return true;
    }

    // Кодирует строку UTF-16 (std::u16string_view, включая суррогатные пары) в UTF-8.
    bool utils::utf8::Utf16ToUtf8(std::u16string_view input, std::string& output)
    {
        output.clear();
        output.reserve(input.size());

        for (size_t i = 0; i < input.size(); ++i)
        {
            const char16_t unit = input[i];
            char32_t cp = 0;

            if (unit >= 0xD800U && unit <= 0xDBFFU)
            {
                // Старший суррогат: обязан сопровождаться младшим суррогатом (0xDC00..0xDFFF).
                if (i + 1 >= input.size())
                {
                    output.clear();
                    return false;
                }
                const char16_t low = input[++i];
                if (low < 0xDC00U || low > 0xDFFFU)
                {
                    output.clear();
                    return false;
                }
                cp = 0x10000U + ((static_cast<char32_t>(unit - 0xD800U) << 10) |
                    static_cast<char32_t>(low - 0xDC00U));
            }
            else if (unit >= 0xDC00U && unit <= 0xDFFFU)
            {
                // Одиночный младший суррогат недопустим.
                output.clear();
                return false;
            }
            else
            {
                cp = static_cast<char32_t>(unit);
            }

            if (!Utf8EncodeCodePoint(cp, output))
            {
                output.clear();
                return false;
            }
        }
        return true;
    }

    // Декодирует строку UTF-8 (std::string_view) в UTF-16 (std::u16string, с суррогатными парами).
    bool utils::utf8::Utf8ToUtf16(std::string_view input, std::u16string& output)
    {
        output.clear();
        output.reserve(input.size());

        size_t offset = 0;
        char32_t cp = 0;
        while (offset < input.size())
        {
            if (!Utf8DecodeCodePoint(input, offset, cp))
            {
                output.clear();
                return false;
            }

            if (cp <= 0xFFFFU)
            {
                output.push_back(static_cast<char16_t>(cp));
            }
            else
            {
                // Разбиваем кодовую точку > U+FFFF на суррогатную пару UTF-16.
                const char32_t v = cp - 0x10000U;
                output.push_back(static_cast<char16_t>(0xD800U + (v >> 10)));
                output.push_back(static_cast<char16_t>(0xDC00U + (v & 0x3FFU)));
            }
        }
        return true;
    }

    // Кодирует кроссплатформенную широкую строку (std::wstring_view: UTF-16 на Windows, UTF-32 на Linux/macOS) в UTF-8.
    bool utils::utf8::WideToUtf8(std::wstring_view input, std::string& output)
    {
        if constexpr (sizeof(wchar_t) == 2)
        {
            std::u16string_view u16(reinterpret_cast<const char16_t*>(input.data()), input.size());
            return Utf16ToUtf8(u16, output);
        }
        else
        {
            std::u32string_view u32(reinterpret_cast<const char32_t*>(input.data()), input.size());
            return Utf32ToUtf8(u32, output);
        }
    }

    // Декодирует UTF-8 строку в кроссплатформенную std::wstring (UTF-16 на Windows, UTF-32 на Linux/macOS).
    bool utils::utf8::Utf8ToWide(std::string_view input, std::wstring& output)
    {
        output.clear();
        if constexpr (sizeof(wchar_t) == 2)
        {
            std::u16string u16;
            if (!Utf8ToUtf16(input, u16))
                return false;
            output.assign(u16.begin(), u16.end());
            return true;
        }
        else
        {
            std::u32string u32;
            if (!Utf8ToUtf32(input, u32))
                return false;
            output.assign(u32.begin(), u32.end());
            return true;
        }
    }
}

/**
 * @file PrivateBase.hpp
 * @brief Внутренние алфавиты и базовые алгоритмы кодирования и декодирования семейства Base-N.
 *
 * @details
 * Этот заголовок предназначен исключительно для внутреннего использования внутри реализации
 * модуля кодирования и не является частью публичного API библиотеки. Он объединяет стандартные
 * таблицы символов и низкоуровневые механизмы преобразования произвольных бинарных данных
 * в текстовое представление и обратно.
 *
 * Внутри реализованы три основные стратегии преобразования:
 * - побитовая упаковка для систем с основанием, являющимся степенью двойки (например, Base16, Base32, Base64);
 * - позиционный перевод длинной арифметики для произвольных алфавитов с сохранением ведущих нулевых байтов (например, Base58, Base62);
 * - блочное преобразование фиксированных групп байтов для компактных форматов (Base45 и семейство Base85).
 *
 * Все функции при старте и при обнаружении некорректных входных данных очищают выходной буфер,
 * возвращая булев признак успеха без выброса исключений форматирования.
 */

#ifndef LIBOTHER_PRIVATEBASE_HPP
#define LIBOTHER_PRIVATEBASE_HPP

#include "Utils.h"


namespace other::crypt
{
    ///////////////////////////////////////////////////////////////////////
    ///            СТАНДАРТНЫЕ АЛФАВИТЫ КОДИРОВАНИЯ                     ///
    ///////////////////////////////////////////////////////////////////////

    /** @brief Алфавит Base16 (шестнадцатеричный, верхний регистр) — RFC 4648 Section 8. */
    inline constexpr const char* BASE16_MAP           = "0123456789ABCDEF";
    /** @brief Алфавит Base26 (латинские заглавные буквы A–Z). */
    inline constexpr const char* BASE26_MAP           = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    /** @brief Алфавит Base32 — RFC 4648 Section 6. */
    inline constexpr const char* BASE32_MAP           = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    /** @brief Алфавит Base32Hex (с расширенным шестнадцатеричным порядком) — RFC 4648 Section 7. */
    inline constexpr const char* BASE32_HEX_MAP       = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
    /** @brief Алфавит Base32 Crockford (исключены визуально похожие символы I, L, O, U). */
    inline constexpr const char* BASE32_CROCKFORD_MAP = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    /** @brief Алфавит Base36 (цифры и заглавные латинские буквы, без учёта регистра). */
    inline constexpr const char* BASE36_MAP           = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    /** @brief Алфавит Base45 (компактное представление для QR-кодов) — RFC 9285. */
    inline constexpr const char* BASE45_MAP           = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    /** @brief Алфавит Base58 Bitcoin (без 0, O, I, l для исключения ошибок чтения человеком). */
    inline constexpr const char* BASE58_BITCOIN       = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    /** @brief Алфавит Base58 Flickr (сначала строчные буквы, затем заглавные). */
    inline constexpr const char* BASE58_FLICKR        = "123456789abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ";
    /** @brief Алфавит Base58 Ripple (специализированный порядок символов протокола Ripple). */
    inline constexpr const char* BASE58_RIPPLE        = "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";
    /** @brief Алфавит Base62 (цифры, заглавные и строчные латинские буквы, регистрозависимый). */
    inline constexpr const char* BASE62_MAP           = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    /** @brief Алфавит Base64 (стандартный) — RFC 4648 Section 4. */
    inline constexpr const char* BASE64_MAP           = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    /** @brief Алфавит Base64URL (безопасный для URL и имён файлов) — RFC 4648 Section 5. */
    inline constexpr const char* BASE64_URL_MAP       = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    /** @brief Алфавит Base85 Math (математический / совместимый с Z85 набор символов). */
    inline constexpr const char* BASE85_MATH_MAP      = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";
    /** @brief Алфавит Base85 Z85 (стандарт ZeroMQ). */
    inline constexpr const char* BASE85_Z85_MAP       = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";
    /** @brief Алфавит Base85 ASCII85 (Adobe, печатные символы ASCII от '!' до 'u'). */
    inline constexpr const char* BASE85_ASCII85_MAP   = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstu";


    ///////////////////////////////////////////////////////////////////////
    ///            ВНУТРЕННИЕ ФУНКЦИИ КОДИРОВАНИЯ И ДЕКОДИРОВАНИЯ       ///
    ///////////////////////////////////////////////////////////////////////

    /**
     * @brief Выполняет побитовое кодирование массива байтов в текстовое представление (Base16, Base32, Base64 и аналоги).
     *
     * Разбивает поток входных битов на группы по `bits` бит и сопоставляет каждой группе символ из `alphabet`.
     * Для `bits == 5` (Base32) и `bits == 6` (Base64) автоматически дополняет результат символами `'='`
     * до кратности 8 и 4 символов соответственно.
     * @param [in]  input    Исходный массив байтов для кодирования (если пуст — возвращает `true` и пустой `output`).
     * @param [out] output   Выходная закодированная строка (предварительно очищается).
     * @param [in]  alphabet Алфавит кодирования длиной не менее `2^bits` символов.
     * @param [in]  bits     Количество бит на один выходной символ (допустимый диапазон `1..8`, например `4`, `5`, `6`).
     * @return `true` при успешном кодировании; `false`, если `bits` вне диапазона `1..8` или алфавит слишком короток.
     * @throws std::bad_alloc При нехватке памяти для расширения строки `output`.
     */
    inline bool InternalEncodeBitwise(const std::vector<uint8_t>& input, std::string& output, const std::string& alphabet, int bits)
    {
        output.clear();

        if (bits < 1 || bits > 8 || alphabet.size() < (size_t(1) << bits))
            return false;

        if (input.empty())
            return true;

        const uint32_t mask = (1U << bits) - 1U;
        const size_t totalBits = input.size() * 8;
        size_t charCount = (totalBits + static_cast<size_t>(bits) - 1) / static_cast<size_t>(bits);

        const size_t padding = (bits == 6) ? 4 : (bits == 5 ? 8 : 1);
        if (padding > 1 && (charCount % padding) != 0)
        {
            charCount += (padding - (charCount % padding));
        }

        output.reserve(charCount);

        uint32_t buffer = 0;
        int bufferBits = 0;

        for (uint8_t byte : input)
        {
            buffer = (buffer << 8) | byte;
            bufferBits += 8;

            while (bufferBits >= bits)
            {
                bufferBits -= bits;
                output.push_back(alphabet[(buffer >> bufferBits) & mask]);
            }
        }

        if (bufferBits > 0)
        {
            output.push_back(alphabet[(buffer << (bits - bufferBits)) & mask]);
        }

        if (padding > 1)
        {
            while (output.size() % padding != 0)
                output.push_back('=');
        }

        return true;
    }

    /**
     * @brief Выполняет побитовое декодирование строки обратно в массив байтов (Base16, Base32, Base64 и аналоги).
     *
     * Игнорирует стандартные пробельные символы (`std::isspace`), проверяет корректность символов дополнения `'='`
     * (допустимы только в конце строки для `bits == 5` и `bits == 6`) и валидирует нулевые биты выравнивания в хвосте.
     * @param [in]  input    Входная закодированная строка (если пуста — возвращает `true` и пустой `output`).
     * @param [out] output   Выходной массив декодированных байтов (очищается в начале вызова и при любой ошибке).
     * @param [in]  alphabet Алфавит декодирования длиной не менее `2^bits` символов.
     * @param [in]  bits     Количество бит на один входной символ (допустимый диапазон `1..8`).
     * @return `true` при успешном декодировании; `false` при неверных параметрах, неизвестных символах,
     *         некорректном расположении `'='` или невалидном битовом хвосте.
     * @throws std::bad_alloc При нехватке памяти для расширения вектора `output`.
     */
    inline bool InternalDecodeBitwise(const std::string& input, std::vector<uint8_t>& output, const std::string& alphabet, int bits)
    {
        output.clear();

        if (bits < 1 || bits > 8 || alphabet.size() < (size_t(1) << bits))
            return false;

        if (input.empty())
            return true;

        int8_t map[256];
        other::utils::BuildInverseMap(alphabet, alphabet.size(), map);

        uint32_t buffer = 0;
        int bufferBits = 0;
        bool seenPadding = false;

        for (unsigned char byte : input)
        {
            // Пропускаем только стандартные пробельные символы (не произвольные управляющие байты).
            if (std::isspace(byte))
                continue;

            if (byte == '=')
            {
                if (bits != 5 && bits != 6)
                {
                    output.clear();
                    return false;
                }
                seenPadding = true;
                continue;
            }

            // Данные после знака '=' недопустимы.
            if (seenPadding)
            {
                output.clear();
                return false;
            }

            const int8_t val = map[byte];
            if (val == -1)
            {
                output.clear();
                return false;
            }

            buffer = (buffer << bits) | static_cast<uint32_t>(val);
            bufferBits += bits;

            if (bufferBits >= 8)
            {
                bufferBits -= 8;
                output.push_back(static_cast<uint8_t>((buffer >> bufferBits) & 0xFFU));
            }
        }

        // Проверяем валидность хвоста:
        // 1) bufferBits >= bits означает невозможную длину (напр. 1 символ Base16/Base32/Base64).
        // 2) Оставшиеся биты дополнения должны быть нулевыми.
        if (bufferBits >= bits || (bufferBits > 0 && (buffer & ((1U << bufferBits) - 1U)) != 0))
        {
            output.clear();
            return false;
        }

        return true;
    }

    /**
     * @brief Кодирует массив байтов через перевод большого целого числа в позиционную систему счисления по основанию `alphabet.size()`.
     *
     * Используется для кодировок с основанием, не являющимся степенью двойки (например, Base26, Base36, Base58, Base62).
     * Каждый ведущий нулевой байт `0x00` на входе сохраняется в начале результата как символ `alphabet[0]`.
     * @param [in]  input    Исходный массив байтов (если пуст — возвращает `true` и пустой `output`).
     * @param [out] output   Выходная закодированная строка (предварительно очищается).
     * @param [in]  alphabet Алфавит кодирования (должен содержать не менее `2` символов).
     * @return `true` при успехе; `false`, если длина `alphabet` меньше `2`.
     * @note Временная сложность алгоритма составляет $O(N^2)$ от числа входных байтов.
     * @throws std::bad_alloc При нехватке памяти для промежуточного буфера или строки `output`.
     */
    inline bool InternalEncodeBigInt(const std::vector<uint8_t>& input, std::string& output, const std::string& alphabet)
    {
        output.clear();

        if (alphabet.size() < 2)
            return false;

        if (input.empty())
            return true;

        const int base = static_cast<int>(alphabet.size());

        size_t zeroCount = 0;
        while (zeroCount < input.size() && input[zeroCount] == 0)
        {
            zeroCount++;
        }

        std::vector<uint8_t> buffer((input.size() - zeroCount) * 2 + 1, 0);

        for (size_t c = zeroCount; c < input.size(); c++)
        {
            int carry = input[c];
            for (size_t b = buffer.size(); b > 0; b--)
            {
                carry += 256 * buffer[b - 1];
                buffer[b - 1] = static_cast<uint8_t>(carry % base);
                carry /= base;
            }
        }

        auto it = std::find_if(buffer.begin(), buffer.end(), [](uint8_t n) { return n != 0; });
        output.reserve(zeroCount + static_cast<size_t>(std::distance(it, buffer.end())));

        for (size_t i = 0; i < zeroCount; i++)
            output.push_back(alphabet[0]);

        for (; it != buffer.end(); ++it)
            output.push_back(alphabet[*it]);

        return true;
    }

    /**
     * @brief Декодирует строку из произвольной позиционной системы счисления обратно в массив байтов.
     *
     * Каждый ведущий символ `alphabet[0]` преобразуется обратно в ведущий нулевой байт `0x00`.
     * @param [in]  input    Входная закодированная строка (если пуста — возвращает `true` и пустой `output`).
     * @param [out] output   Выходной массив байтов (очищается в начале вызова и при ошибке).
     * @param [in]  alphabet Алфавит декодирования (должен содержать не менее `2` символов).
     * @return `true` при успешном декодировании; `false`, если длина алфавита меньше `2` либо во входной строке
     *         встретился символ, отсутствующий в `alphabet`.
     * @note Временная сложность составляет $O(N^2)$ от длины входной строки.
     * @throws std::bad_alloc При нехватке памяти для промежуточного буфера или вектора `output`.
     */
    inline bool InternalDecodeBigInt(const std::string& input, std::vector<uint8_t>& output, const std::string& alphabet)
    {
        output.clear();

        if (alphabet.size() < 2)
            return false;

        if (input.empty())
            return true;

        int8_t map[256];
        other::utils::BuildInverseMap(alphabet, alphabet.size(), map);

        const int base = static_cast<int>(alphabet.size());

        size_t zeroCount = 0;
        while (zeroCount < input.size() && input[zeroCount] == alphabet[0])
        {
            zeroCount++;
        }

        std::vector<uint8_t> bytes(input.size(), 0);

        for (size_t c = zeroCount; c < input.size(); c++)
        {
            int carry = map[static_cast<uint8_t>(input[c])];
            if (carry == -1)
            {
                output.clear();
                return false;
            }

            for (size_t b = bytes.size(); b > 0; b--)
            {
                int temp = carry + base * bytes[b - 1];
                bytes[b - 1] = static_cast<uint8_t>(temp % 256);
                carry = temp / 256;
            }
        }

        auto it = std::find_if(bytes.begin(), bytes.end(), [](uint8_t n) { return n != 0; });
        output.assign(zeroCount, 0x00);
        output.insert(output.end(), it, bytes.end());
        return true;
    }

    /**
     * @brief Выполняет блочное кодирование Base45 по спецификации RFC 9285.
     *
     * Каждые 2 входных байта (16-битное число в Big-Endian) кодируются в 3 символа по основанию 45
     * (от младшего разряда к старшему); одиночный оставшийся байт кодируется в 2 символа.
     * @param [in]  input    Исходный массив байтов (если пуст — возвращает `true` и пустой `output`).
     * @param [out] output   Выходная закодированная строка Base45 (предварительно очищается).
     * @param [in]  alphabet Алфавит Base45 длиной не менее `45` символов.
     * @return `true` при успехе; `false`, если длина `alphabet` меньше `45`.
     * @throws std::bad_alloc При нехватке памяти для расширения строки `output`.
     */
    inline bool InternalEncodeBlock45(const std::vector<uint8_t>& input, std::string& output, const std::string& alphabet)
    {
        output.clear();

        if (alphabet.size() < 45)
            return false;

        if (input.empty())
            return true;

        output.reserve((input.size() / 2) * 3 + (input.size() % 2) * 2);

        size_t i = 0;
        const size_t n = input.size();

        for (; i + 1 < n; i += 2)
        {
            uint32_t val = (static_cast<uint32_t>(input[i]) << 8) | static_cast<uint32_t>(input[i + 1]);
            output.push_back(alphabet[val % 45]);
            output.push_back(alphabet[(val / 45) % 45]);
            output.push_back(alphabet[val / 2025]);
        }

        if (i < n)
        {
            uint32_t val = static_cast<uint32_t>(input[i]);
            output.push_back(alphabet[val % 45]);
            output.push_back(alphabet[val / 45]);
        }

        return true;
    }

    /**
     * @brief Выполняет блочное декодирование строки Base45 по спецификации RFC 9285.
     *
     * Преобразует группы по 3 символа обратно в 2 байта, а финальную группу из 2 символов — в 1 байт,
     * контролируя переполнение значений (`> 0xFFFF` для 3 символов и `> 0xFF` для 2 символов).
     * @param [in]  input    Входная закодированная строка Base45 (если пуста — возвращает `true` и пустой `output`).
     * @param [out] output   Выходной массив декодированных байтов (очищается в начале вызова и при ошибке).
     * @param [in]  alphabet Алфавит Base45 длиной не менее `45` символов.
     * @return `true` при успешном декодировании; `false` при коротком алфавите, одиночном неполном символе в хвосте,
     *         неизвестном символе или переполнении декодированного блока.
     * @throws std::bad_alloc При нехватке памяти для расширения вектора `output`.
     */
    inline bool InternalDecodeBlock45(const std::string& input, std::vector<uint8_t>& output, const std::string& alphabet)
    {
        output.clear();

        if (alphabet.size() < 45)
            return false;

        if (input.empty())
            return true;

        int8_t map[256];
        other::utils::BuildInverseMap(alphabet, alphabet.size(), map);

        size_t i = 0;
        while (i < input.size())
        {
            const size_t remaining = input.size() - i;
            if (remaining < 2)
            {
                output.clear();
                return false;
            }

            const size_t chunkSize = (remaining >= 3) ? 3 : 2;
            uint32_t val = 0;
            uint32_t multiplier = 1;

            for (size_t j = 0; j < chunkSize; ++j)
            {
                const int v = map[static_cast<uint8_t>(input[i + j])];
                if (v == -1)
                {
                    output.clear();
                    return false;
                }

                val += static_cast<uint32_t>(v) * multiplier;
                multiplier *= 45;
            }

            i += chunkSize;

            if (chunkSize == 3)
            {
                if (val > 0xFFFFU)
                {
                    output.clear();
                    return false;
                }
                output.push_back(static_cast<uint8_t>((val >> 8) & 0xFFU));
                output.push_back(static_cast<uint8_t>(val & 0xFFU));
            }
            else
            {
                if (val > 0xFFU)
                {
                    output.clear();
                    return false;
                }
                output.push_back(static_cast<uint8_t>(val & 0xFFU));
            }
        }
        return true;
    }

    /**
     * @brief Выполняет блочное кодирование семейства Base85 (ASCII85 / Z85).
     *
     * Упаковывает каждые 4 входных байта (32-битное число в Big-Endian) в 5 символов по основанию 85.
     * Неполный финальный блок длиной `k` байт (`1..3`) дополняется нулями справа и записывается как `k + 1` символ.
     * В режиме ASCII85 полный нулевой 4-байтный блок сокращается до одного символа `'z'`.
     * @param [in]  input     Исходный массив байтов (если пуст — возвращает `true` и пустой `output`).
     * @param [out] output    Выходная закодированная строка Base85 (предварительно очищается).
     * @param [in]  alphabet  Алфавит Base85 длиной не менее `85` символов.
     * @param [in]  isAscii85 Если `true` — включает сжатие нулевых 4-байтных блоков в `'z'` (формат Adobe ASCII85);
     *                        если `false` — кодирует без сокращения `'z'` (формат Z85 / Math).
     * @return `true` при успехе; `false`, если длина `alphabet` меньше `85`.
     * @throws std::bad_alloc При нехватке памяти для расширения строки `output`.
     */
    inline bool InternalEncodeBlock85(const std::vector<uint8_t>& input, std::string& output, const std::string& alphabet, bool isAscii85)
    {
        output.clear();

        if (alphabet.size() < 85)
            return false;

        if (input.empty())
            return true;

        const size_t n = input.size();
        output.reserve((n / 4) * 5 + 5);

        size_t i = 0;
        for (; i + 4 <= n; i += 4)
        {
            uint32_t val = (static_cast<uint32_t>(input[i])     << 24) |
                           (static_cast<uint32_t>(input[i + 1]) << 16) |
                           (static_cast<uint32_t>(input[i + 2]) << 8)  |
                            static_cast<uint32_t>(input[i + 3]);

            if (isAscii85 && val == 0)
            {
                output.push_back('z');
                continue;
            }

            char block[5];
            for (int j = 4; j >= 0; --j)
            {
                block[j] = alphabet[val % 85];
                val /= 85;
            }
            output.append(block, 5);
        }

        if (i < n)
        {
            const size_t remaining = n - i;
            uint32_t val = 0;

            for (size_t j = 0; j < 4; ++j)
            {
                val <<= 8;
                if (j < remaining)
                    val |= static_cast<uint32_t>(input[i + j]);
            }

            char block[5];
            for (int j = 4; j >= 0; --j)
            {
                block[j] = alphabet[val % 85];
                val /= 85;
            }

            output.append(block, remaining + 1);
        }

        return true;
    }

    /**
     * @brief Выполняет блочное декодирование строки семейства Base85 (ASCII85 / Z85).
     *
     * Игнорирует стандартные пробельные символы (`std::isspace`), поддерживает распаковку `'z'` в четыре
     * нулевых байта в режиме ASCII85 и проверяет переполнение 32-битного значения (`> 0xFFFFFFFF`).
     * @param [in]  input     Входная закодированная строка Base85 (если пуста — возвращает `true` и пустой `output`).
     * @param [out] output    Выходной массив декодированных байтов (очищается в начале вызова и при ошибке).
     * @param [in]  alphabet  Алфавит Base85 длиной не менее `85` символов.
     * @param [in]  isAscii85 Если `true` — распознаёт символ `'z'` как сокращение для четырёх нулевых байтов (ASCII85);
     *                        если `false` — обрабатывает `'z'` исключительно как обычный символ алфавита (Z85 / Math).
     * @return `true` при успешном декодировании; `false` при коротком алфавите, недопустимом символе,
     *         переполнении 32-битного блока или одиночном висячем символе в конце потока.
     * @throws std::bad_alloc При нехватке памяти для расширения вектора `output`.
     */
    inline bool InternalDecodeBlock85(const std::string& input, std::vector<uint8_t>& output, const std::string& alphabet, bool isAscii85)
    {
        output.clear();

        if (alphabet.size() < 85)
            return false;

        if (input.empty())
            return true;

        int8_t map[256];
        other::utils::BuildInverseMap(alphabet, alphabet.size(), map);

        size_t i = 0;
        while (i < input.size())
        {
            if (std::isspace(static_cast<unsigned char>(input[i])))
            {
                i++;
                continue;
            }

            if (isAscii85 && input[i] == 'z')
            {
                for (int j = 0; j < 4; ++j)
                    output.push_back(0);
                i++;
                continue;
            }

            // Используем uint64_t для обнаружения переполнения (> 0xFFFFFFFF),
            // т.к. 85^5 - 1 = 4,437,053,124 > UINT32_MAX.
            uint64_t val = 0;
            int charsInBlock = 0;

            while (charsInBlock < 5 && i < input.size())
            {
                const unsigned char curr = static_cast<unsigned char>(input[i++]);
                if (std::isspace(curr))
                    continue;

                const int v = map[curr];
                if (v == -1)
                {
                    output.clear();
                    return false;
                }

                val = val * 85ULL + static_cast<uint64_t>(v);
                charsInBlock++;
            }

            if (charsInBlock == 5)
            {
                if (val > 0xFFFFFFFFULL)
                {
                    output.clear();
                    return false;
                }

                output.push_back(static_cast<uint8_t>((val >> 24) & 0xFFULL));
                output.push_back(static_cast<uint8_t>((val >> 16) & 0xFFULL));
                output.push_back(static_cast<uint8_t>((val >> 8)  & 0xFFULL));
                output.push_back(static_cast<uint8_t>(val & 0xFFULL));
            }
            else if (charsInBlock >= 2)
            {
                const int padding = 5 - charsInBlock;
                for (int j = 0; j < padding; ++j)
                {
                    val = val * 85ULL + 84ULL;
                }

                if (val > 0xFFFFFFFFULL)
                {
                    output.clear();
                    return false;
                }

                const int bytesToExtract = charsInBlock - 1;
                for (int j = 0; j < bytesToExtract; ++j)
                {
                    output.push_back(static_cast<uint8_t>((val >> (24 - j * 8)) & 0xFFULL));
                }
            }
            else if (charsInBlock == 1)
            {
                // Одиночный символ в конце блока Base85 недопустим.
                output.clear();
                return false;
            }
        }
        return true;
    }
}

#endif // LIBOTHER_PRIVATEBASE_HPP

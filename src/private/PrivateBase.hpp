#ifndef LIBOTHER_PRIVATEBASE_HPP
#define LIBOTHER_PRIVATEBASE_HPP

#include "Utils.h"

namespace other::crypt
{
    /** BASE-16 (Hexadecimal) — RFC 4648 Section 8 */
    inline constexpr const char* BASE16_MAP           = "0123456789ABCDEF";
    /** BASE-26 (Alphabetic) */
    inline constexpr const char* BASE26_MAP           = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    /** BASE-32 — RFC 4648 Section 6 */
    inline constexpr const char* BASE32_MAP           = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    /** BASE-32-HEX — RFC 4648 Section 7 */
    inline constexpr const char* BASE32_HEX_MAP       = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
    /** BASE-32-CROCKFORD (без I, L, O) */
    inline constexpr const char* BASE32_CROCKFORD_MAP = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    /** BASE-36 (Alphanumeric) */
    inline constexpr const char* BASE36_MAP           = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    /** BASE-45 (RFC 9285, QR-коды) */
    inline constexpr const char* BASE45_MAP           = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    /** BASE-58-BITCOIN */
    inline constexpr const char* BASE58_BITCOIN       = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    /** BASE-58-FLICKR */
    inline constexpr const char* BASE58_FLICKR        = "123456789abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ";
    /** BASE-58-RIPPLE */
    inline constexpr const char* BASE58_RIPPLE        = "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";
    /** BASE-62 (Alphanumeric Case-Sensitive) */
    inline constexpr const char* BASE62_MAP           = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    /** BASE-64 — RFC 4648 Section 4 */
    inline constexpr const char* BASE64_MAP           = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    /** BASE-64-URL — RFC 4648 Section 5 */
    inline constexpr const char* BASE64_URL_MAP       = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    /** BASE-85-MATH */
    inline constexpr const char* BASE85_MATH_MAP      = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";
    /** BASE-85-Z85 (ZeroMQ) */
    inline constexpr const char* BASE85_Z85_MAP       = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";
    /** BASE-85-ASCII85 (Adobe) */
    inline constexpr const char* BASE85_ASCII85_MAP   = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstu";

    /**
     * @brief Универсальное побитовое кодирование (Base16, Base32, Base64).
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
     * @brief Универсальное побитовое декодирование (Base16, Base32, Base64).
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
     * @brief Кодирование через перевод системы счисления (BigInt).
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
     * @brief Декодирование через перевод системы счисления (BigInt).
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
     * @brief Блочное кодирование Base45 (RFC 9285).
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
     * @brief Блочное декодирование Base45 (RFC 9285).
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
     * @brief Блочное кодирование Base85 (ASCII85 / Z85).
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
     * @brief Блочное декодирование Base85 (ASCII85 / Z85).
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

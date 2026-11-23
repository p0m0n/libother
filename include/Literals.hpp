#ifndef LIBOTHER_LITERALS_HPP
#define LIBOTHER_LITERALS_HPP

#include "DateTime.h"
#include "Base.h"


namespace other::literals
{
    namespace detail
    {
        inline std::string EncodeLiteral(const char* str, std::size_t len, crypt::base::BaseType type)
        {
            const auto* bytes = reinterpret_cast<const uint8_t*>(str);
            std::vector<uint8_t> input(bytes, bytes + len);
            std::string result;
            crypt::base::Encode(input, result, type);
            return result;
        }

        inline std::vector<uint8_t> DecodeLiteral(const char* str, std::size_t len, crypt::base::BaseType type)
        {
            std::string input(str, len);
            std::vector<uint8_t> result;
            crypt::base::Decode(input, result, type);
            return result;
        }
    }

    // --- DateTime литералы (поддерживают пустую строку для формата "G" или произвольный формат) ---
    inline std::string operator""_dnow(const char* str, std::size_t len)
    {
        return time::DateTime::Now().ToString(std::string(str, len));
    }

    inline std::string operator""_dutc(const char* str, std::size_t len)
    {
        return time::DateTime::UtcNow().ToString(std::string(str, len));
    }

    // --- Base литералы ---
    inline std::string operator""_bts(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE_BINARY);
    }

    inline std::vector<uint8_t> operator""_stb(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE_BINARY);
    }

    inline std::string operator""_b16e(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE16);
    }

    inline std::vector<uint8_t> operator""_b16d(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE16);
    }

    inline std::string operator""_b26e(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE26);
    }

    inline std::vector<uint8_t> operator""_b26d(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE26);
    }

    inline std::string operator""_b32e(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE32);
    }

    inline std::vector<uint8_t> operator""_b32d(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE32);
    }

    inline std::string operator""_b32e_hex(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE32_HEX);
    }

    inline std::vector<uint8_t> operator""_b32d_hex(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE32_HEX);
    }

    inline std::string operator""_b32e_crockford(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE32_CROCKFORD);
    }

    inline std::vector<uint8_t> operator""_b32d_crockford(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE32_CROCKFORD);
    }

    inline std::string operator""_b36e(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE36);
    }

    inline std::vector<uint8_t> operator""_b36d(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE36);
    }

    inline std::string operator""_b45e(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE45);
    }

    inline std::vector<uint8_t> operator""_b45d(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE45);
    }

    inline std::string operator""_b58e_btc(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE58_BITCOIN);
    }

    inline std::vector<uint8_t> operator""_b58d_btc(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE58_BITCOIN);
    }

    inline std::string operator""_b58e_flickr(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE58_FLICKR);
    }

    inline std::vector<uint8_t> operator""_b58d_flickr(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE58_FLICKR);
    }

    inline std::string operator""_b58e_ripple(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE58_RIPPLE);
    }

    inline std::vector<uint8_t> operator""_b58d_ripple(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE58_RIPPLE);
    }

    inline std::string operator""_b62e(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE62);
    }

    inline std::vector<uint8_t> operator""_b62d(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE62);
    }

    inline std::string operator""_b64e(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE64);
    }

    inline std::vector<uint8_t> operator""_b64d(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE64);
    }

    inline std::string operator""_b64e_url(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE64_URL);
    }

    inline std::vector<uint8_t> operator""_b64d_url(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE64_URL);
    }

    inline std::string operator""_b85e_math(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE85_MATH);
    }

    inline std::vector<uint8_t> operator""_b85d_math(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE85_MATH);
    }

    inline std::string operator""_b85e_z85(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE85_Z85);
    }

    inline std::vector<uint8_t> operator""_b85d_z85(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE85_Z85);
    }

    inline std::string operator""_b85e_ascii85(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE85_ASCII85);
    }

    inline std::vector<uint8_t> operator""_b85d_ascii85(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE85_ASCII85);
    }

    inline std::string operator""_b85e_pdf(const char* str, std::size_t len)
    {
        return detail::EncodeLiteral(str, len, crypt::base::BaseType::BASE85_PDF);
    }

    inline std::vector<uint8_t> operator""_b85d_pdf(const char* str, std::size_t len)
    {
        return detail::DecodeLiteral(str, len, crypt::base::BaseType::BASE85_PDF);
    }
}

#endif // LIBOTHER_LITERALS_HPP

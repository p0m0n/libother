#include "Base.h"
#include "private/PrivateBase.hpp"


namespace other::crypt
{
    bool base::Encode(const std::vector<uint8_t>& input, std::string& output, const BaseType& typeId)
    {
        switch (typeId)
        {
        case BaseType::BASE_BINARY:
            output.assign(input.begin(), input.end());
            return true;

        case BaseType::BASE16:             return InternalEncodeBitwise(input, output, BASE16_MAP, 4);
        case BaseType::BASE26:             return InternalEncodeBigInt (input, output, BASE26_MAP);
        case BaseType::BASE32:             return InternalEncodeBitwise(input, output, BASE32_MAP, 5);
        case BaseType::BASE32_HEX:         return InternalEncodeBitwise(input, output, BASE32_HEX_MAP, 5);
        case BaseType::BASE32_CROCKFORD:   return InternalEncodeBitwise(input, output, BASE32_CROCKFORD_MAP, 5);
        case BaseType::BASE36:             return InternalEncodeBigInt (input, output, BASE36_MAP);
        case BaseType::BASE45:             return InternalEncodeBlock45(input, output, BASE45_MAP);
        case BaseType::BASE58_FLICKR:      return InternalEncodeBigInt (input, output, BASE58_FLICKR);
        case BaseType::BASE58_RIPPLE:      return InternalEncodeBigInt (input, output, BASE58_RIPPLE);
        case BaseType::BASE58_BITCOIN:     return InternalEncodeBigInt (input, output, BASE58_BITCOIN);
        case BaseType::BASE62:             return InternalEncodeBigInt (input, output, BASE62_MAP);
        case BaseType::BASE64:             return InternalEncodeBitwise(input, output, BASE64_MAP, 6);
        case BaseType::BASE64_URL:         return InternalEncodeBitwise(input, output, BASE64_URL_MAP, 6);
        case BaseType::BASE85_MATH:        return InternalEncodeBigInt (input, output, BASE85_MATH_MAP);
        case BaseType::BASE85_Z85:         return InternalEncodeBlock85(input, output, BASE85_Z85_MAP, false);
        case BaseType::BASE85_ASCII85:     return InternalEncodeBlock85(input, output, BASE85_ASCII85_MAP, true);
        case BaseType::BASE85_PDF:
        {
            std::string temp;
            if (InternalEncodeBlock85(input, temp, BASE85_ASCII85_MAP, true))
            {
                output = "<~" + temp + "~>";
                return true;
            }
            output.clear();
            return false;
        }
        case BaseType::BASE_NULL:
        default:
            output.clear();
            return false;
        }
    }

    bool base::Decode(const std::string& input, std::vector<uint8_t>& output, const BaseType& typeId)
    {
        switch (typeId)
        {
        case BaseType::BASE_BINARY:
            output.assign(input.begin(), input.end());
            return true;

        case BaseType::BASE16:             return InternalDecodeBitwise(input, output, BASE16_MAP, 4);
        case BaseType::BASE26:             return InternalDecodeBigInt (input, output, BASE26_MAP);
        case BaseType::BASE32:             return InternalDecodeBitwise(input, output, BASE32_MAP, 5);
        case BaseType::BASE32_HEX:         return InternalDecodeBitwise(input, output, BASE32_HEX_MAP, 5);
        case BaseType::BASE32_CROCKFORD:   return InternalDecodeBitwise(input, output, BASE32_CROCKFORD_MAP, 5);
        case BaseType::BASE36:             return InternalDecodeBigInt (input, output, BASE36_MAP);
        case BaseType::BASE45:             return InternalDecodeBlock45(input, output, BASE45_MAP);
        case BaseType::BASE58_FLICKR:      return InternalDecodeBigInt (input, output, BASE58_FLICKR);
        case BaseType::BASE58_RIPPLE:      return InternalDecodeBigInt (input, output, BASE58_RIPPLE);
        case BaseType::BASE58_BITCOIN:     return InternalDecodeBigInt (input, output, BASE58_BITCOIN);
        case BaseType::BASE62:             return InternalDecodeBigInt (input, output, BASE62_MAP);
        case BaseType::BASE64:             return InternalDecodeBitwise(input, output, BASE64_MAP, 6);
        case BaseType::BASE64_URL:         return InternalDecodeBitwise(input, output, BASE64_URL_MAP, 6);
        case BaseType::BASE85_MATH:        return InternalDecodeBigInt (input, output, BASE85_MATH_MAP);
        case BaseType::BASE85_Z85:         return InternalDecodeBlock85(input, output, BASE85_Z85_MAP, false);
        case BaseType::BASE85_ASCII85:     return InternalDecodeBlock85(input, output, BASE85_ASCII85_MAP, true);
        case BaseType::BASE85_PDF:
        {
            size_t startPos = input.find("<~");
            if (startPos != std::string::npos)
            {
                std::string_view cleanInput = input;
                size_t endPos = cleanInput.rfind("~>");

                startPos += 2; // Пропускаем "<~"

                if (endPos != std::string::npos && endPos >= startPos)
                {
                    cleanInput = cleanInput.substr(startPos, endPos - startPos);
                }
                else
                {
                    cleanInput = cleanInput.substr(startPos);
                }

                return InternalDecodeBlock85(std::string(cleanInput), output, BASE85_ASCII85_MAP, true);
            }

            return InternalDecodeBlock85(input, output, BASE85_ASCII85_MAP, true);
        }
        case BaseType::BASE_NULL:
        default:
            output.clear();
            return false;
        }
    }
}

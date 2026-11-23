#ifndef LIBOTHER_CRYPT_BASE_H
#define LIBOTHER_CRYPT_BASE_H

#include "Define.h"


namespace other::crypt::base
{
    /**
     * @brief Перечисление поддерживаемых алгоритмов кодирования.
     * Разделено на три категории: Битовые (RFC), Математические (BigInt) и Специализированные.
     */
    enum class BaseType
    {
        BASE_NULL             = 0,
        // binary to string else string to binary
        BASE_BINARY           = 1,

        ///--- БИТОВЫЕ ОПЕРАЦИИ ---///

        /// Стандартный Base16 (Hexadecimal). Классическое 16-ричное представление.
        BASE16                = 16,

        /// Стандартный Base32. Группирует по 5 бит. Безопасен для регистронезависимых систем.
        BASE32                = 32,

        /// Base32 Extended Hex Alphabet. Сохраняет естественный порядок сортировки данных.
        BASE32_HEX            = 33,

        /// Самый популярный стандарт кодирования бинарных данных. Группирует по 6 бит.
        BASE64                = 64,

        /// URL-Safe Base64. Символы '+/' заменены на '-_'.
        BASE64_URL            = 65,

        ///--- МАТЕМАТИЧЕСКИЕ ОПЕРАЦИИ ---///

        /// Алфавитная база (A-Z).
        BASE26                = 26,

        /// Буквенно-цифровая база без учета регистра.
        BASE36                = 36,

        /// Алфавит Flickr. Один из первых вариантов исключения похожих символов.
        BASE58_FLICKR         = 58,

        /// Вариант Base58 для платежных систем.
        BASE58_RIPPLE         = 59,

        /// Стандарт Сатоши Накамото. Исключены 0/O, I/l для исключения ошибок ручного ввода.
        BASE58_BITCOIN        = 60,

        /// Максимально компактная база для BigInt без спецсимволов.
        BASE62                = 62,

        /// Математический Base85. Кодирует данные как сверхбольшое число.
        BASE85_MATH           = 85,

        ///--- СПЕЦИАЛИЗИРОВАННЫЕ ОПЕРАЦИИ ---///

        /// Человеко-ориентированный Base32. Исключены неоднозначные символы.
        BASE32_CROCKFORD      = 34,

        /// Кодирование для QR-кодов. Использует алфавит из 45 символов.
        BASE45                = 45,

        /// Блочный Z85 (ZeroMQ). Оптимизирован для сетевых протоколов.
        BASE85_Z85            = 86,

        /// Классический Adobe ASCII85. Поддерживает эффективное сжатие нулевых сегментов ('z').
        BASE85_ASCII85        = 87,

        /// Тот же BASE85_ASCII85, но с учетом специальных маркеров <~ ... ~>.
        BASE85_PDF            = 88,
    };

    bool Encode(const std::vector<uint8_t>& input, std::string& output, const BaseType& typeId = BaseType::BASE_NULL);
    bool Decode(const std::string& input, std::vector<uint8_t>& output, const BaseType& typeId = BaseType::BASE_NULL);
}

#endif // LIBOTHER_CRYPT_BASE_H

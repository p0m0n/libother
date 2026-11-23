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
}

#endif // LIBOTHER_UTILS_H

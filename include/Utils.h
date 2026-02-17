#ifndef _OTHER_UTILS_H_
#define _OTHER_UTILS_H_

#include "Define.h"


namespace other::utils
{
	/**
	 * @brief Создает таблицу быстрого поиска (Lookup Table).
	 * Возвращает фиксированный массив из 256 элементов.
	 */
	static void BuildInverseMap(const std::string& input, size_t size, int8_t* result);

}

#endif

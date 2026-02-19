#ifndef _OTHER_LITERALS_H_
#define _OTHER_LITERALS_H_

#include "DateTime.h"






namespace other::literals
{
	inline std::string operator ""_dnow(const char* str, std::size_t len)
	{
		LIBOTHER_UNUSED(str);
		LIBOTHER_UNUSED(len);
		return time::DateTime::Now().ToString();
	}

	inline std::string operator ""_dutc(const char* str, std::size_t len)
	{
		LIBOTHER_UNUSED(str);
		LIBOTHER_UNUSED(len);
		return time::DateTime::UtcNow().ToString();
	}
}

#endif

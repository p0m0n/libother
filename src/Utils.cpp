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
}

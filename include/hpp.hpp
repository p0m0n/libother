#ifndef _OTHER_HPP_
#define _OTHER_HPP_


#include "hpp/basic_number.hpp"


namespace other::hpp
{
	typedef basic_number<int8_t>   int8_n;
	typedef basic_number<int16_t>  int16_n;
	typedef basic_number<int32_t>  int32_n;
	typedef basic_number<int64_t>  int64_n;

	typedef basic_number<uint8_t>  uint8_n;
	typedef basic_number<uint16_t> uint16_n;
	typedef basic_number<uint32_t> uint32_n;
	typedef basic_number<uint64_t> uint64_n;

	typedef basic_number<float>    float_n;
	typedef basic_number<double>   double_n;
}


#endif // !_HPP_

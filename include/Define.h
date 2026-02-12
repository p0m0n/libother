#ifndef LIBOTHER_DEFINE_H
#define LIBOTHER_DEFINE_H

#include "CompilerDefs.h"


#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <string>
#include <vector>
#include <map>
#include <numeric>
#include <regex>

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include <ctime>
#include <chrono>
#include <stdexcept>
#include <cassert>


#if LIBOTHER_PLATFORM == LIBOTHER_PLATFORM_WINDOWS
#   include <windows.h>
#   undef min
#   undef max
#   undef ARRAYSIZE
#   undef _ARRAYSIZE
#   define m_sscanf                    sscanf_s
#else
#   define m_sscanf                    sscanf
#endif

#define ARRAY_SIZE(A)    (sizeof(A)/sizeof((A)[0]))

#endif

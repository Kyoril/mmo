
#pragma once

#include <cassert>

#define ASSERT(x) assert(x)

#ifdef _DEBUG
#	define VERIFY(x) ASSERT(x)
#else
#	define VERIFY(x) (void)(x)
#endif

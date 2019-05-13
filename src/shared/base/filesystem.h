
#pragma once

#if defined(__GNUC__)
#	if __GNUC__ <= 7
#		include <experimental/filesystem>
namespace std
{
	namespace filesystem = std::experimental::filesystem::v1;
}
#	endif
#else
#	include <filesystem>
#endif

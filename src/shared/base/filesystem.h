
#pragma once

#if defined(__GNUC__)
#	if __GNUC__ <= 7
#		include <experimental/filesystem>
namespace std
{
	namespace filesystem = std::experimental::filesystem::v1;
}
#	else
#		include <filesystem>
#	endif
#else
#	include <filesystem>
#endif

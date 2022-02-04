
#pragma once

#include <filesystem>

using Path = std::filesystem::path;

namespace mmo
{
    Path GetExecutablePath();
}

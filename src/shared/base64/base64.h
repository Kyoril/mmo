// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <string>

namespace mmo
{
	std::string base64_encode(unsigned char const *, unsigned int len);
	std::string base64_decode(std::string const &s);
}
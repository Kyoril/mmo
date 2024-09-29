// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <string>

namespace mmo
{
	std::string base64_encode(unsigned char const *, unsigned int len);
	std::string base64_decode(std::string const &s);
}
// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <istream>
#include <ostream>

namespace mmo
{
	/// Describes the given archive by printing it's contents to the standard
	/// output stream.
	void Describe(
	    std::istream &archive,
	    std::ostream &description
	);
}

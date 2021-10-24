
#pragma once

#include "base/typedefs.h"


namespace mmo
{
	class Localization;

	/// Translates a given string using the provided localization. If the string id
	/// is not present, the string id is returned.
	const std::string& Localize(const Localization& localization, const std::string& id);
}

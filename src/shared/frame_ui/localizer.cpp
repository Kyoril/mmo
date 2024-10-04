// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "localizer.h"
#include "localization.h"

namespace mmo
{
	const std::string & Localize(const Localization & localization, const std::string & id)
	{
		const std::string* translation = localization.FindStringById(id);
		if (translation)
		{
			return *translation;
		}

		// String not found
		return id;
	}
}

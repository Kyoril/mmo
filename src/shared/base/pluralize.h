
#pragma once

#include "typedefs.h"


namespace mmo
{
	/// A simple helper function to pluralize English words if needed. Keep in mind that this
	/// is probably not perfect but should fit for most situations.
	template<class T = uint32>
	std::string Pluralize(const std::string& word, T count)
	{
		// Nothing to do here?
		if (count == 1) return word;

		const size_t l = word.size();
		if (l > 0)
		{
			const auto lastChar = word.back();
			switch (lastChar)
			{
			case 'y':
				return word.substr(0, l - 1) + "ies";
			case 'o':
			case 's':
			case 'x':
				return word + "es";
			case 'f':
				return word.substr(0, l - 1) + "ves";
			}

			if (l > 1)
			{
				const auto preLastChar = word[l - 2];
				if (preLastChar == 'f' && lastChar == 'e')
				{
					return word.substr(0, l - 2) + "ves";
				}

				if ((preLastChar == 'c' && lastChar == 'h') ||
					(preLastChar == 's' && lastChar == 'h'))
				{
					return word + "es";
				}
			}
		}

		return word + 's';
	}
}

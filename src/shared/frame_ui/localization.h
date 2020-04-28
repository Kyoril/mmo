
#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <unordered_map>


namespace mmo
{
	/// 
	class Localization final
		: public NonCopyable
	{
	public:
		/// 
		explicit Localization();

	public:
		/// Finds the localized text of a string by it's id.
		const std::string* FindStringById(const std::string& id) const;
		/// Loads the localization data from a given file.
		bool LoadFromFile();

	private:
		typedef std::unordered_map<std::string, std::string> TranslationsById;
		TranslationsById m_translationsById;
	};
}

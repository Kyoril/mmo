#pragma once

#include "style.h"

#include "base/non_copyable.h"
#include "base/utilities.h"

#include <string>


namespace mmo
{
	/// Manages ui styles by name.
	class StyleManager final 
		: public NonCopyable
	{
	private:
		StyleManager() = default;

	public:
		/// Singleton getter method.
		static StyleManager& Get();

	public:
		/// Creates a new style using the given type.
		StylePtr Create(const std::string& name);
		/// Finds a style instance by the given name.
		StylePtr Find(const std::string& name);
		/// Destroys a style with the given name. Note that the style instance might not actually be 
		/// destroyed here if it is still referenced somewhere else, but it will no longer be findable
		/// using Find().
		void Destroy(const std::string& name);

	private:
		/// A map of all registered styles, grouped by name.
		std::map<std::string, StylePtr, StrCaseIComp> m_stylesByName;
	};
}
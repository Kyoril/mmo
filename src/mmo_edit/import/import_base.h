#pragma once

#include "base/typedefs.h"

#include <filesystem>
using Path = std::filesystem::path;

namespace mmo
{
	/// @brief Base class of a file importer.
	class ImportBase
	{
	public:
		/// @brief Virtual default destructor because of inheritance.
		virtual ~ImportBase() = default;

		/// Override this method if the importer needs to render some UI elements.
		virtual void Draw() {};

	public:
		/// @brief Does the actual file import for a given source file.
		/// @param filename The full path of the file to import.
		///	@param currentAssetPath The current path in the asset browser of the editor to indicate the prefix for new assets,
		/// @return true if the import was a success, false otherwise.
		virtual bool ImportFromFile(const Path& filename, const Path& currentAssetPath) = 0;

		/// @brief Determines whether the given file extension is supported by the importer implementation.
		///	       Extension is provided in all lowercase and starts with a dot.
		/// @param extension The lowercase extension starting with a dot.
		///	@return true if this importer supports the given extension, false otherwise.
		[[nodiscard]] virtual bool SupportsExtension(const String& extension) const noexcept = 0;
	};
}

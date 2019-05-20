
#pragma once

#include <string>
#include <memory>
#include <istream>

namespace mmo
{
	enum class ArchiveMode
	{
		ReadOnly,
		WriteOnly,
		ReadWrite,
	};

	struct IArchive
	{
		virtual ~IArchive() = default;

		/// Loads the archive.
		virtual void Load() = 0;
		/// Unloads the archive.
		virtual void Unload() = 0;
		/// Gets the archive name.
		virtual const std::string& GetName() const = 0;
		/// Gets the archive mode.
		virtual ArchiveMode GetMode() const = 0;
		/// Tries to open a file for reading.
		virtual std::unique_ptr<std::istream> Open(const std::string& filename) = 0;
	};

}
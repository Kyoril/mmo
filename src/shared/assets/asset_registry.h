// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/filesystem.h"

#include <memory>
#include <istream>


namespace mmo
{
	/// This class manages assets for the game. It allows loading of files regardless
	/// of their source. For example, a file might be an actual file or a virtual file
	/// inside of an hpak archive.
	class AssetRegistry final
		: public NonCopyable
	{
	private:
		/// Private default constructor.
		AssetRegistry() = default;

	public:
		/// Initializes the asset registry.
		static void Initialize(const std::filesystem::path& basePath, const std::vector<std::string>& archives);
		/// Destroys the asset registry.
		static void Destroy();
		/// Opens a file for reading. Returns nullptr if the files doesn't exist.
		static std::unique_ptr<std::istream> OpenFile(const std::string& filename);
	};
}
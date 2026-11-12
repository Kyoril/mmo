// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "pcg/graph/controls.h"

#include <map>
#include <memory>
#include <vector>

namespace mmo
{
	/// @brief Loads and caches control types (.hctl) by asset path; lists biomes and their types.
	/// @details Biomes are the folders under Editor/Pcg/Biomes; a biome's control types live in its
	///          Controls folder. A type is read once and kept (also a failure, with its message) until
	///          Invalidate or InvalidateAll drops it, which bumps the revision. Pointers returned by Find
	///          stay valid until the type is invalidated.
	class ControlLibrary final
	{
	public:
		/// @brief Asset folder holding the biomes, with a trailing slash.
		static constexpr const char* BiomesFolder = "Editor/Pcg/Biomes/";

	public:
		/// @brief Lists the biomes: the folder names under Editor/Pcg/Biomes that contain any file, sorted.
		[[nodiscard]] std::vector<String> ListBiomes() const;

		/// @brief Lists the asset paths of a biome's control types (Controls/*.hctl), sorted.
		/// @param biome Folder name of the biome.
		[[nodiscard]] std::vector<String> ListControlTypes(const String& biome) const;

		/// @brief Gets a control type, reading it on first use.
		/// @param typePath Asset path of the .hctl file.
		/// @param error Receives "control type '<path>' not found" or "... is invalid: <reason>" when null is returned.
		/// @return The type, or null if the file is missing or invalid.
		const pcg::graph::ControlType* Find(const String& typePath, String* error = nullptr);

		/// @brief Drops a cached type so the next Find reads it again (the control type editor calls this
		///        when it saves). Bumps the revision.
		/// @param typePath Asset path of the .hctl file.
		void Invalidate(const String& typePath);

		/// @brief Drops every cached type. Bumps the revision.
		void InvalidateAll();

		/// @brief Counter bumped by Invalidate and InvalidateAll.
		[[nodiscard]] uint64 Revision() const { return m_revision; }

	private:
		/// @brief A read control type, or the reason it could not be read.
		struct Entry
		{
			std::unique_ptr<pcg::graph::ControlType> type;
			String error;
		};

		std::map<String, Entry> m_types;
		uint64 m_revision = 0;
	};
}

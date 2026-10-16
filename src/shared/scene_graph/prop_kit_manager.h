// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "prop_kit.h"

#include "base/non_copyable.h"
#include "base/utilities.h"

#include <map>

namespace mmo
{
	/// @brief Loads prop kits (.hkit) from the asset registry and caches them so that a kit
	/// is not loaded multiple times.
	class PropKitManager final : public NonCopyable
	{
	private:
		PropKitManager() = default;

	public:
		~PropKitManager() = default;

	public:
		/// @brief Gets the singleton instance of the prop kit manager.
		static PropKitManager& Get();

	public:
		/// @brief Loads a prop kit from file or retrieves it from the cache. Only successful loads are cached.
		/// @param path The path to the prop kit file.
		/// @return Shared pointer to the prop kit, or nullptr on failure.
		PropKitPtr Load(const String& path);

		/// @brief Finds a cached prop kit without loading it.
		/// @param path The path of the prop kit.
		/// @return Shared pointer to the prop kit if cached, nullptr otherwise.
		PropKitPtr Find(const String& path) const;

		/// @brief Inserts (or replaces) a prop kit in the cache without loading from file.
		/// Used by tests and the editor.
		/// @param path The path to cache the kit under.
		/// @param kit The prop kit.
		void Insert(const String& path, PropKitPtr kit);

		/// @brief Removes a prop kit from the cache.
		/// @param path The path of the prop kit to remove.
		void Remove(const String& path);

		/// @brief Clears all cached prop kits.
		void Clear();

	private:
		/// Loaded prop kits. Case-insensitive paths.
		std::map<String, PropKitPtr, StrCaseIComp> m_kits;
	};
}

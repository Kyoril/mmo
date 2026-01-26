// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_model.h"
#include "world_model_serializer.h"

#include "base/non_copyable.h"
#include "base/utilities.h"

#include <map>
#include <memory>
#include <string>

namespace mmo
{
	/// @brief This class manages world models. It loads them from files if required and caches
	/// them so that a world model isn't loaded multiple times.
	class WorldModelManager final : public NonCopyable
	{
	private:
		WorldModelManager() = default;

	public:
		~WorldModelManager() = default;

	public:
		/// @brief Gets the singleton instance of the world model manager.
		/// @return Reference to the world model manager instance.
		static WorldModelManager& Get();

	public:
		/// @brief Loads a world model from file or retrieves it from the cache.
		/// @param filename The path to the world model file.
		/// @return Shared pointer to the world model, or nullptr on failure.
		WorldModelPtr Load(const std::string& filename);

		/// @brief Finds a world model by name without loading it.
		/// @param name The name of the world model to find.
		/// @return Shared pointer to the world model if found, nullptr otherwise.
		WorldModelPtr Find(const std::string& name);

		/// @brief Creates a world model manually without loading from file.
		/// @param name The name for the new world model.
		/// @return Shared pointer to the newly created world model.
		WorldModelPtr CreateManual(const std::string& name);

		/// @brief Removes a world model from the cache.
		/// @param name The name of the world model to remove.
		void Remove(const std::string& name);

		/// @brief Clears all cached world models.
		void Clear();

	private:
		/// A map of loaded world models. Used to cache. Case-insensitive file names.
		std::map<std::string, WorldModelPtr, StrCaseIComp> m_worldModels;
	};
}

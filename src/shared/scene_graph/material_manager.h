// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "graphics/material.h"

#include <functional>
#include <map>
#include <span>

#include "base/utilities.h"


namespace mmo
{
	/// @brief Class which manages all materials.
	class MaterialManager final : public NonCopyable
	{
		friend std::unique_ptr<MaterialManager> std::make_unique<MaterialManager>();

	private:
		MaterialManager() = default;

	public:
		~MaterialManager() override = default;

	public:
		static MaterialManager& Get();
		
	public:
		/// Loads a material from file or retrieves it from the cache.
		MaterialPtr Load(std::string_view filename);

		/// Creates a material manually.
		MaterialPtr CreateManual(const std::string_view name);

		void Remove(std::string_view filename);

		void RemoveAllUnreferenced();

		/// @brief Called for every .hmat after it was deserialized and before its GPU shaders are created.
		using PostLoadHook = std::function<void(Material& material, std::span<const uint8> fileBytes)>;

		/// @brief Installs a hook that may replace a freshly loaded material's shaders, e.g. compile missing or
		///        stale ones in development clients. Without a hook, materials load as stored.
		/// @param hook The hook, or nullptr to remove it.
		void SetPostLoadHook(PostLoadHook hook) { m_postLoadHook = std::move(hook); }

	private:
		std::map<std::string, MaterialPtr, StrCaseIComp> m_materials;
		PostLoadHook m_postLoadHook;
	};
}

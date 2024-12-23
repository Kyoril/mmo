// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "graphics/material.h"

#include <map>

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

	private:
		std::map<std::string, MaterialPtr, StrCaseIComp> m_materials;
	};
}

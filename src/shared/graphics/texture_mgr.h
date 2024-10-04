// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "texture.h"

#include "base/non_copyable.h"
#include "base/utilities.h"

#include <string>
#include <map>


namespace mmo
{
	/// The texture manager class can be used to avoid loading a single texture multiple
	/// times when referencing it by a filename. It also makes loading texture a lot
	/// easier.
	class TextureManager final : public NonCopyable
	{
	private:
		TextureManager()
			: m_memoryBudget(1024 * 1024 * 1024)	// 1 GB budget for now
			, m_memoryUsage(0)
		{}

	public:
		/// Singleton getter method.
		static TextureManager& Get();

	public:
		/// Creates a new texture from a file or returns a shared pointer to it if it has
		/// already been loaded before. The assets library is used for loading, so the file
		/// name should be relative to the asset repository.
		/// @param filename The filename of the texture.
		TexturePtr CreateOrRetrieve(const std::string& filename);

	private:
		/// Checks if the memory budget is exceeded and if so, tries to free some memory
		/// by removing textures that are not referenced anymore.
		void EnsureMemoryBudget();

	public:
		/// Gets the current memory budget.
		inline size_t GetMemoryBudget() const { return m_memoryBudget; }
		/// Gets the memory usage in bytes.
		inline size_t GetMemoryUsage() const { return m_memoryUsage; }
		/// Sets the new memory budget in bytes.
		void SetMemoryBudget(size_t newBudget);

	private:
		/// A list of textures associated to their case-insensitive filenames. Note that we only
		/// keep weak_ptr references since we don't want to own the loaded textures ourself here
		/// to make unloading texture less complicated.
		std::map<std::string, TexturePtr, StrCaseIComp> m_texturesByName;
		/// The memory budget in bytes.
		size_t m_memoryBudget;
		/// The current memory usage in bytes.
		size_t m_memoryUsage;
	};
}
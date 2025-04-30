// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <memory>
#include <istream>
#include <vector>

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

		virtual bool RemoveFile(const std::string& filename) = 0;
		/// Gets the archive name.
		[[nodiscard]] virtual const std::string& GetName() const = 0;
		/// Gets the archive mode.
		[[nodiscard]] virtual ArchiveMode GetMode() const = 0;
		/// Tries to open a file for reading.
		virtual std::unique_ptr<std::istream> Open(const std::string& filename) = 0;
		/// Enumerates all files the archive contains.
		virtual void EnumerateFiles(std::vector<std::string>& files) = 0;
	};

}
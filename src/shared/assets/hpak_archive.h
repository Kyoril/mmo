// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "archive.h"

#include "hpak_v1_0/header.h"

#include <fstream>

namespace mmo
{
	class HPAKArchive
		: public IArchive
	{
	private:
		std::string m_name;
		hpak::v1_0::Header m_header;
		std::unique_ptr<std::ifstream> m_file;

	public:
		HPAKArchive(const std::string& filename);

	public:
		void Load() override;
		void Unload() override;
		const std::string& GetName() const override;
		[[nodiscard]] ArchiveMode GetMode() const override;
		std::unique_ptr<std::istream> Open(const std::string& filename) override;
		void EnumerateFiles(std::vector<std::string>& files) override;
	};
}

// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "hpak_archive.h"

#include "base/macros.h"

#include "hpak/pre_header.h"
#include "hpak/pre_header_load.h"
#include "hpak_v1_0/header_load.h"
#include "hpak_v1_0/read_content_file.h"

#include "binary_io/reader.h"
#include "binary_io/stream_source.h"

#include <sstream>
#include <stdexcept>
#include <algorithm>


namespace mmo
{
	HPAKArchive::HPAKArchive(const std::string & filename)
		: m_name(filename)
		, m_header(hpak::Version_1_0)
	{
	}

	void HPAKArchive::Load()
	{
		// Open the hpak archive file for reading
		m_file = std::make_unique<std::ifstream>(m_name.c_str(), std::ios::in | std::ios::binary);
		ASSERT(m_file && *m_file);

		// Generate source and reader
		io::StreamSource source{ *m_file };
		io::Reader reader{ source };

		// Load hpak archive here
		hpak::PreHeader preHeader;
		if (!hpak::loadPreHeader(preHeader, reader))
		{
			throw std::runtime_error("Failed to read pre header");
		}

		// Verify version
		if (preHeader.version != hpak::Version_1_0)
		{
			throw std::runtime_error("Unsupported hpak file format version, expected version 1.0");
		}

		// Load hpak header
		m_header.files.clear();
		if (!hpak::v1_0::loadHeader(m_header, reader))
		{
			throw std::runtime_error("Failed to read hpak v1.0 header, archive " +
				m_name + " might be damaged");
		}
	}

	void HPAKArchive::Unload()
	{
		// Reset file pointer
		m_file.reset();
	}

	const std::string & HPAKArchive::GetName() const
	{
		return m_name;
	}
	
	ArchiveMode HPAKArchive::GetMode() const
	{
		return ArchiveMode::ReadOnly;
	}

	std::unique_ptr<std::istream> HPAKArchive::Open(const std::string & filename)
	{
		// Find file info
		auto it = std::find_if(m_header.files.begin(), m_header.files.end(), [&filename](const hpak::v1_0::FileEntry & entry)
		{
			return entry.name == filename;
		});
		if (it == m_header.files.end())
		{
			return nullptr;
		}

		hpak::v1_0::ContentFileReader fileReader(
			m_header,
			*it,
			*m_file);

		std::unique_ptr<std::stringstream> strm = std::make_unique<std::stringstream>();
		(*strm) << fileReader.GetContent().rdbuf();
		strm->seekg(0);

		return strm;
	}

	void HPAKArchive::EnumerateFiles(std::vector<std::string>& files)
	{
		for (const auto& file : m_header.files)
		{
			files.push_back(file.name);
		}
	}
}

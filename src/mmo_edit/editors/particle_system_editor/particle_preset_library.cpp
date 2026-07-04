// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_preset_library.h"

#include <algorithm>

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"

namespace mmo
{
	namespace
	{
		const String s_presetFolder = "Editor/ParticlePresets/";
		const String s_presetExtension = ".hpar";
	}

	const std::vector<ParticlePresetLibrary::PresetInfo>& ParticlePresetLibrary::ListPresets()
	{
		if (m_cacheValid)
		{
			return m_presets;
		}

		m_presets.clear();

		for (const std::string& file : AssetRegistry::ListFiles(s_presetFolder, s_presetExtension))
		{
			PresetInfo info;
			info.path = file;

			// Display name is the file name without folder and extension
			String name = file;
			if (name.starts_with(s_presetFolder))
			{
				name = name.substr(s_presetFolder.size());
			}
			if (name.ends_with(s_presetExtension))
			{
				name = name.substr(0, name.size() - s_presetExtension.size());
			}
			info.name = name;

			m_presets.push_back(std::move(info));
		}

		std::sort(m_presets.begin(), m_presets.end(), [](const PresetInfo& a, const PresetInfo& b)
		{
			return a.name < b.name;
		});

		m_cacheValid = true;
		return m_presets;
	}

	bool ParticlePresetLibrary::SavePreset(const String& name, const ParticleSystemParameters& params)
	{
		const String fileName = SanitizeName(name);
		if (fileName.empty())
		{
			ELOG("Cannot save particle preset: invalid preset name '" << name << "'");
			return false;
		}

		const String path = s_presetFolder + fileName + s_presetExtension;

		const auto file = AssetRegistry::CreateNewFile(path);
		if (!file)
		{
			ELOG("Failed to create particle preset file " << path);
			return false;
		}

		io::StreamSink sink(*file);
		io::Writer writer(sink);

		ParticleSystemSerializer serializer;
		serializer.Serialize(params, writer);
		file->flush();

		ILOG("Saved particle preset to " << path);
		Invalidate();
		return true;
	}

	bool ParticlePresetLibrary::LoadPreset(const String& path, ParticleSystemParameters& out) const
	{
		const auto file = AssetRegistry::OpenFile(path);
		if (!file)
		{
			ELOG("Failed to open particle preset file " << path);
			return false;
		}

		io::StreamSource source(*file);
		io::Reader reader(source);

		ParticleSystemSerializer serializer;
		if (!serializer.Deserialize(out, reader))
		{
			ELOG("Failed to deserialize particle preset from " << path);
			return false;
		}

		return true;
	}

	bool ParticlePresetLibrary::PresetExists(const String& name)
	{
		const String fileName = SanitizeName(name);

		const auto& presets = ListPresets();
		return std::any_of(presets.begin(), presets.end(), [&fileName](const PresetInfo& info)
		{
			return info.name == fileName;
		});
	}

	String ParticlePresetLibrary::SanitizeName(const String& name)
	{
		String result;
		result.reserve(name.size());

		for (const char c : name)
		{
			// Allow alphanumerics, spaces and a few safe punctuation characters
			if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-' || c == '(' || c == ')')
			{
				result.push_back(c);
			}
		}

		// Trim leading / trailing whitespace
		const auto begin = result.find_first_not_of(' ');
		if (begin == String::npos)
		{
			return {};
		}
		const auto end = result.find_last_not_of(' ');
		return result.substr(begin, end - begin + 1);
	}
}

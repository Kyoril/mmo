// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "audio_settings.h"

#include "console/console_var.h"

#include "shared/audio/audio.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Describes a single sound category controlled by an enable and a volume cvar.
		struct CategoryCVarDefinition
		{
			SoundCategory category;
			const char* enabledName;
			const char* enabledDescription;
			const char* volumeName;
			const char* volumeDescription;
			const char* volumeDefault;
		};

		const CategoryCVarDefinition s_categoryCVars[] =
		{
			{ SoundCategory::Music,			"MusicEnabled",		"Whether music is enabled.",						"MusicVolume",		"Music volume (0 to 1).",			"0.6" },
			{ SoundCategory::Ambience,		"AmbienceEnabled",	"Whether zone ambience sounds are enabled.",		"AmbienceVolume",	"Zone ambience volume (0 to 1).",	"0.8" },
			{ SoundCategory::SoundEffects,	"EffectsEnabled",	"Whether sound effects are enabled.",				"EffectsVolume",	"Sound effects volume (0 to 1).",	"1.0" },
			{ SoundCategory::Interface,		"InterfaceEnabled",	"Whether interface sounds are enabled.",			"InterfaceVolume",	"Interface sound volume (0 to 1).",	"1.0" },
			{ SoundCategory::Voice,			"VoiceEnabled",		"Whether voice lines are enabled.",					"VoiceVolume",		"Voice line volume (0 to 1).",		"1.0" },
		};

		float ClampVolume(const float volume)
		{
			return std::clamp(volume, 0.0f, 1.0f);
		}
	}

	AudioSettings::AudioSettings(IAudio& audio)
		: m_audio(audio)
	{
		// RegisterConsoleVar is idempotent: values that were already loaded from the config
		// file before this point survive registration, so we never overwrite user settings.
		ConsoleVar* masterEnabled = ConsoleVarMgr::RegisterConsoleVar("SoundEnabled", "Whether sound output is enabled at all.", "1");
		ConsoleVar* masterVolume = ConsoleVarMgr::RegisterConsoleVar("MasterVolume", "Master volume (0 to 1).", "1.0");

		m_connections += masterEnabled->Changed.connect([this](ConsoleVar& var, const std::string&)
			{
				m_audio.SetMasterMuted(!var.GetBoolValue());
			});
		m_connections += masterVolume->Changed.connect([this](ConsoleVar& var, const std::string&)
			{
				m_audio.SetMasterVolume(ClampVolume(var.GetFloatValue()));
			});

		// Content reference: the SoundEntry id played as login screen music (0 = built-in default).
		ConsoleVarMgr::RegisterConsoleVar("LoginMusicSound", "SoundEntry id of the login screen music (0 = default).", "0");

		for (const auto& definition : s_categoryCVars)
		{
			ConsoleVar* enabled = ConsoleVarMgr::RegisterConsoleVar(definition.enabledName, definition.enabledDescription, "1");
			ConsoleVar* volume = ConsoleVarMgr::RegisterConsoleVar(definition.volumeName, definition.volumeDescription, definition.volumeDefault);

			const SoundCategory category = definition.category;
			m_connections += enabled->Changed.connect([this, category](ConsoleVar& var, const std::string&)
				{
					m_audio.SetCategoryMuted(category, !var.GetBoolValue());
				});
			m_connections += volume->Changed.connect([this, category](ConsoleVar& var, const std::string&)
				{
					m_audio.SetCategoryVolume(category, ClampVolume(var.GetFloatValue()));
				});
		}

		ApplyAll();
	}

	void AudioSettings::ApplyAll() const
	{
		if (const ConsoleVar* masterEnabled = ConsoleVarMgr::FindConsoleVar("SoundEnabled"))
		{
			m_audio.SetMasterMuted(!masterEnabled->GetBoolValue());
		}
		if (const ConsoleVar* masterVolume = ConsoleVarMgr::FindConsoleVar("MasterVolume"))
		{
			m_audio.SetMasterVolume(ClampVolume(masterVolume->GetFloatValue()));
		}

		for (const auto& definition : s_categoryCVars)
		{
			if (const ConsoleVar* enabled = ConsoleVarMgr::FindConsoleVar(definition.enabledName))
			{
				m_audio.SetCategoryMuted(definition.category, !enabled->GetBoolValue());
			}
			if (const ConsoleVar* volume = ConsoleVarMgr::FindConsoleVar(definition.volumeName))
			{
				m_audio.SetCategoryVolume(definition.category, ClampVolume(volume->GetFloatValue()));
			}
		}
	}
}

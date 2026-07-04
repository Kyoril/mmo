// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"
#include "particle_preset_library.h"
#include "scene_graph/particle_emitter.h"

#include <map>
#include <optional>

namespace mmo
{
	/// @brief Implementation of the EditorBase class for editing particle system files (.hpar).
	class ParticleSystemEditor final : public EditorBase
	{
	public:
		explicit ParticleSystemEditor(EditorHost& host);

		~ParticleSystemEditor() override = default;

	public:
		/// @brief The shared preset library used by all open particle editor instances.
		ParticlePresetLibrary& GetPresetLibrary() { return m_presetLibrary; }

		/// @brief Puts a copy of the given emitter into the editor-wide clipboard.
		void CopyEmitterToClipboard(const EmitterParameters& emitter) { m_emitterClipboard = emitter; }

		/// @brief Returns the emitter currently in the clipboard, if any. Shared across all open instances.
		[[nodiscard]] const std::optional<EmitterParameters>& GetEmitterClipboard() const { return m_emitterClipboard; }

	public:
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

		[[nodiscard]] bool CanCreateAssets() const override
		{
			return true;
		}

		void AddCreationContextMenuItems() override;

	protected:
		void DrawImpl() override;

		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:
		void CreateNewParticleSystem();

	private:
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showNameDialog { false };
		String m_particleSystemName;
		ParticlePresetLibrary m_presetLibrary;
		std::optional<EmitterParameters> m_emitterClipboard;
	};
}

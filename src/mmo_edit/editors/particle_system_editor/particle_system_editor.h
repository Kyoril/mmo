// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	/// @brief Implementation of the EditorBase class for editing particle system files (.hpar).
	class ParticleSystemEditor final : public EditorBase
	{
	public:
		explicit ParticleSystemEditor(EditorHost& host);

		~ParticleSystemEditor() override = default;

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
	};
}

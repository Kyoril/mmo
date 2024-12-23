// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	/// @brief Implementation of the EditorBase class for being able to edit model files.
	class MeshEditor final : public EditorBase
	{
	public:
		explicit MeshEditor(EditorHost& host);

		~MeshEditor() override = default;

	public:
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

	protected:
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:

		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
	};
}
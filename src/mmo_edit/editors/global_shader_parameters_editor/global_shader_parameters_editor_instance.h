// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <vector>

#include "editors/editor_instance.h"
#include "graphics/global_shader_parameters.h"

namespace mmo
{
	/// @brief Editor instance for editing the global shader parameter registry.
	class GlobalShaderParametersEditorInstance final : public EditorInstance
	{
	public:
		GlobalShaderParametersEditorInstance(EditorHost& host, const Path& assetPath);
		~GlobalShaderParametersEditorInstance() override = default;

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;

		/// @copydoc EditorInstance::Save
		bool Save() override;

	private:
		/// @brief Pushes the local working set into the live runtime registry (for live preview).
		void SyncToRegistry();

		/// @brief Pulls the current runtime registry into the local working set.
		void RefreshFromRegistry();

	private:
		std::vector<GlobalShaderParameter> m_working;
		bool m_modified { false };
		int m_newParamType { 0 };
		String m_newParamName;
	};
}

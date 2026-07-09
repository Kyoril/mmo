// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>

#include "editors/editor_base.h"
#include "graphics/material.h"

namespace mmo
{
	class PreviewProviderManager;

	/// @brief Editor implementation to support creation and editing of materials.
	class MaterialEditor final : public EditorBase
	{
	public:
		/// @brief Default constructor.
		/// @param host Host which provides support for stuff editors might require.
		explicit MaterialEditor(EditorHost& host, PreviewProviderManager& previewManager)
			: EditorBase(host)
			, m_previewManager(previewManager)
		{
		}

	public:
		/// @copydoc EditorBase::CanLoadAsset 
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;
		
		/// @brief EditorBase::CanCreateAssets 
		[[nodiscard]] bool CanCreateAssets() const override { return true; }
		
		/// @brief Adds creation items to a context menu.
		void AddCreationContextMenuItems() override;

		void AddAssetActions(const String& asset) override;

		/// @copydoc EditorBase::AddToolMenuItems
		void AddToolMenuItems() override;

		PreviewProviderManager& GetPreviewManager() const { return m_previewManager; }

	protected:
		/// @copydoc EditorBase::DrawImpl
		void DrawImpl() override;
		
		/// @copydoc EditorBase::OpenAssetImpl 
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		
		/// @copydoc EditorBase::CloseInstanceImpl
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:
		/// @brief Called when a new material should be created.
		void CreateNewMaterial();

		void CreateNewMaterialFunction();

		void CreateNewMaterialInstance();

		/// @brief Starts an incremental rebuild of every .hmat asset: each material is recompiled from
		///        its stored node graph and re-saved with fresh shader bytecode. Required after
		///        engine-side changes to the generated shader interface (e.g. constant buffer layout
		///        changes). The work is spread across frames (see TickMaterialRebuild) so the editor
		///        stays responsive; progress is shown in a modal.
		void RebuildAllMaterials();

		/// @brief Rebuilds a single material asset (load, recompile, re-save). The original graph
		///        chunk is copied back verbatim so node-editor layout data is preserved exactly.
		/// @return true on success.
		bool RebuildMaterial(const std::string& assetPath);

		/// @brief Processes the pending rebuild queue with a per-frame time budget. Called from
		///        DrawImpl while a rebuild is in progress.
		void TickMaterialRebuild();

	private:
		PreviewProviderManager& m_previewManager;
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showMaterialNameDialog { false };
		bool m_showMaterialFunctionNameDialog{ false };
		bool m_showMaterialInstanceDialog{ false };
		String m_materialName;
		String m_materialFunctionName;
		String m_materialInstanceName;
		MaterialPtr m_selectedMaterial;

		// State of a running / finished "Rebuild All Materials" run. The queue is processed
		// incrementally from DrawImpl with a time budget so the UI stays responsive.
		bool m_rebuildInProgress{ false };
		std::vector<std::string> m_rebuildQueue;
		size_t m_rebuildIndex{ 0 };
		/// Throwaway node-editor context active while rebuilding, so graph deserialization has a
		/// context to restore node state into (avoids per-node log spam). Never rendered or saved.
		void* m_rebuildEdContext{ nullptr };
		bool m_showRebuildResultPopup{ false };
		uint32 m_rebuildSucceeded{ 0 };
		std::vector<String> m_rebuildFailed;
	};
}

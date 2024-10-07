// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class ModelEditorWindow final
		: public EditorEntryWindowBase<proto::ModelDatas, proto::ModelDataEntry>
		, public NonCopyable
	{
	public:
		explicit ModelEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ModelEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::ModelDatas, proto::ModelDataEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		std::vector<String> m_modelFiles;
	};
}

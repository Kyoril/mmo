// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class SpellEditorWindow final
		: public EditorEntryWindowBase<proto::Spells, proto::SpellEntry>
		, public NonCopyable
	{
	public:
		explicit SpellEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~SpellEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::SpellEntry& currentEntry) override;

		void DrawEffectDialog(proto::SpellEntry& currentEntry, proto::SpellEffect& effect, int32 effectIndex);

		void DrawSpellAuraEffectDetails(proto::SpellEffect& effect);

		void OnNewEntry(proto::TemplateManager<proto::Spells, proto::SpellEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		std::vector<String> m_textures;
		std::map<std::string, TexturePtr> m_iconCache;
	};

	inline void SpellEditorWindow::OnNewEntry(
		proto::TemplateManager<proto::Spells, proto::SpellEntry>::EntryType& entry)
	{
		EditorEntryWindowBase<proto::Spells, proto::SpellEntry>::OnNewEntry(entry);

		entry.add_attributes(0);
		entry.add_attributes(0);
	}
}

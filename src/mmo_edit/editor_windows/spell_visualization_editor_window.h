// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "editor_entry_window_base.h"
#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	class PreviewProviderManager;
	class IAudio;

	/// \brief Editor window for managing spell visualizations (data-driven visual effects).
	///
	/// Provides CRUD operations for SpellVisualization entries with per-event kit management.
	/// Each visualization can have multiple kits bound to different spell lifecycle events
	/// (StartCast, CancelCast, Casting, CastSucceeded, Impact, AuraApplied, AuraRemoved, etc.).
	class SpellVisualizationEditorWindow final
		: public EditorEntryWindowBase<proto::SpellVisualizations, proto::SpellVisualization>
		, public NonCopyable
	{
	public:
	/// \brief Constructor.
	/// \param name Window title.
	/// \param project Proto data project containing the spell visualizations dataset.
	/// \param host Editor host for additional UI services.
	/// \param previewManager Preview provider manager for asset previews.
	/// \param audioSystem Audio system for sound previews (optional, may be null).
	explicit SpellVisualizationEditorWindow(const String& name, proto::Project& project, EditorHost& host, PreviewProviderManager& previewManager, IAudio* audioSystem = nullptr);		~SpellVisualizationEditorWindow() override = default;

	private:
		/// \brief Draw the details panel for the selected visualization entry.
		/// \param currentEntry The currently selected SpellVisualization entry.
		void DrawDetailsImpl(proto::SpellVisualization& currentEntry) override;

		/// \brief Draw the kits for a specific event.
		/// \param currentEntry The visualization being edited.
		/// \param eventValue The event enum value (proto::SpellVisualEvent).
		/// \param eventName Human-readable name for the event.
		void DrawEventKits(proto::SpellVisualization& currentEntry, uint32 eventValue, const char* eventName);

		/// \brief Draw a single kit editor (sounds, animation, tint, loop).
		/// \param kit The SpellKit to edit.
		/// \param kitIndex Index of this kit within its event list.
		/// \return True if the kit should be removed.
		bool DrawKit(proto::SpellKit& kit, int kitIndex);

		/// \brief Initialize a new entry with default values.
		void OnNewEntry(proto::TemplateManager<proto::SpellVisualizations, proto::SpellVisualization>::EntryType& entry) override;

	public:
		bool IsDockable() const override
		{
			return true;
		}

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override
		{
			return DockDirection::Center;
		}

	private:
		EditorHost& m_host;
		PreviewProviderManager& m_previewManager;
		IAudio* m_audioSystem;
		
		/// \brief Track which event sections are expanded in the UI.
		std::map<uint32, bool> m_eventExpanded;
	};
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_entry_window_base.h"
#include "base/non_copyable.h"

#include "editor_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/**
	 * @brief Editor window for managing unit classes (creature classes).
	 * 
	 * This window provides an interface to create and edit unit class templates
	 * that define stat formulas, power types, and scaling for creatures.
	 */
	class UnitClassEditorWindow final
		: public EditorEntryWindowBase<proto::UnitClasses, proto::UnitClassEntry>
		, public NonCopyable
	{
	public:
		/**
		 * @brief Constructs a new unit class editor window.
		 * @param name The window name.
		 * @param project The project containing the data.
		 * @param host The editor host.
		 */
		explicit UnitClassEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		
		/**
		 * @brief Default destructor.
		 */
		~UnitClassEditorWindow() override = default;

	public:
		/**
		 * @brief Draws the details panel for the current unit class entry.
		 * @param currentEntry The unit class entry to edit.
		 */
		void DrawDetailsImpl(EntryType& currentEntry) override;

		/**
		 * @brief Called when a new unit class entry is created.
		 * @param entry The new entry to initialize.
		 */
		void OnNewEntry(proto::TemplateManager<proto::UnitClasses, proto::UnitClassEntry>::EntryType& entry) override;

	public:
		/**
		 * @brief Whether this window can be docked.
		 * @return True if dockable.
		 */
		bool IsDockable() const override 
		{ 
			return true; 
		}

		/**
		 * @brief Gets the default dock direction for this window.
		 * @return The default dock direction.
		 */
		[[nodiscard]] DockDirection GetDefaultDockDirection() const override 
		{ 
			return DockDirection::Center; 
		}

	private:
		/**
		 * @brief Draws the base values section for a unit class.
		 * @param currentEntry The unit class entry being edited.
		 */
		void DrawBaseValuesSection(EntryType& currentEntry);

		/**
		 * @brief Draws the stat sources section (how stats convert to other values).
		 * @param currentEntry The unit class entry being edited.
		 */
		void DrawStatSourcesSection(EntryType& currentEntry);

		/**
		 * @brief Draws the regeneration settings section.
		 * @param currentEntry The unit class entry being edited.
		 */
		void DrawRegenerationSection(EntryType& currentEntry);

		/**
		 * @brief Draws the combat settings section.
		 * @param currentEntry The unit class entry being edited.
		 */
		void DrawCombatSection(EntryType& currentEntry);

		/**
		 * @brief Draws a stat source editor for attack power, health, mana, or armor.
		 * @param label The label for this stat source section.
		 * @param statSources The repeated field containing stat sources.
		 */
		template<typename StatSourceType>
		void DrawStatSourceEditor(const char* label, google::protobuf::RepeatedPtrField<StatSourceType>* statSources);

	private:
		EditorHost& m_host;
		proto::Project& m_project;
	};
}

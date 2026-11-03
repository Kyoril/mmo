// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

#include <limits>

namespace mmo
{
	/// @brief Editor window for the primary attributes (Stamina, Strength, ...).
	///
	/// An attribute's id is its slot in the replicated unit fields and every per-attribute array, so
	/// ids must be unique and smaller than MaxAttributes. New attributes take the lowest free slot.
	class AttributeEditorWindow final
		: public EditorEntryWindowBase<proto::Attributes, proto::AttributeEntry>
		, public NonCopyable
	{
	public:
		/// @brief Creates the attribute editor window.
		/// @param name The window name.
		/// @param project The project whose attributes are edited.
		/// @param host The editor host.
		explicit AttributeEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~AttributeEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Attributes, proto::AttributeEntry>::EntryType& entry) override;

		bool SupportsDuplicate() const override
		{
			return false;
		}

		/// @brief Returns the lowest attribute slot that no attribute uses yet.
		/// @returns The free slot, or MaxAttributes if every slot is taken.
		[[nodiscard]] uint32 FindLowestFreeId() const;

		/// @brief Moves the attribute with the given id to a new id, keeping its position in the list.
		/// @param oldId The current id of the attribute.
		/// @param newId The new id. Must be free and smaller than MaxAttributes.
		void ChangeEntryId(uint32 oldId, uint32 newId);

	public:
		/// @copydoc EditorWindowBase::IsDockable
		bool IsDockable() const override
		{
			return true;
		}

		/// @copydoc EditorWindowBase::GetDefaultDockDirection
		[[nodiscard]] DockDirection GetDefaultDockDirection() const override
		{
			return DockDirection::Center;
		}

	private:
		EditorHost& m_host;

		/// Id of the entry the id input below belongs to, used to reset the input when the selection changes.
		uint32 m_idEditEntry = std::numeric_limits<uint32>::max();

		/// Value typed into the id input, applied with the "Change ID" button.
		int m_idEditValue = 0;
	};
}

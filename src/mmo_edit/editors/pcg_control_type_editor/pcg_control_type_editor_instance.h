// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_instance.h"

#include "pcg/graph/controls.h"

#include <vector>

namespace mmo
{
	class PcgControlTypeEditor;

	/// @brief Editor instance for one control type asset (.hctl): shape, default tags and property groups with
	///        typed properties (name, type, default, range, enum values, description). Saving writes the file and
	///        invalidates the type in the control library of every open world, so placed controls pick up the
	///        change without reopening the world.
	class PcgControlTypeEditorInstance final : public EditorInstance
	{
	public:
		/// @brief Opens a control type for editing.
		/// @param host The editor host.
		/// @param editor The owning editor (its modified / saved signals are raised).
		/// @param asset The asset path of the control type.
		explicit PcgControlTypeEditorInstance(EditorHost& host, PcgControlTypeEditor& editor, Path asset);

		~PcgControlTypeEditorInstance() override = default;

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;

		/// @brief Writes the type to its file. Refuses to save while validation reports problems or when the
		///        file could not be parsed on load.
		/// @copydoc EditorInstance::Save
		bool Save() override;

		/// @brief Gets the edited control type.
		[[nodiscard]] const pcg::graph::ControlType& GetControlType() const { return m_type; }

		/// @brief Lists the reasons the type cannot be saved (an empty property name, duplicate property names).
		[[nodiscard]] std::vector<String> Validate() const;

		/// @brief Whether the type differs from what was loaded or last saved.
		[[nodiscard]] bool HasUnsavedChanges() const;

	private:
		/// @brief Stable ImGui ids of a group and its properties, so rows keep their widget state while others are
		///        added, removed or reordered.
		struct GroupKeys
		{
			uint64 key = 0;
			std::vector<uint64> propertyKeys;
		};

		/// @brief A structural edit requested while drawing, applied after the lists were drawn.
		struct PendingEdit
		{
			enum class Kind { None, AddProperty, RemoveGroup, MoveGroup, RemoveProperty, MoveProperty };

			Kind kind = Kind::None;
			size_t group = 0;
			size_t property = 0;
			int direction = 0;
		};

	private:
		void LoadFromFile();

		/// @brief Rebuilds the stable ids and the tag text from m_type.
		void ResetUiState();

		void DrawGroup(size_t groupIndex, PendingEdit& pending);
		void DrawProperty(size_t groupIndex, size_t propertyIndex, PendingEdit& pending);

		void ApplyPendingEdit(const PendingEdit& pending);

		/// @brief Raises the editor's modified signal when the type became dirty.
		void UpdateDirtyState();

		/// @brief Asks every open world to read this type again.
		void InvalidateInOpenWorlds() const;

		/// @brief The asset path with forward slashes, as placed controls store it.
		[[nodiscard]] String GetNormalizedAssetPath() const;

	private:
		PcgControlTypeEditor& m_editor;
		pcg::graph::ControlType m_type;

		/// @brief Set when the file could not be read or parsed; editing and saving are disabled then.
		String m_loadError;

		/// @brief The text of the type as loaded or last saved; the type is dirty when it writes differently.
		String m_savedText;
		bool m_wasDirty { false };

		/// @brief Comma separated text of the default tags (edited as text, parsed on change).
		String m_tagsText;

		std::vector<GroupKeys> m_keys;
		uint64 m_nextKey { 1 };
	};
}

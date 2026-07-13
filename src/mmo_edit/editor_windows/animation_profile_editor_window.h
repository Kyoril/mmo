// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Editor window for animation profiles: named bindings of logical animation
	///	slots (idle, movement directions, death, ...) to skeleton clip names, plus condition
	///	override sets (walk, swim, stealth, combat by weapon type) and an optional
	///	directional movement blend space. Profiles are assigned to models in the model
	///	data editor.
	class AnimationProfileEditorWindow final
		: public EditorEntryWindowBase<proto::AnimationProfiles, proto::AnimationProfileEntry>
		, public NonCopyable
	{
	public:
		explicit AnimationProfileEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~AnimationProfileEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::AnimationProfiles, proto::AnimationProfileEntry>::EntryType& entry) override;

		/// @brief Draws the editable binding table and optional blend space of a clip set.
		void DrawClipSet(proto::AnimationClipSet& clipSet);

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
	};
}

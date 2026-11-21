// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Allows editing of cutscenes: camera flights through the world, built from shots (one camera
	/// each) with keyframes, plus the sounds and subtitles on their timeline.
	///
	/// Keyframes are world editor viewpoints. Fly the world editor camera to where a keyframe should
	/// be, press "Copy Viewpoint" there, and paste it into a keyframe here; "Copy" on a keyframe goes
	/// the other way, so the world editor can jump back to it.
	class CutsceneEditorWindow final
		: public EditorEntryWindowBase<proto::Cutscenes, proto::CutsceneEntry>
		, public NonCopyable
	{
	public:
		explicit CutsceneEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~CutsceneEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::CutsceneEntry& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Cutscenes, proto::CutsceneEntry>::EntryType& entry) override;

		void DrawShot(proto::CutsceneEntry& cutscene, int shotIndex, bool& out_remove);

		void DrawSounds(proto::CutsceneEntry& cutscene);

		void DrawTexts(proto::CutsceneEntry& cutscene);

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;

		/// Camera speed used by "Retime by speed", in world units per second.
		float m_retimeSpeed = 12.0f;
	};
}

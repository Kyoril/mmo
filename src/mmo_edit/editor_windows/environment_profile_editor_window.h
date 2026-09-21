// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"
#include "editor_host.h"
#include "editors/color_curve_editor/color_curve_imgui_editor.h"
#include "graphics/color_curve.h"
#include "proto_data/project.h"

#include <array>
#include <memory>

#include <imgui.h>

namespace mmo
{
	/// Allows editing of environment profiles: the day-cycle curves and fixed values that set a
	/// zone's sky, lights, fog, light shafts, exposure and bloom. Offers a live preview in every
	/// open world editor.
	class EnvironmentProfileEditorWindow final
		: public EditorEntryWindowBase<proto::EnvironmentProfiles, proto::EnvironmentProfile>
		, public NonCopyable
	{
	public:
		/// @brief Number of day curves a profile has.
		static constexpr size_t CurveCount = 8;

	public:
		explicit EnvironmentProfileEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~EnvironmentProfileEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::EnvironmentProfile& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::EnvironmentProfiles, proto::EnvironmentProfile>::EntryType& entry) override;

		bool CanRemoveEntry(const proto::EnvironmentProfile& entry) const override;

		void OnEntryRemoved(uint32 entryId) override;

		/// @brief Rebuilds the curve copies and their widgets for the selected profile.
		void BindCurves(const proto::EnvironmentProfile& entry);

		void DrawPreviewBar(const proto::EnvironmentProfile& entry);

		void DrawCurves(proto::EnvironmentProfile& entry);

		void DrawFixedValues(proto::EnvironmentProfile& entry);

		void DrawWind(proto::EnvironmentProfile& entry);

		void DrawColorGrading(proto::EnvironmentProfile& entry);

		void DrawReferences(const proto::EnvironmentProfile& entry);

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;

		/// Editable copies of the selected profile's curves (Default curves where unauthored).
		std::array<ColorCurve, CurveCount> m_curves;

		/// Widgets bound to m_curves. Recreated whenever a curve is rebound.
		std::array<std::unique_ptr<ColorCurveImGuiEditor>, CurveCount> m_curveEditors;

		/// Id of the profile m_curves belongs to. 0 = nothing bound.
		uint32 m_boundProfileId = 0;
	};
}

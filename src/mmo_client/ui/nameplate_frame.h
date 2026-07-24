// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "frame_ui/frame.h"

#include <functional>

namespace mmo
{
	class Camera;
	class GameUnitC;
	class ProgressBar;

	/// Displays a unit nameplate (name text + health bar) above a unit's head in the 3D world.
	///
	/// The visual look (bar textures, font, selection highlight) is defined in
	/// data/client/Interface/GameUI/Nameplate.xml and copied onto this frame and its
	/// children at construction time, so it can be tweaked without code changes.
	///
	/// Unlike chat bubbles, nameplates are clickable: clicking one selects the unit it
	/// belongs to, and the currently selected unit's plate shows a highlight outline.
	class NameplateFrame final : public Frame
	{
	public:
		/// Callback invoked when the plate is right-clicked, mirroring a right-click on
		/// the unit itself (attack / talk / trade).
		using InteractHandler = std::function<void(GameUnitC&)>;

		/// Creates a nameplate which follows the specified unit until the manager removes it.
		/// @param name Unique frame name (also used to name the child frames).
		NameplateFrame(const String& name, Camera& camera, ObjectGuid unitGuid, InteractHandler interactHandler);

		/// Gets the GUID of the unit this nameplate follows.
		[[nodiscard]] ObjectGuid GetUnitGuid() const { return m_unitGuid; }

		/// Repositions the plate over its unit's head and refreshes name, health,
		/// reaction color and the selection highlight.
		///
		/// This is driven manually by the NameplateManager rather than the frame tree's
		/// Update so the plate keeps reacting even while it is temporarily hidden
		/// (e.g. off screen or behind the camera).
		void Animate(float elapsed);

	public:
		/// Selects the unit this nameplate belongs to and consumes the click so it does
		/// not bleed through into camera control (mirrors the Button behavior).
		bool OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;

		/// Performs the unit right-click interaction (if the release still happens over the
		/// plate) and consumes the event.
		bool OnMouseUp(MouseButton button, int32 buttons, const Point& position) override;

	private:
		/// Copies the matching XML template imagery onto this frame.
		void ApplyTemplate();

		/// Creates and attaches the child frames (selection highlight, health bar, name text).
		void CreateChildren();

		/// Refreshes name text, health bar progress, reaction color and selection state.
		void UpdateContent(GameUnitC& unit);

		/// Refreshes the cast bar from the unit's current cast state (progress, spell
		/// name, interrupted flash, visibility, cvar filter).
		void UpdateCastBar(GameUnitC& unit);

	private:
		Camera* m_camera = nullptr;
		ObjectGuid m_unitGuid = 0;
		InteractHandler m_interactHandler;

		/// White backing frame slightly larger than the health bar - visible only while
		/// the unit is selected, so it reads as an outline around the bar.
		FramePtr m_highlight;
		/// The reaction-colored health bar.
		std::shared_ptr<ProgressBar> m_healthBar;
		/// The unit name text above the health bar.
		FramePtr m_nameText;

		/// The cast/channel bar below the health bar - visible only while the unit is
		/// casting (or briefly flashing "Interrupted" after a failed cast).
		std::shared_ptr<ProgressBar> m_castBar;

		/// The spell whose name the cast bar currently displays, so the (truncated)
		/// caption is only recomputed when the cast changes.
		const void* m_lastCastSpell = nullptr;

		/// Whether the cast bar currently shows the interrupted flash styling.
		bool m_castBarInterrupted = false;

		/// Cached cast bar fill color so the color property is only written on changes.
		argb_t m_castBarColor = 0;

		/// The unit name the plate currently displays (before truncation), so the
		/// truncated display text is only recomputed when the name actually changes.
		String m_lastUnitName;

		/// The ui scale the display name was last fitted with, so the truncation is
		/// redone when the ui scale (and with it the rendered text width) changes.
		float m_lastNameFitScale = 0.0f;

		/// The text scale the display name was last fitted with. Tracked separately from
		/// the ui scale because glyphs scale uniformly (FrameManager::GetTextScale) while
		/// the plate width scales with the horizontal ui scale.
		float m_lastNameFitTextScale = 0.0f;

		/// Cached bar color so the color property is only written on changes.
		argb_t m_barColor = 0;

		/// Cached name text color so the color property is only written on changes.
		argb_t m_nameColor = 0;
	};
}

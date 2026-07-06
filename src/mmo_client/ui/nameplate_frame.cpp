// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "nameplate_frame.h"

#include "base/signal.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/progress_bar.h"
#include "frame_ui/property.h"
#include "game_client/game_player_c.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
#include "graphics/graphics_device.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace mmo
{
	namespace
	{
		// Vertical room (in ui units) kept between the unit's head and the plate.
		constexpr float NameplateScreenMargin = 4.0f;
		// World-space offset added above the unit's bounding box top.
		constexpr float NameplateWorldOffset = 0.25f;
		// How far the selection highlight extends beyond the health bar on each side.
		constexpr float NameplateHighlightBorder = 4.0f;
		// Opacity used for plates of units which are not the current target.
		constexpr float NameplateUnselectedOpacity = 0.85f;
		// Opacity of the highlight outline while the plate is only hovered (the selected
		// plate shows it fully opaque).
		constexpr float NameplateHoverHighlightOpacity = 0.4f;

		const std::string NameplateTemplateName("NameplateTemplate");
		const std::string NameplateNameTemplateName("NameplateNameTemplate");
		const std::string NameplateHealthBarTemplateName("NameplateHealthBarTemplate");
		const std::string NameplateHighlightTemplateName("NameplateHighlightTemplate");

		// Plater-inspired reaction colors for the health bar fill.
		constexpr argb_t HostileBarColor = 0xFFD23A3A;			// red
		constexpr argb_t NeutralBarColor = 0xFFE8B923;			// yellow
		constexpr argb_t FriendlyNpcBarColor = 0xFF3FBF3F;		// green
		constexpr argb_t FriendlyPlayerBarColor = 0xFF3F6FD9;	// blue

		/// Removes the last UTF-8 code point from a string (multi-byte aware, so
		/// truncation never leaves a broken byte sequence behind).
		void PopUtf8Char(String& text)
		{
			while (!text.empty())
			{
				const auto byte = static_cast<unsigned char>(text.back());
				text.pop_back();

				// Stop once the removed byte was a lead byte (not a continuation byte).
				if ((byte & 0xC0) != 0x80)
				{
					break;
				}
			}
		}

		/// Truncates a name with an ellipsis so it fits a single line of the given width.
		/// The text component word-wraps unconditionally, and a wrapped name would spill
		/// over the health bar below it.
		String FitNameToWidth(const String& name, const FontPtr& font, const float maxWidth)
		{
			if (!font || maxWidth <= 0.0f)
			{
				return name;
			}

			// Measure with the same scale the text component renders (and wraps) with.
			const float textScale = FrameManager::Get().GetUIScale().y;
			if (font->GetTextWidth(name, textScale) <= maxWidth)
			{
				return name;
			}

			static const String ellipsis = "...";

			String truncated = name;
			while (!truncated.empty() && font->GetTextWidth(truncated + ellipsis, textScale) > maxWidth)
			{
				PopUtf8Char(truncated);
			}

			return truncated + ellipsis;
		}

		/// Formats an ARGB color as the 8-digit hex string expected by frame properties.
		std::string ToHexColor(const argb_t argb)
		{
			std::ostringstream stream;
			stream << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << argb;
			return stream.str();
		}

		/// Returns the health bar color for a unit based on its reaction towards the player.
		argb_t GetBarColorForUnit(const GameUnitC& unit)
		{
			const auto player = ObjectMgr::GetActivePlayer();
			if (!player)
			{
				return HostileBarColor;
			}

			const bool friendly = player->IsFriendlyTo(unit);
			if (unit.IsPlayer())
			{
				return friendly ? FriendlyPlayerBarColor : HostileBarColor;
			}

			if (friendly)
			{
				return FriendlyNpcBarColor;
			}

			return player->IsHostileTo(unit) ? HostileBarColor : NeutralBarColor;
		}
	}

	NameplateFrame::NameplateFrame(const String& name, Camera& camera, const ObjectGuid unitGuid, InteractHandler interactHandler)
		: Frame("Nameplate", name)
		, m_camera(&camera)
		, m_unitGuid(unitGuid)
		, m_interactHandler(std::move(interactHandler))
	{
		ApplyTemplate();
		CreateChildren();

		// The plate itself is the click target which selects its unit. The children stay
		// non-clickable so clicks anywhere on the plate bubble up to this frame.
		SetClickable(true);

		// Refresh contents right away so the plate doesn't flash with template defaults.
		Animate(0.0f);
	}

	void NameplateFrame::ApplyTemplate()
	{
		const FramePtr templateFrame = FrameManager::Get().Find(NameplateTemplateName);
		if (!templateFrame)
		{
			ELOG("Missing nameplate template '" << NameplateTemplateName << "' - is Nameplate.xml loaded?");
			return;
		}

		templateFrame->Copy(*this);
	}

	void NameplateFrame::CreateChildren()
	{
		const FramePtr highlightTemplate = FrameManager::Get().Find(NameplateHighlightTemplateName);
		const FramePtr healthBarTemplate = FrameManager::Get().Find(NameplateHealthBarTemplateName);
		const FramePtr nameTemplate = FrameManager::Get().Find(NameplateNameTemplateName);
		if (!highlightTemplate || !healthBarTemplate || !nameTemplate)
		{
			ELOG("Missing nameplate child templates - is Nameplate.xml loaded?");
			return;
		}

		// The children are created directly (not via FrameManager) so they are owned solely
		// by this plate and destroyed together with it, instead of leaking into the global
		// frame registry (same pattern as the chat bubble tail). They are also disabled so
		// hover tracking lands on this plate frame itself instead of a child.
		m_highlight = std::make_shared<Frame>("Frame", GetName() + "_Highlight");
		highlightTemplate->Copy(*m_highlight);
		m_highlight->SetClickable(false);
		m_highlight->SetEnabled(false);
		m_highlight->SetVisible(false);

		m_healthBar = std::make_shared<ProgressBar>("ProgressBar", GetName() + "_Health");
		healthBarTemplate->Copy(*m_healthBar);
		m_healthBar->SetClickable(false);
		m_healthBar->SetEnabled(false);

		m_nameText = std::make_shared<Frame>("Frame", GetName() + "_Name");
		nameTemplate->Copy(*m_nameText);
		m_nameText->SetClickable(false);
		m_nameText->SetEnabled(false);

		// Highlight first so it renders behind the health bar and reads as an outline.
		AddChild(m_highlight);
		AddChild(m_healthBar);
		AddChild(m_nameText);

		// Name spans the top of the plate; the bar spans the bottom (height from template).
		m_nameText->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr, 0.0f);
		m_nameText->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr, 0.0f);
		m_nameText->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr, 0.0f);

		m_healthBar->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr, 0.0f);
		m_healthBar->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr, 0.0f);
		m_healthBar->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr, 0.0f);

		// The highlight wraps the health bar with a small overhang on every side.
		m_highlight->SetAnchor(anchor_point::Left, anchor_point::Left, m_healthBar, -NameplateHighlightBorder);
		m_highlight->SetAnchor(anchor_point::Right, anchor_point::Right, m_healthBar, NameplateHighlightBorder);
		m_highlight->SetAnchor(anchor_point::Top, anchor_point::Top, m_healthBar, -NameplateHighlightBorder);
		m_highlight->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, m_healthBar, NameplateHighlightBorder);
	}

	void NameplateFrame::UpdateContent(GameUnitC& unit)
	{
		if (m_nameText && m_lastUnitName != unit.GetName())
		{
			m_lastUnitName = unit.GetName();

			// Truncate to a single line: the plate width is the wrap constraint, minus a
			// small margin so the text doesn't touch the plate edges.
			const float maxNameWidth = (GetWidth() > 0.0f ? GetWidth() : 240.0f) - 8.0f;
			m_nameText->SetText(FitNameToWidth(m_lastUnitName, m_nameText->GetFont(), maxNameWidth));
		}

		if (m_healthBar)
		{
			const uint32 maxHealth = std::max<uint32>(1, unit.GetMaxHealth());
			m_healthBar->SetProgress(static_cast<float>(unit.GetHealth()) / static_cast<float>(maxHealth));

			const argb_t barColor = GetBarColorForUnit(unit);
			if (barColor != m_barColor)
			{
				m_barColor = barColor;
				if (Property* progressColor = m_healthBar->GetProperty("ProgressColor"))
				{
					progressColor->Set(ToHexColor(barColor));
				}
			}
		}

		// Make the selected unit's plate stand out: full opacity plus a highlight outline.
		// Hovered plates get a subtle preview of the same treatment (full opacity, faint
		// outline) so they respond to the mouse.
		const bool selected = ObjectMgr::GetSelectedObjectGuid() == m_unitGuid;
		const bool hovered = IsHovered();
		if (m_highlight)
		{
			m_highlight->SetVisible(selected || hovered);
			m_highlight->SetOpacity(selected ? 1.0f : NameplateHoverHighlightOpacity);
		}

		SetOpacity(selected || hovered ? 1.0f : NameplateUnselectedOpacity);
	}

	void NameplateFrame::Animate(float)
	{
		const auto unit = ObjectMgr::Get<GameUnitC>(m_unitGuid);
		if (!unit || !m_camera)
		{
			SetVisible(false);
			return;
		}

		UpdateContent(*unit);

		Vector3 worldPosition = unit->GetPosition() + Vector3::UnitY * 2.0f;
		if (const Entity* entity = unit->GetEntity())
		{
			const AABB& bounds = entity->GetWorldBoundingBox(true);
			if (!bounds.IsNull())
			{
				worldPosition.y = bounds.max.y + NameplateWorldOffset;
			}
		}

		const Vector3 cameraToPlate = worldPosition - m_camera->GetDerivedPosition();
		if (cameraToPlate.Dot(m_camera->GetDerivedDirection()) <= 0.0f)
		{
			SetVisible(false);
			return;
		}

		float normalizedX = 0.0f;
		float normalizedY = 0.0f;
		m_camera->GetNormalizedScreenPosition(worldPosition, normalizedX, normalizedY);
		if (normalizedX < 0.0f || normalizedX > 1.0f || normalizedY < 0.0f || normalizedY > 1.0f)
		{
			SetVisible(false);
			return;
		}

		int32 viewportWidth = 0;
		int32 viewportHeight = 0;
		GraphicsDevice::Get().GetViewport(nullptr, nullptr, &viewportWidth, &viewportHeight);

		const float inverseUiScale = 1.0f / FrameManager::Get().GetUIScale().y;
		const float logicalWidth = static_cast<float>(viewportWidth) * inverseUiScale;
		const float logicalHeight = static_cast<float>(viewportHeight) * inverseUiScale;

		// Centered horizontally over the head; hanging just above it. Unlike chat bubbles,
		// plates are not clamped to the screen edges - a plate whose anchor moves off
		// screen simply disappears (handled by the normalized position check above).
		const Point position(
			normalizedX * logicalWidth - GetWidth() * 0.5f,
			normalizedY * logicalHeight - GetHeight() - NameplateScreenMargin);
		SetPosition(position);

		// Re-layout the plate subtree (including the anchored children) for the new position.
		Invalidate();
		SetVisible(true);
	}

	bool NameplateFrame::OnMouseDown(const MouseButton button, const int32 buttons, const Point& position)
	{
		Frame::OnMouseDown(button, buttons, position);

		if (button == MouseButton::Left || button == MouseButton::Right)
		{
			if (const auto player = ObjectMgr::GetActivePlayer())
			{
				if (const auto unit = ObjectMgr::Get<GameUnitC>(m_unitGuid))
				{
					player->SetTargetUnit(unit);
				}
			}
		}

		// Like Button: consume the event so the click does not also start camera turning.
		abort_emission();
		return true;
	}

	bool NameplateFrame::OnMouseUp(const MouseButton button, const int32 buttons, const Point& position)
	{
		Frame::OnMouseUp(button, buttons, position);

		// Right-clicking a plate acts like right-clicking the unit itself (attack living
		// enemies, talk to friendly NPCs). Like a button click, this only triggers if the
		// button is released while still over the plate.
		if (button == MouseButton::Right && m_interactHandler && IsHovered())
		{
			if (const auto unit = ObjectMgr::Get<GameUnitC>(m_unitGuid))
			{
				m_interactHandler(*unit);
			}
		}

		abort_emission();
		return true;
	}
}

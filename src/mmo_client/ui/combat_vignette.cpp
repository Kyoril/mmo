// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_vignette.h"

#include "screen.h"

#include "base/clock.h"
#include "frame_ui/color.h"
#include "frame_ui/geometry_buffer.h"
#include "graphics/texture_mgr.h"

#include <cmath>
#include <memory>

namespace mmo
{
	namespace
	{
		ScreenLayerIt s_vignetteLayer;
		bool s_layerRegistered = false;

		std::unique_ptr<GeometryBuffer> s_vignetteBuffer;
		std::shared_ptr<Texture> s_whiteTexture;

		/// Current danger factor in [0,1]; 0 means the vignette is fully hidden.
		float s_danger = 0.0f;

		/// A solid white texture so the per-vertex color/alpha drives the vignette look.
		constexpr const char* WhiteTexturePath = "Textures/Character/T_EV_BlankWhite_01.htex";

		/// Health-fraction band, in normalized device coordinates, that each screen edge fades
		/// over (0 at the edge to fully transparent this far inward).
		constexpr float BandThickness = 0.25f;

		/// Peak edge alpha at maximum danger before the critical pulse is added.
		constexpr float PeakAlpha = 0.55f;

		/// Appends a colored quad using the same vertex winding as GeometryHelper::CreateRect
		/// (BL, TL, TR, TR, BR, BL) so it renders correctly regardless of the current cull mode.
		/// Each corner carries its own packed color, which lets a band fade from an opaque screen
		/// edge to a fully transparent inner line.
		void AppendQuad(GeometryBuffer& buffer,
			const float blx, const float bly, const uint32 blc,
			const float tlx, const float tly, const uint32 tlc,
			const float trx, const float try_, const uint32 trc,
			const float brx, const float bry, const uint32 brc)
		{
			const GeometryBuffer::Vertex verts[6]{
				{ { blx, bly, 0.0f }, blc, { 0.0f, 0.0f } },
				{ { tlx, tly, 0.0f }, tlc, { 0.0f, 0.0f } },
				{ { trx, try_, 0.0f }, trc, { 0.0f, 0.0f } },
				{ { trx, try_, 0.0f }, trc, { 0.0f, 0.0f } },
				{ { brx, bry, 0.0f }, brc, { 0.0f, 0.0f } },
				{ { blx, bly, 0.0f }, blc, { 0.0f, 0.0f } },
			};

			buffer.AppendGeometry(verts, 6);
		}
	}

	void CombatVignette::Init()
	{
		if (!s_vignetteBuffer)
		{
			s_vignetteBuffer = std::make_unique<GeometryBuffer>();
		}

		s_whiteTexture = TextureManager::Get().CreateOrRetrieve(WhiteTexturePath);

		if (!s_layerRegistered)
		{
			// Drawn below the loading screen (1000) but above the 3D world.
			s_vignetteLayer = Screen::AddLayer(&CombatVignette::Paint, 900.0f,
				ScreenLayerFlags::Disabled | ScreenLayerFlags::IdentityProjection | ScreenLayerFlags::IdentityTransform);
			s_layerRegistered = true;
		}

		s_danger = 0.0f;
	}

	void CombatVignette::Destroy()
	{
		if (s_layerRegistered)
		{
			Screen::RemoveLayer(s_vignetteLayer);
			s_layerRegistered = false;
		}

		s_vignetteBuffer.reset();
		s_whiteTexture.reset();
		s_danger = 0.0f;
	}

	void CombatVignette::SetDangerFactor(const float danger)
	{
		float clamped = danger;
		if (clamped < 0.0f)
		{
			clamped = 0.0f;
		}
		else if (clamped > 1.0f)
		{
			clamped = 1.0f;
		}

		s_danger = clamped;

		if (!s_layerRegistered)
		{
			return;
		}

		// Only paint while there is something to show.
		if (s_danger > 0.0f)
		{
			s_vignetteLayer->flags &= ~ScreenLayerFlags::Disabled;
		}
		else
		{
			s_vignetteLayer->flags |= ScreenLayerFlags::Disabled;
		}
	}

	void CombatVignette::Paint()
	{
		if (s_danger <= 0.0f || !s_vignetteBuffer || !s_whiteTexture)
		{
			return;
		}

		// Base intensity scales with danger. When critically low, add a soft heartbeat pulse.
		float edgeAlpha = s_danger * PeakAlpha;
		if (s_danger > 0.6f)
		{
			const float t = static_cast<float>(GetAsyncTimeMs()) / 1000.0f;
			const float pulse = 0.5f + 0.5f * std::sin(t * 6.0f);
			edgeAlpha += (s_danger - 0.6f) * 0.35f * pulse;
		}

		if (edgeAlpha > 0.9f)
		{
			edgeAlpha = 0.9f;
		}

		const uint32 edge = Color(0.85f, 0.06f, 0.06f, edgeAlpha).GetARGB();
		const uint32 inner = Color(0.85f, 0.06f, 0.06f, 0.0f).GetARGB();

		s_vignetteBuffer->Reset();
		s_vignetteBuffer->SetActiveTexture(s_whiteTexture);

		constexpr float e = 1.0f;      // screen edge in NDC
		const float b = BandThickness; // inward fade distance

		// Each band is opaque at the screen edge and transparent at the inner line. The four
		// bands overlap in the corners, which naturally intensifies them there.

		// Left edge (x = -e) fading inward to x = -e + b.
		AppendQuad(*s_vignetteBuffer,
			-e, e, edge, -e, -e, edge, -e + b, -e, inner, -e + b, e, inner);
		// Right edge (x = e) fading inward to x = e - b.
		AppendQuad(*s_vignetteBuffer,
			e - b, e, inner, e - b, -e, inner, e, -e, edge, e, e, edge);
		// Top edge (y = -e) fading inward to y = -e + b.
		AppendQuad(*s_vignetteBuffer,
			-e, -e + b, inner, -e, -e, edge, e, -e, edge, e, -e + b, inner);
		// Bottom edge (y = e) fading inward to y = e - b.
		AppendQuad(*s_vignetteBuffer,
			-e, e, edge, -e, e - b, inner, e, e - b, inner, e, e, edge);

		s_vignetteBuffer->Draw();
	}
}

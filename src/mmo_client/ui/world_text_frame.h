// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/frame.h"

namespace mmo
{
	class Camera;

	/// Controls how a world text frame enters, moves, and expires.
	enum class WorldTextAnimation
	{
		Normal,
		Critical
	};

	/// Frame for rendering the actual 3d game world.
	class WorldTextFrame final
		: public Frame
	{
	public:
		/// Creates floating text at a position in the game world.
		explicit WorldTextFrame(Camera& camera, const Vector3& position, float duration, const Color& color = Color::White, WorldTextAnimation animation = WorldTextAnimation::Normal);

	public:

		const Vector3& GetWorldPosition() const { return m_worldPosition; }

		float GetDuration() const { return m_duration; }

		float GetLifetime() const { return m_lifetime; }

		bool IsExpired() const { return m_lifetime >= m_duration; }

		void Update(float elapsed) override;

	protected:
		void OnTextChanged() override;

		void PopulateGeometryBuffer() override;

	private:
		Camera* m_camera;

		Vector3 m_worldPosition;

		float m_duration;

		float m_lifetime;

		Point m_offset;

		Color m_textColor;

		WorldTextAnimation m_animation;

		float m_textScale;

		float m_baseTextWidth;

		float m_baseTextHeight;

		float m_horizontalDirection;
	};
}

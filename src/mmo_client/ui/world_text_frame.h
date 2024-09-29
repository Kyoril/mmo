// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/frame.h"

namespace mmo
{
	class Camera;

	/// Frame for rendering the actual 3d game world.
	class WorldTextFrame final
		: public Frame
	{
	public:
		/// Default constructor.
		explicit WorldTextFrame(Camera& camera, const Vector3& position, float duration);

	public:

		const Vector3& GetWorldPosition() const { return m_worldPosition; }

		float GetDuration() const { return m_duration; }

		float GetLifetime() const { return m_lifetime; }

		bool IsExpired() const { return m_lifetime >= m_duration; }

		void Update(float elapsed) override;

	protected:
		void OnTextChanged() override;

	private:
		Camera* m_camera;

		Vector3 m_worldPosition;

		float m_duration;

		float m_lifetime;

		Point m_offset;
	};
}
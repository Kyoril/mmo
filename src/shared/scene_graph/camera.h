// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "movable_object.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/radian.h"

namespace mmo
{
	class Scene;
	class Sphere;

	/// This class represents a camera inside of a scene. A camera is a special type
	/// of movable object, which allows to collect all renderable objects inside of it's
	/// view frustum.
	class Camera final
		: public MovableObject
		, public NonCopyable
	{
	public:
		Camera(const String& name);

	public:
		[[nodiscard]] const Matrix4& GetProjectionMatrix() const;

		[[nodiscard]] const Matrix4& GetViewMatrix() const;

		[[nodiscard]] float GetNearClipDistance() const noexcept { return m_nearDist; }

		[[nodiscard]] float GetFarClipDistance() const noexcept { return m_farDist; }

		[[nodiscard]] float GetAspectRatio() const noexcept { return m_aspect; }

		void SetOrientation(const Quaternion& quaternion);

	protected:
		void UpdateFrustum() const;

		const Quaternion& GetOrientationForViewUpdate() const { return m_lastParentOrientation; }

		const Vector3& GetPositionForViewUpdate() const { return m_lastParentPosition; }

		void UpdateView() const;

		bool IsViewOutOfDate() const;

		bool IsFrustumOutOfDate() const;
		
		void CalcProjectionParameters(float& left, float& right, float& bottom, float& top) const;

	public:
		[[nodiscard]] const String& GetMovableType() const override;
		[[nodiscard]] const AABB& GetBoundingBox() const override;
		[[nodiscard]] float GetBoundingRadius() const override;
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

		void SetAspectRatio(const float aspect);

        const Quaternion& GetDerivedOrientation() const;

        const Vector3& GetDerivedPosition() const;

		void SetFillMode(const FillMode fillMode) { m_fillMode = fillMode; }

		[[nodiscard]] FillMode GetFillMode() const noexcept { return m_fillMode; }

		bool IsVisible(const Sphere& bound) const;

	public:
		void PopulateRenderQueue(RenderQueue& queue) override {}

	private:
		Radian m_fovY;
		float m_farDist { 1000.0f };
		float m_nearDist { 0.01f };
		float m_aspect;
		float m_orthoHeight;
		mutable Quaternion m_lastParentOrientation;
		mutable Vector3 m_lastParentPosition;
		mutable Matrix4 m_projMatrix;
		mutable Matrix4 m_viewMatrix;
		mutable bool m_viewInvalid;
		mutable bool m_recalcView { true };
		float m_left, m_right, m_top, m_bottom;
		FillMode m_fillMode { FillMode::Solid };
		mutable bool m_recalcFrustum { true };
		mutable bool m_recalcFrustumPlanes { true };
		mutable bool m_recalcWorldSpaceCorners { true };
		bool m_obliqueDepthProjection { false };
	};
}

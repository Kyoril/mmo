#pragma once

#include "movable_object.h"
#include "frame_ui/color.h"

namespace mmo
{
	enum class LightType
	{
		Point,
		Directional,
		Spot,
	};

	class Light : public MovableObject
	{
	public:
		Light();

		explicit Light(const String& name);

		virtual ~Light() override;

	public:
		void SetType(const LightType& type) noexcept { m_lightType = type; }

		[[nodiscard]] LightType GetType() const noexcept { return m_lightType; }
		
		void SetColor(float red, float green, float blue);

		void SetColor(const Color& color);

		[[nodiscard]] const Color& GetColor() const noexcept { return m_color; }

		void SetAttenuation(float range, float constant, float linear, float quadratic);

		[[nodiscard]] float GetAttenuationRange() const noexcept { return m_range; }

		[[nodiscard]] float GetAttenuationConstant() const noexcept { return m_attenuationConst; }

		[[nodiscard]] float GetAttenuationLinear() const noexcept { return m_attenuationLinear; }

		[[nodiscard]]  float GetAttenuationQuadratic() const noexcept { return m_attenuationQuad; }

		void SetPosition(float x, float y, float z);

		void SetPosition(const Vector3& position);

		[[nodiscard]] const Vector3& GetPosition() const noexcept { return m_position; }

		void SetDirection(float x, float y, float z);

		void SetDirection(const Vector3& direction);

		[[nodiscard]] const Vector3& GetDirection() const noexcept { return m_direction; }

		void SetSpotlightRange(const Radian& innerAngle, const Radian& outerAngle, float falloff = 1.0f);

		[[nodiscard]] const Radian& GetSpotlightInnerAngle() const noexcept { return m_spotInner; }

		[[nodiscard]] const Radian& GetSpotlightOuterAngle() const noexcept { return m_spotOuter; }

		[[nodiscard]] float GetSpotlightFalloff() const noexcept { return m_spotFalloff; }

		void SetSpotlightInnerAngle(const Radian& value) noexcept { m_spotInner = value; }

		void SetSpotlightOuterAngle(const Radian& value) noexcept { m_spotOuter = value; }

		void SetSpotlightFalloff(const float value) noexcept { m_spotFalloff = value; }

		void SetSpotlightNearClipDistance(const float nearClip) noexcept { m_spotNearClip = nearClip; }

		[[nodiscard]] float GetSpotlightNearClipDistance() const noexcept { return m_spotNearClip; }

		void SetPowerScale(const float value) noexcept { m_powerScale = value; }

		[[nodiscard]] float GetPowerScale() const noexcept { return m_powerScale; }
		
		[[nodiscard]] const Vector3& GetDerivedPosition(bool cameraRelativeIfSet = false) const;

		[[nodiscard]] const Vector3& GetDerivedDirection() const;

		[[nodiscard]] uint32 GetIndexInFrame() const noexcept { return m_indexInFrame; }

		void NotifyIndexInFrame(const uint32 index) noexcept { m_indexInFrame = index; }

		void SetShadowFarDistance(float distance);

		void ResetShadowFarDistance() noexcept { m_ownShadowFarDist = false; }

		[[nodiscard]] float GetShadowFarDistance() const;

		[[nodiscard]] float GetShadowFarDistanceSquared() const;

		void SetShadowNearClipDistance(const float nearClip) noexcept { m_shadowNearClip = nearClip; }

		[[nodiscard]] float GetShadowNearClipDistance() const noexcept { return m_shadowNearClip; }

		[[nodiscard]] float DeriveShadowNearClipDistance(const Camera& camera) const;

		void SetShadowFarClipDistance(const float farClip) noexcept { m_shadowFarClip = farClip; }

		[[nodiscard]] float GetShadowFarClipDistance() const noexcept { return m_shadowFarClip; }

		[[nodiscard]] float DeriveShadowFarClipDistance(const Camera& camera) const;

		void SetCameraRelative(Camera* camera);

		[[nodiscard]] bool IsInLightRange(const Sphere& sphere) const;

		[[nodiscard]] bool IsInLightRange(const AABB& aabb) const;

	public:
		virtual void NotifyAttachmentChanged(SceneNode* parent, bool isTagPoint) override;

		virtual void NotifyMoved() override;

		[[nodiscard]] virtual const AABB& GetBoundingBox() const override;

		virtual void PopulateRenderQueue(RenderQueue& queue) override;

		[[nodiscard]] virtual const String& GetMovableType() const override;
		
		[[nodiscard]] virtual float GetBoundingRadius() const override;

		virtual void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

	protected:
		virtual void Update() const;

	protected:
		LightType m_lightType { LightType::Point };
		Vector3 m_position;
		Color m_color { Color::White };
		Vector3 m_direction { Vector3::UnitZ };
		Radian m_spotOuter { Degree(40.0f) };
		Radian m_spotInner { Degree(30.0f) };
		float m_spotFalloff { 1.0f };
		float m_spotNearClip { 0.0f };
		float m_range { 100000.0f };
		float m_attenuationConst { 1.0f };
		float m_attenuationLinear { 0.0f };
		float m_attenuationQuad { 0.0f };
		float m_powerScale { 1.0f };
		uint32 m_indexInFrame { 0 };
		bool m_ownShadowFarDist { false };
		float m_shadowFarDist { 0.0f };
		float m_shadowFarDistSquared { 0.0f };
		float m_shadowNearClip { -1.0f };
		float m_shadowFarClip { -1.0f };

		mutable Vector3 m_derivedPosition;
		mutable Vector3 m_derivedDirection { Vector3::UnitZ };
		mutable Vector3 m_derivedCamRelativePosition;
		mutable bool m_derivedCamRelativeDirty { false };
		Camera* m_cameraToBeRelativeTo { nullptr };
		mutable bool m_derivedTransformDirty { false };
	};
}

#pragma once

#include <memory>

#include "math/quaternion.h"
#include "math/vector3.h"

namespace mmo
{
	class AnimationTrack;

	class KeyFrame
	{
    public:
        KeyFrame(const AnimationTrack& parent, float time);
        virtual ~KeyFrame() = default;

	public:
        float GetTime() const { return m_time; }

        virtual std::shared_ptr<KeyFrame> Clone(AnimationTrack& parent) const;

    protected:
        float m_time;
        const AnimationTrack* m_parentTrack;
	};

	class TransformKeyFrame : public KeyFrame
	{
	public:
		TransformKeyFrame(const AnimationTrack& parent, float time);
		virtual ~TransformKeyFrame() override = default;

	public:
		virtual void SetTranslate(const Vector3& trans);

		const Vector3& GetTranslate() const { return m_translate; }

		virtual void SetScale(const Vector3& scale);

		virtual const Vector3& GetScale() const { return m_scale; }

		virtual void SetRotation(const Quaternion& rot);

		virtual const Quaternion& GetRotation() const { return m_rotation; }

		std::shared_ptr<KeyFrame> Clone(AnimationTrack& newParent) const override;

	protected:
		Vector3 m_translate{ Vector3::Zero };
		Vector3 m_scale{ Vector3::UnitScale };
		Quaternion m_rotation{ Quaternion::Identity };


	};
}

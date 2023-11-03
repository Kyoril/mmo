#pragma once
#include "base/typedefs.h"

namespace mmo
{
	class Animation;

	class TimeIndex
	{
	protected:
		float m_timePos;

		uint32 m_keyIndex;

		static const uint32 InvalidKeyIndex = 0xFFFFFFFF;

	public:
		TimeIndex(float timePos)
			: m_timePos(timePos)
			, m_keyIndex(InvalidKeyIndex)
		{
		}

		TimeIndex(float timePos, uint32 keyIndex)
			: m_timePos(timePos)
			, m_keyIndex(keyIndex)
		{
		}

		bool HasKeyIndex() const
		{
			return m_keyIndex != InvalidKeyIndex;
		}

		float GetTimePos() const
		{
			return m_timePos;
		}

		uint32 GetKeyIndex() const
		{
			return m_keyIndex;
		}
	};

	class AnimationTrack
	{
	public:

	};

	enum class VertexAnimationType : uint8
	{
		None,
		Morph,
		Pose
	};

	class VertexAnimationTrack : public AnimationTrack
	{
	public:
		/** The target animation mode */
		enum class TargetMode : uint8
		{
			Software,
			Hardware,
		};


	};
}

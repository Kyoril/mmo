#pragma once

#include "key_frame.h"
#include "node.h"
#include "simple_spline.h"
#include "base/typedefs.h"
#include "base/signal.h"

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
	protected:
		typedef std::vector<KeyFramePtr> KeyFrameList;
		KeyFrameList m_keyFrames;

		Animation& m_parent;
		uint16 m_handle;

		typedef std::vector<uint16> KeyFrameIndexMap;
		KeyFrameIndexMap m_keyFrameIndexMap;

	public:
		AnimationTrack(Animation& parent, uint16 handle);

		virtual ~AnimationTrack() = default;

		virtual void KeyFrameDataChanged() const {}

		[[nodiscard]] uint16 GetHandle() const { return m_handle; }

		[[nodiscard]] virtual uint16 GetNumKeyFrames() const;

		[[nodiscard]] virtual KeyFramePtr GetKeyFrame(uint16 index) const;

		virtual float GetKeyFramesAtTime(const TimeIndex& timeIndex, KeyFrame** keyFrame1, KeyFrame** keyFrame2, uint16* firstKeyIndex = nullptr) const;

		virtual KeyFramePtr CreateKeyFrame(float timePos);

		virtual void RemoveKeyFrame(uint16 index);

		virtual void RemoveAllKeyFrames();

		virtual void GetInterpolatedKeyFrame(const TimeIndex& timeIndex, KeyFrame& kf) const = 0;

		virtual void Apply(const TimeIndex& timeIndex, float weight = 1.0f, float scale = 1.0f) = 0;

		[[nodiscard]] virtual bool HasNonZeroKeyFrames() const { return true; }

		virtual void Optimize() {}

		virtual void CollectKeyFrameTimes(std::vector<float>& keyFrameTimes);

		virtual void BuildKeyFrameIndexMap(const std::vector<float>& keyFrameTimes);

		virtual void ApplyBaseKeyFrame(const KeyFrame* base);

		[[nodiscard]] Animation& GetParent() const { return m_parent; }

	protected:
		virtual KeyFramePtr CreateKeyFrameImpl(float time) = 0;

		virtual void PopulateClone(AnimationTrack* clone) const;
	};

	class NodeAnimationTrack : public AnimationTrack
	{
	public:
		NodeAnimationTrack(Animation& parent, uint16 handle);
		NodeAnimationTrack(Animation& parent, uint16 handle, Node& targetNode);

	public:
		virtual std::shared_ptr<TransformKeyFrame> CreateNodeKeyFrame(float timePos);

		virtual Node* GetAssociatedNode() const;

		virtual void SetAssociatedNode(Node* node);

		virtual void ApplyToNode(Node& node, const TimeIndex& timeIndex, float weight = 1.0f, float scale = 1.0f);

		virtual void SetUseShortestRotationPath(bool useShortestPath);

		virtual bool UsesShortestRotationPath() const;

		void GetInterpolatedKeyFrame(const TimeIndex& timeIndex, KeyFrame& kf) const override;

		void Apply(const TimeIndex& timeIndex, float weight = 1.0f, float scale = 1.0f) override;

		void KeyFrameDataChanged() const override;

		virtual std::shared_ptr<TransformKeyFrame> GetNodeKeyFrame(uint16 index) const;

		bool HasNonZeroKeyFrames() const override;

		void Optimize() override;

		virtual NodeAnimationTrack* Clone(Animation& newParent) const;

		void ApplyBaseKeyFrame(const KeyFrame* base) override;

	protected:
		KeyFramePtr CreateKeyFrameImpl(float time) override;

		virtual void BuildInterpolationSplines() const;

		struct Splines
		{
			SimpleSpline positionSpline;
			SimpleSpline scaleSpline;
			RotationalSpline rotationSpline;
		};

		Node* m_targetNode;
		mutable std::unique_ptr<Splines> m_splines;
		mutable bool m_splineBuildNeeded;
		mutable bool m_useShortestRotationPath;
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

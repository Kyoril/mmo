// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "log/default_log_levels.h"

#include <string>
#include <vector>
#include <memory>
#include <map>

#include "lua.hpp"
#include "luabind/object.hpp"


namespace mmo
{
	class Frame;

	/// A single keyframe in an animation track, holding a time offset and float value.
	struct FrameAnimationKeyframe
	{
		float time{ 0.0f };
		float value{ 0.0f };
	};

	/// Animates a single numeric property on a target frame over time.
	class FrameAnimationTrack
	{
	public:
		/// Animatable frame properties.
		enum class Property
		{
			Opacity,
			PositionX,
			PositionY,
			Width,
			Height,
			ColorR,
			ColorG,
			ColorB,
			ColorA,
		};

	public:
		FrameAnimationTrack() = default;
		FrameAnimationTrack(Property property, std::string targetFrame = "");

		/// Inserts a keyframe sorted by time.
		void AddKeyframe(float time, float value);

		/// Returns the linearly interpolated value at the given time.
		float GetValueAtTime(float time) const;

		/// Applies the value at the given time to the appropriate property of the target frame.
		void Apply(float time, Frame& owningFrame) const;

		Property GetProperty() const { return m_property; }
		void SetProperty(Property property) { m_property = property; }

		const std::string& GetTargetFrame() const { return m_targetFrame; }
		void SetTargetFrame(const std::string& name) { m_targetFrame = name; }

	private:
		Property m_property{ Property::Opacity };
		std::string m_targetFrame;
		std::vector<FrameAnimationKeyframe> m_keyframes;
	};

	/// Maps a property name string to the FrameAnimationTrack::Property enum value.
	FrameAnimationTrack::Property AnimationPropertyFromString(const std::string& name);


	/// A named animation composed of one or more tracks, optionally looping.
	/// Owned by a Frame; provides C++ signals and Lua callbacks for lifecycle events.
	class FrameAnimation
	{
	public:
		/// Fired when the animation starts playing (via Play()).
		signal<void(FrameAnimation&)> Started;

		/// Fired when the animation plays to completion (non-looping only).
		signal<void(FrameAnimation&)> Finished;

		/// Fired when the animation is explicitly stopped via Stop().
		signal<void(FrameAnimation&)> Stopped;

		/// Fired at the end of each loop iteration (looping animations only).
		signal<void(FrameAnimation&)> LoopFinished;

	public:
		FrameAnimation() = default;
		FrameAnimation(std::string name, float duration, bool looping = false);

		/// Creates a deep copy of this animation without preserving play state.
		/// @param targetRemapping Optional mapping from old child names to new child names for track targets.
		std::unique_ptr<FrameAnimation> Clone(const std::map<std::string, std::string>* targetRemapping = nullptr) const;

		/// Adds a track to this animation.
		void AddTrack(FrameAnimationTrack track);

		/// Starts playing the animation from the beginning.
		void Play();

		/// Stops the animation and resets to time zero.
		void Stop();

		/// Advances playback by elapsed seconds and applies all tracks to owningFrame.
		void Update(float elapsed, Frame& owningFrame);

		const std::string& GetName() const { return m_name; }
		void SetName(std::string name) { m_name = std::move(name); }

		float GetDuration() const { return m_duration; }
		void SetDuration(float duration) { m_duration = duration; }

		bool IsLooping() const { return m_looping; }
		void SetLooping(bool looping) { m_looping = looping; }

		bool IsPlaying() const { return m_playing; }

		float GetCurrentTime() const { return m_currentTime; }

		void SetOnStart(const luabind::object& fn) { m_onStart = fn; }
		void SetOnFinish(const luabind::object& fn) { m_onFinish = fn; }
		void SetOnStop(const luabind::object& fn) { m_onStop = fn; }
		void SetOnLoop(const luabind::object& fn) { m_onLoop = fn; }

	private:
		void FireLuaCallback(luabind::object fn);

		void ApplyTracks(Frame& owningFrame);

	private:
		std::string m_name;
		float m_duration{ 1.0f };
		bool m_looping{ false };
		bool m_playing{ false };
		float m_currentTime{ 0.0f };
		std::vector<FrameAnimationTrack> m_tracks;

		luabind::object m_onStart{};
		luabind::object m_onFinish{};
		luabind::object m_onStop{};
		luabind::object m_onLoop{};
	};
}

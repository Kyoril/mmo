// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mmo
{
	namespace cutscene_flags
	{
		enum Type
		{
			None = 0x00,

			/// The player may end the cutscene early.
			Skippable = 0x01,
		};
	}

	namespace cutscene_end_reason
	{
		enum Type : uint8
		{
			/// The cutscene played to its end.
			Finished = 0,

			/// The player skipped it.
			Skipped = 1,

			/// The server ended it early (the character moved, died, left the world, ...).
			Cancelled = 2,

			Count_
		};
	}

	namespace cutscene_interpolation
	{
		enum Type
		{
			/// A smooth curve through the keyframes.
			Smooth = 0,

			/// Straight lines between the keyframes.
			Linear = 1,
		};
	}

	/// The camera of a cutscene at one moment of its timeline.
	struct CutsceneSample final
	{
		/// Eye position in world units.
		Vector3 position;

		/// Heading in degrees. 0 looks north (-Z), 90 west (-X), 180 south, 270 east.
		float yaw = 0.0f;

		/// Elevation in degrees. Negative looks down.
		float pitch = 0.0f;

		/// Vertical field of view in degrees, or 0 to keep the player camera's field of view.
		float fov = 0.0f;

		/// Index of the shot the moment falls into.
		int32 shot = 0;

		/// How far the picture is faded to black: 0 = clear, 1 = black.
		float fade = 0.0f;
	};

	/// Length of a shot in milliseconds: the time of its last keyframe. A shot without keyframes has
	/// no length and is skipped during playback.
	template <class Shot>
	uint32 GetCutsceneShotDuration(const Shot& shot)
	{
		uint32 duration = 0;
		for (int i = 0; i < shot.keyframes_size(); ++i)
		{
			duration = std::max(duration, static_cast<uint32>(shot.keyframes(i).time()));
		}

		return duration;
	}

	/// Length of a whole cutscene in milliseconds: its shots one after another.
	template <class Entry>
	uint32 GetCutsceneDuration(const Entry& entry)
	{
		uint32 duration = 0;
		for (int i = 0; i < entry.shots_size(); ++i)
		{
			duration += GetCutsceneShotDuration(entry.shots(i));
		}

		return duration;
	}

	/// Whether a cutscene has anything to play: at least one shot with a length.
	template <class Entry>
	bool IsCutscenePlayable(const Entry& entry)
	{
		return GetCutsceneDuration(entry) > 0;
	}

	namespace cutscene_detail
	{
		/// Cubic Hermite interpolation between p1 and p2 with tangents m1 and m2 (already scaled to the
		/// segment length), t in 0..1.
		template <class T>
		T Hermite(const T& p1, const T& m1, const T& p2, const T& m2, const float t)
		{
			const float t2 = t * t;
			const float t3 = t2 * t;
			return p1 * (2.0f * t3 - 3.0f * t2 + 1.0f) + m1 * (t3 - 2.0f * t2 + t) + p2 * (-2.0f * t3 + 3.0f * t2) + m2 * (t3 - t2);
		}

		/// Interpolates a channel of values given at ascending times. Tangents are finite differences over
		/// the neighbouring keys, so the curve passes through every key and keeps its speed across them
		/// even when the keys are unevenly spaced in time.
		template <class T>
		T SampleChannel(const std::vector<float>& times, const std::vector<T>& values, const float time, const bool linear)
		{
			const size_t count = values.size();
			if (count == 1 || time <= times.front())
			{
				return values.front();
			}

			if (time >= times.back())
			{
				return values.back();
			}

			size_t next = 1;
			while (next < count - 1 && times[next] <= time)
			{
				++next;
			}

			const size_t prev = next - 1;
			const float span = times[next] - times[prev];
			if (span <= 0.0f)
			{
				return values[next];
			}

			const float t = (time - times[prev]) / span;
			if (linear)
			{
				return values[prev] + (values[next] - values[prev]) * t;
			}

			const auto tangent = [&](const size_t index) -> T
			{
				const size_t before = index == 0 ? 0 : index - 1;
				const size_t after = index + 1 >= count ? count - 1 : index + 1;
				const float dt = times[after] - times[before];
				if (dt <= 0.0f)
				{
					return values[index] - values[index];
				}

				return (values[after] - values[before]) * (span / dt);
			};

			return Hermite(values[prev], tangent(prev), values[next], tangent(next), t);
		}
	}

	/// Evaluates the camera of a single shot.
	/// @param shot The shot. Must have at least one keyframe.
	/// @param shotTimeMs Milliseconds from the start of the shot.
	/// @param out Receives position, yaw, pitch and field of view. shot and fade are left alone.
	template <class Shot>
	void SampleCutsceneShot(const Shot& shot, const float shotTimeMs, CutsceneSample& out)
	{
		const int count = shot.keyframes_size();

		std::vector<float> times;
		std::vector<Vector3> positions;
		std::vector<float> yaws, pitches, fovs;
		times.reserve(count);
		positions.reserve(count);
		yaws.reserve(count);
		pitches.reserve(count);
		fovs.reserve(count);

		for (int i = 0; i < count; ++i)
		{
			const auto& key = shot.keyframes(i);
			times.push_back(static_cast<float>(key.time()));
			positions.emplace_back(key.x(), key.y(), key.z());
			pitches.push_back(key.pitch());
			fovs.push_back(key.fov());

			// Unwrap the heading so the camera always turns the short way from one keyframe to the next.
			float yaw = key.yaw();
			if (!yaws.empty())
			{
				const float previous = yaws.back();
				yaw = previous + std::remainder(yaw - previous, 360.0f);
			}
			yaws.push_back(yaw);
		}

		const bool linear = shot.interpolation() == cutscene_interpolation::Linear;
		out.position = cutscene_detail::SampleChannel(times, positions, shotTimeMs, linear);
		out.yaw = cutscene_detail::SampleChannel(times, yaws, shotTimeMs, linear);
		out.pitch = cutscene_detail::SampleChannel(times, pitches, shotTimeMs, linear);

		// A field of view of 0 means "the player camera's": never blend towards it numerically.
		const bool anyDefaultFov = std::any_of(fovs.begin(), fovs.end(), [](const float fov) { return fov <= 0.0f; });
		out.fov = anyDefaultFov ? 0.0f : cutscene_detail::SampleChannel(times, fovs, shotTimeMs, linear);
	}

	/// Evaluates the camera of a cutscene at a moment of its timeline.
	/// @param entry The cutscene.
	/// @param timeMs Milliseconds from the start of the cutscene. Clamped to its length.
	/// @param out Receives the camera.
	/// @return false if the cutscene has nothing to play, in which case out is untouched.
	template <class Entry>
	bool SampleCutscene(const Entry& entry, const float timeMs, CutsceneSample& out)
	{
		int lastShot = -1;
		float shotStart = 0.0f;
		float lastShotStart = 0.0f;

		for (int i = 0; i < entry.shots_size(); ++i)
		{
			const auto& shot = entry.shots(i);
			const float duration = static_cast<float>(GetCutsceneShotDuration(shot));
			if (shot.keyframes_size() == 0 || duration <= 0.0f)
			{
				continue;
			}

			lastShot = i;
			lastShotStart = shotStart;
			if (timeMs < shotStart + duration)
			{
				break;
			}

			shotStart += duration;
		}

		if (lastShot < 0)
		{
			return false;
		}

		const auto& shot = entry.shots(lastShot);
		const float duration = static_cast<float>(GetCutsceneShotDuration(shot));
		const float shotTime = std::clamp(timeMs - lastShotStart, 0.0f, duration);

		SampleCutsceneShot(shot, shotTime, out);
		out.shot = lastShot;

		float fade = 0.0f;
		if (shot.fade_in() > 0 && shotTime < static_cast<float>(shot.fade_in()))
		{
			fade = std::max(fade, 1.0f - shotTime / static_cast<float>(shot.fade_in()));
		}
		if (shot.fade_out() > 0 && duration - shotTime < static_cast<float>(shot.fade_out()))
		{
			fade = std::max(fade, 1.0f - (duration - shotTime) / static_cast<float>(shot.fade_out()));
		}
		out.fade = std::clamp(fade, 0.0f, 1.0f);

		return true;
	}
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "frame_animation.h"
#include "frame.h"
#include "color.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#include <algorithm>


namespace mmo
{
	// ---------------------------------------------------------------------------
	// FrameAnimationTrack
	// ---------------------------------------------------------------------------

	FrameAnimationTrack::FrameAnimationTrack(Property property, std::string targetFrame)
		: m_property(property)
		, m_targetFrame(std::move(targetFrame))
	{
	}

	void FrameAnimationTrack::AddKeyframe(float time, float value)
	{
		FrameAnimationKeyframe kf{ time, value };
		auto it = std::lower_bound(m_keyframes.begin(), m_keyframes.end(), kf,
			[](const FrameAnimationKeyframe& a, const FrameAnimationKeyframe& b) { return a.time < b.time; });
		m_keyframes.insert(it, kf);
	}

	float FrameAnimationTrack::GetValueAtTime(float time) const
	{
		if (m_keyframes.empty())
		{
			return 0.0f;
		}

		if (m_keyframes.size() == 1)
		{
			return m_keyframes[0].value;
		}

		if (time <= m_keyframes.front().time)
		{
			return m_keyframes.front().value;
		}

		if (time >= m_keyframes.back().time)
		{
			return m_keyframes.back().value;
		}

		for (size_t i = 0; i < m_keyframes.size() - 1; ++i)
		{
			const auto& kf1 = m_keyframes[i];
			const auto& kf2 = m_keyframes[i + 1];

			if (time >= kf1.time && time <= kf2.time)
			{
				const float t = (time - kf1.time) / (kf2.time - kf1.time);
				return kf1.value + t * (kf2.value - kf1.value);
			}
		}

		return m_keyframes.back().value;
	}

	void FrameAnimationTrack::Apply(float time, Frame& owningFrame) const
	{
		Frame* target = &owningFrame;

		if (!m_targetFrame.empty())
		{
			const auto found = owningFrame.FindChild(m_targetFrame);
			if (!found)
			{
				return;
			}
			target = found.get();
		}

		const float value = GetValueAtTime(time);

		switch (m_property)
		{
		case Property::Opacity:
			target->SetOpacity(value);
			break;
		case Property::PositionX:
			target->SetPosition(Point(value, target->GetPosition().y));
			break;
		case Property::PositionY:
			target->SetPosition(Point(target->GetPosition().x, value));
			break;
		case Property::Width:
			target->SetWidth(value);
			break;
		case Property::Height:
			target->SetHeight(value);
			break;
		case Property::ColorR:
		{
			Color c = target->GetColor();
			c.SetRed(value);
			target->SetColor(c);
			break;
		}
		case Property::ColorG:
		{
			Color c = target->GetColor();
			c.SetGreen(value);
			target->SetColor(c);
			break;
		}
		case Property::ColorB:
		{
			Color c = target->GetColor();
			c.SetBlue(value);
			target->SetColor(c);
			break;
		}
		case Property::ColorA:
		{
			Color c = target->GetColor();
			c.SetAlpha(value);
			target->SetColor(c);
			break;
		}
		default:
			break;
		}
	}

	FrameAnimationTrack::Property AnimationPropertyFromString(const std::string& name)
	{
		if (name == "Opacity")   return FrameAnimationTrack::Property::Opacity;
		if (name == "PositionX") return FrameAnimationTrack::Property::PositionX;
		if (name == "PositionY") return FrameAnimationTrack::Property::PositionY;
		if (name == "Width")     return FrameAnimationTrack::Property::Width;
		if (name == "Height")    return FrameAnimationTrack::Property::Height;
		if (name == "ColorR")    return FrameAnimationTrack::Property::ColorR;
		if (name == "ColorG")    return FrameAnimationTrack::Property::ColorG;
		if (name == "ColorB")    return FrameAnimationTrack::Property::ColorB;
		if (name == "ColorA")    return FrameAnimationTrack::Property::ColorA;

		WLOG("Unknown animation track property '" << name << "', defaulting to Opacity");
		return FrameAnimationTrack::Property::Opacity;
	}


	// ---------------------------------------------------------------------------
	// FrameAnimation
	// ---------------------------------------------------------------------------

	FrameAnimation::FrameAnimation(std::string name, float duration, bool looping)
		: m_name(std::move(name))
		, m_duration(duration)
		, m_looping(looping)
	{
	}

	std::unique_ptr<FrameAnimation> FrameAnimation::Clone(const std::map<std::string, std::string>* targetRemapping) const
	{
		auto clone = std::make_unique<FrameAnimation>();
		clone->m_name = m_name;
		clone->m_duration = m_duration;
		clone->m_looping = m_looping;
		clone->m_tracks = m_tracks;
		clone->m_onStart = m_onStart;
		clone->m_onFinish = m_onFinish;
		clone->m_onStop = m_onStop;
		clone->m_onLoop = m_onLoop;
		// Play state is intentionally not copied

		if (targetRemapping)
		{
			for (auto& track : clone->m_tracks)
			{
				const std::string& originalTarget = track.GetTargetFrame();
				if (!originalTarget.empty())
				{
					const auto it = targetRemapping->find(originalTarget);
					if (it != targetRemapping->end())
					{
						track.SetTargetFrame(it->second);
					}
				}
			}
		}

		return clone;
	}

	void FrameAnimation::AddTrack(FrameAnimationTrack track)
	{
		m_tracks.push_back(std::move(track));
	}

	void FrameAnimation::Play()
	{
		m_playing = true;
		m_currentTime = 0.0f;

		Started(*this);
		FireLuaCallback(m_onStart);
	}

	void FrameAnimation::Stop()
	{
		if (!m_playing)
		{
			return;
		}

		m_playing = false;
		m_currentTime = 0.0f;

		Stopped(*this);
		FireLuaCallback(m_onStop);
	}

	void FrameAnimation::Update(float elapsed, Frame& owningFrame)
	{
		if (!m_playing)
		{
			return;
		}

		m_currentTime += elapsed;

		if (m_currentTime >= m_duration)
		{
			if (m_looping)
			{
				// Wrap the time, apply, then fire loop signal
				while (m_currentTime >= m_duration)
				{
					m_currentTime -= m_duration;
				}

				ApplyTracks(owningFrame);

				LoopFinished(*this);
				FireLuaCallback(m_onLoop);
			}
			else
			{
				// Clamp to end, apply final values, then stop
				m_currentTime = m_duration;
				ApplyTracks(owningFrame);

				m_playing = false;

				Finished(*this);
				FireLuaCallback(m_onFinish);
			}
		}
		else
		{
			ApplyTracks(owningFrame);
		}
	}

	void FrameAnimation::ApplyTracks(Frame& owningFrame)
	{
		for (const auto& track : m_tracks)
		{
			track.Apply(m_currentTime, owningFrame);
		}
	}

	void FrameAnimation::FireLuaCallback(luabind::object fn)
	{
		if (!fn.is_valid())
		{
			return;
		}

		try
		{
			fn(this);
		}
		catch (const luabind::error& e)
		{
			ELOG("Lua error in animation '" << m_name << "' callback: " << e.what());
		}
	}
}

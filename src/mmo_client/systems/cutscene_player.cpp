// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cutscene_player.h"

#include "client_locale.h"
#include "net/realm_connector.h"

#include "base/localization.h"
#include "client_data/project.h"
#include "game_client/sound_entry_player.h"
#include "log/default_log_levels.h"
#include "math/degree.h"

#include <algorithm>
#include <numeric>

namespace mmo
{
	namespace
	{
		/// The longest step the timeline takes in one frame. A frame that hung (loading) must not jump
		/// the camera across half the path.
		constexpr float MaxFrameSeconds = 0.1f;

		/// How long the hint how to skip stays up after a key press.
		constexpr float SkipHintSeconds = 4.0f;

		/// How long the picture takes to fade to black after the player skipped.
		constexpr float SkipFadeSeconds = 0.4f;

		float SmoothStep(const float t)
		{
			const float clamped = std::clamp(t, 0.0f, 1.0f);
			return clamped * clamped * (3.0f - 2.0f * clamped);
		}

		Quaternion MakeOrientation(const float yawDegrees, const float pitchDegrees)
		{
			// Yaw about the world up axis, then pitch about the rotated right axis: the order the world
			// editor's viewpoints use, which is where keyframes come from.
			return Quaternion(Degree(yawDegrees), Vector3::UnitY) *
				Quaternion(Degree(std::clamp(pitchDegrees, -89.0f, 89.0f)), Vector3::UnitX);
		}

		void ToView(const CutsceneSample& sample, CutscenePlayer::View& out_view)
		{
			out_view.position = sample.position;
			out_view.orientation = MakeOrientation(sample.yaw, sample.pitch);
			out_view.fov = sample.fov;
			out_view.fade = sample.fade;
			out_view.shot = sample.shot;
		}
	}

	CutscenePlayer::CutscenePlayer(RealmConnector& connector, const proto_client::Project& project, SoundEntryPlayer& sounds, IAudio& audio)
		: m_connector(connector)
		, m_project(project)
		, m_sounds(sounds)
		, m_audio(audio)
	{
	}

	CutscenePlayer::~CutscenePlayer()
	{
		StopSounds();
	}

	void CutscenePlayer::Request(const uint32 cutsceneId)
	{
		// The server only runs one at a time, so whatever still plays is over for it
		if (m_current)
		{
			Finish(cutscene_end_reason::Cancelled, false);
		}

		m_pending = nullptr;

		const proto_client::CutsceneEntry* cutscene = m_project.cutscenes.getById(cutsceneId);
		if (!cutscene || !IsCutscenePlayable(*cutscene))
		{
			// Client data older than the server's: release the character instead of leaving it
			// standing in a cutscene nobody sees.
			WLOG("Server announced cutscene " << cutsceneId << " which this client cannot play");
			m_connector.SendCutsceneBegin(cutsceneId);
			m_connector.SendCutsceneEnd(cutsceneId, true);
			return;
		}

		m_pending = cutscene;
	}

	void CutscenePlayer::OnServerEnded(const uint32 cutsceneId, const cutscene_end_reason::Type reason)
	{
		if (m_pending && m_pending->id() == cutsceneId)
		{
			m_pending = nullptr;
		}

		if (m_current && m_current->id() == cutsceneId)
		{
			Finish(reason, false);
		}
	}

	void CutscenePlayer::Begin()
	{
		if (!m_pending)
		{
			return;
		}

		m_current = m_pending;
		m_pending = nullptr;

		m_time = 0.0f;
		m_duration = static_cast<float>(GetCutsceneDuration(*m_current));
		m_skipHintTime = 0.0f;
		m_skipFadeTime = -1.0f;
		m_subtitle.clear();

		// Sounds in the order they start
		m_soundOrder.resize(m_current->sounds_size());
		std::iota(m_soundOrder.begin(), m_soundOrder.end(), 0);
		std::stable_sort(m_soundOrder.begin(), m_soundOrder.end(), [this](const int a, const int b)
		{
			return m_current->sounds(a).time() < m_current->sounds(b).time();
		});
		m_nextSound = 0;
		m_channels.clear();

		m_connector.SendCutsceneBegin(m_current->id());
		started(*m_current);

		// Whatever sits at the very start of the timeline is due now
		Update(0.0f);
	}

	void CutscenePlayer::Update(const float deltaSeconds)
	{
		if (!m_current)
		{
			return;
		}

		const float step = std::clamp(deltaSeconds, 0.0f, MaxFrameSeconds);

		if (m_skipHintTime > 0.0f)
		{
			m_skipHintTime = std::max(0.0f, m_skipHintTime - step);
		}

		// Skipped: the timeline stands still while the picture goes black, then it is over
		if (m_skipFadeTime >= 0.0f)
		{
			m_skipFadeTime -= step;
			if (m_skipFadeTime <= 0.0f)
			{
				Finish(cutscene_end_reason::Skipped, true);
			}

			return;
		}

		m_time += step * 1000.0f;

		while (m_nextSound < m_soundOrder.size())
		{
			const auto& sound = m_current->sounds(m_soundOrder[m_nextSound]);
			if (static_cast<float>(sound.time()) > m_time)
			{
				break;
			}

			const ChannelIndex channel = m_sounds.PlayEntry(sound.sound(), 2.0f);
			if (channel != InvalidChannel)
			{
				m_channels.push_back(channel);
			}

			++m_nextSound;
		}

		UpdateSubtitle();

		if (m_time >= m_duration)
		{
			Finish(cutscene_end_reason::Finished, true);
		}
	}

	bool CutscenePlayer::GetView(View& out_view) const
	{
		if (!m_current)
		{
			return false;
		}

		CutsceneSample sample;
		if (!SampleCutscene(*m_current, m_time, sample))
		{
			return false;
		}

		ToView(sample, out_view);

		// Glide in from the player's camera at the start and back to it at the end
		float weight = 1.0f;
		if (m_current->blend_in() > 0)
		{
			weight = std::min(weight, SmoothStep(m_time / static_cast<float>(m_current->blend_in())));
		}
		if (m_current->blend_out() > 0)
		{
			weight = std::min(weight, SmoothStep((m_duration - m_time) / static_cast<float>(m_current->blend_out())));
		}
		out_view.weight = weight;

		if (m_skipFadeTime >= 0.0f)
		{
			out_view.fade = std::max(out_view.fade, 1.0f - std::clamp(m_skipFadeTime / SkipFadeSeconds, 0.0f, 1.0f));
		}

		return true;
	}

	bool CutscenePlayer::GetStartView(const proto_client::CutsceneEntry& cutscene, View& out_view)
	{
		CutsceneSample sample;
		if (!SampleCutscene(cutscene, 0.0f, sample))
		{
			return false;
		}

		ToView(sample, out_view);
		out_view.weight = cutscene.blend_in() > 0 ? 0.0f : 1.0f;
		return true;
	}

	bool CutscenePlayer::IsSkippable() const
	{
		return m_current != nullptr && (m_current->flags() & cutscene_flags::Skippable) != 0;
	}

	void CutscenePlayer::RequestSkip()
	{
		if (!m_current || m_skipFadeTime >= 0.0f)
		{
			return;
		}

		// The first press asks, the second one answers
		if (!IsSkippable() || m_skipHintTime <= 0.0f)
		{
			ShowSkipHint();
			return;
		}

		m_skipFadeTime = SkipFadeSeconds;
		m_skipHintTime = 0.0f;
		m_subtitle.clear();
		StopSounds();
	}

	void CutscenePlayer::ShowSkipHint()
	{
		if (m_current && IsSkippable() && m_skipFadeTime < 0.0f)
		{
			m_skipHintTime = SkipHintSeconds;
		}
	}

	void CutscenePlayer::Reset()
	{
		StopSounds();
		m_pending = nullptr;
		m_current = nullptr;
		m_subtitle.clear();
		m_skipHintTime = 0.0f;
		m_skipFadeTime = -1.0f;
	}

	void CutscenePlayer::Finish(const cutscene_end_reason::Type reason, const bool notifyServer)
	{
		if (!m_current)
		{
			return;
		}

		const proto_client::CutsceneEntry& cutscene = *m_current;
		m_current = nullptr;
		m_subtitle.clear();
		m_skipHintTime = 0.0f;
		m_skipFadeTime = -1.0f;

		// A narrator that has not finished speaking when the timeline ends is left to finish; a
		// cutscene that was cut short takes its sounds with it.
		if (reason != cutscene_end_reason::Finished)
		{
			StopSounds();
		}
		m_channels.clear();

		if (notifyServer)
		{
			m_connector.SendCutsceneEnd(cutscene.id(), reason == cutscene_end_reason::Skipped);
		}

		ended(cutscene, reason);
	}

	void CutscenePlayer::StopSounds()
	{
		for (ChannelIndex& channel : m_channels)
		{
			if (channel != InvalidChannel)
			{
				m_audio.StopSound(&channel);
			}
		}

		m_channels.clear();
	}

	void CutscenePlayer::UpdateSubtitle()
	{
		const proto_client::CutsceneText* active = nullptr;
		for (int i = 0; i < m_current->texts_size(); ++i)
		{
			const auto& text = m_current->texts(i);
			const float start = static_cast<float>(text.time());
			if (m_time >= start && m_time < start + static_cast<float>(text.duration()))
			{
				// Of overlapping lines the one that started last is shown
				if (!active || text.time() >= active->time())
				{
					active = &text;
				}
			}
		}

		if (!active)
		{
			m_subtitle.clear();
			return;
		}

		m_subtitle = GetLocalizedString(active->text(), active->text_loc(), GetClientLocale());
	}
}

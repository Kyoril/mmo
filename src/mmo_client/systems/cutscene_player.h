// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "audio/audio.h"
#include "game/cutscene.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	class RealmConnector;
	class SoundEntryPlayer;

	namespace proto_client
	{
		class Project;
		class CutsceneEntry;
	}

	/// Plays the cutscenes the server announces: it runs the timeline of a cutscene, says where the
	/// camera is at every moment, starts the sounds on the timeline and knows the subtitle that is up.
	/// It does not touch the camera, the ui or the player's input itself: the world state reads the
	/// view from it every frame and reacts to its started and ended signals.
	///
	/// A cutscene goes through three steps. The server announces it (Request) and it is pending until
	/// the world around the player is loaded. Then it begins (Begin), which tells the server to start
	/// following the camera, and plays until its timeline is over, the player skips it or the server
	/// ends it.
	class CutscenePlayer final : public NonCopyable
	{
	public:
		/// Where the camera of the playing cutscene is.
		struct View final
		{
			/// Eye position in world space.
			Vector3 position;

			/// Orientation in world space.
			Quaternion orientation;

			/// How much of the view is the cutscene's camera: 1 = all of it, 0 = the player's own
			/// camera. Below 1 while the cutscene blends in from or out to the player's camera.
			float weight = 1.0f;

			/// Vertical field of view in degrees, or 0 to keep the player camera's.
			float fov = 0.0f;

			/// How far the picture is faded to black: 0 = clear, 1 = black.
			float fade = 0.0f;

			/// Index of the shot that plays. Changes at a cut.
			int32 shot = 0;
		};

	public:
		/// Fired when a cutscene begins to play.
		signal<void(const proto_client::CutsceneEntry&)> started;

		/// Fired when the cutscene that played is over, for whatever reason.
		signal<void(const proto_client::CutsceneEntry&, cutscene_end_reason::Type)> ended;

	public:
		explicit CutscenePlayer(RealmConnector& connector, const proto_client::Project& project, SoundEntryPlayer& sounds, IAudio& audio);
		~CutscenePlayer();

	public:
		/// The server announced a cutscene. It becomes pending; a cutscene that plays is dropped first.
		/// An id the client has no playable cutscene for is answered right away, so that the server
		/// does not keep the character in a cutscene nobody sees.
		void Request(uint32 cutsceneId);

		/// The server says the cutscene is over. Ends it here too if it is still pending or playing.
		void OnServerEnded(uint32 cutsceneId, cutscene_end_reason::Type reason);

		/// Whether a cutscene waits for the world to be loaded.
		[[nodiscard]] bool HasPending() const { return m_pending != nullptr; }

		/// The cutscene that waits to begin, or nullptr.
		[[nodiscard]] const proto_client::CutsceneEntry* GetPending() const { return m_pending; }

		/// Whether a cutscene plays.
		[[nodiscard]] bool IsPlaying() const { return m_current != nullptr; }

		/// The cutscene that plays, or nullptr.
		[[nodiscard]] const proto_client::CutsceneEntry* GetCurrent() const { return m_current; }

		/// Starts playing the pending cutscene and tells the server so.
		void Begin();

		/// Advances the playing cutscene. May end it.
		/// @param deltaSeconds Time since the last frame.
		void Update(float deltaSeconds);

		/// The camera of the playing cutscene at this moment.
		/// @return false if no cutscene plays.
		[[nodiscard]] bool GetView(View& out_view) const;

		/// The camera at the very start of a cutscene, for loading the world around it up front.
		/// @return false if the cutscene has nothing to play.
		[[nodiscard]] static bool GetStartView(const proto_client::CutsceneEntry& cutscene, View& out_view);

		/// Whether the playing cutscene may be ended early by the player.
		[[nodiscard]] bool IsSkippable() const;

		/// The player pressed the key that skips. The first press only shows the hint (see
		/// IsSkipHintVisible); a second one while the hint is up skips, if the cutscene allows it.
		void RequestSkip();

		/// The player pressed some other key or a mouse button: shows the hint how to skip.
		void ShowSkipHint();

		/// Whether the hint how to skip is up.
		[[nodiscard]] bool IsSkipHintVisible() const { return m_current != nullptr && m_skipHintTime > 0.0f; }

		/// The subtitle that is up at this moment, in the client's language. Empty if none.
		[[nodiscard]] const String& GetSubtitle() const { return m_subtitle; }

		/// Drops everything without telling anyone: the world is left.
		void Reset();

	private:
		/// Ends the playing cutscene.
		/// @param reason Why.
		/// @param notifyServer false if the server already knows.
		void Finish(cutscene_end_reason::Type reason, bool notifyServer);

		void StopSounds();

		void UpdateSubtitle();

	private:
		RealmConnector& m_connector;
		const proto_client::Project& m_project;
		SoundEntryPlayer& m_sounds;
		IAudio& m_audio;

		const proto_client::CutsceneEntry* m_pending = nullptr;
		const proto_client::CutsceneEntry* m_current = nullptr;

		/// Position on the timeline in milliseconds.
		float m_time = 0.0f;
		float m_duration = 0.0f;

		/// Index of the next entry of the cutscene's sounds (sorted by time) that has not started yet.
		std::vector<int> m_soundOrder;
		size_t m_nextSound = 0;
		std::vector<ChannelIndex> m_channels;

		/// Seconds the skip hint stays up.
		float m_skipHintTime = 0.0f;

		/// Seconds left of the fade to black that follows a skip, or negative while not skipping.
		float m_skipFadeTime = -1.0f;

		String m_subtitle;
	};
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/movement_info.h"
#include "game/traversal.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	namespace proto
	{
		class AreaTriggers;
	}

	/// Server-side rules for gliding: when a glide may start, which packets may change the Gliding
	/// movement flag, how far a glider may move, and how long a client may keep gliding after the
	/// server ended its glide. Pure functions, so the world server's movement handler only wires them.
	namespace glide_rules
	{
		/// How long a client may keep sending the Gliding flag after the server ended its glide
		/// (ForceStopGlide sent, Vigor ran out) before that counts as a violation.
		constexpr GameTime StopGraceMs = 1000;

		/// Horizontal speed tolerance on top of TraversalSettings::glideForwardSpeed (latency jitter,
		/// frame-rate variation), the same factor the regular speed check uses.
		constexpr float SpeedTolerance = 1.35f;

		/// Height a glider may gain without an updraft or carried upward speed (physics rounding). It is
		/// a pool: rising uses it up and only sinking refills it, so small rises never add up to a climb.
		constexpr float RiseToleranceMeters = 0.5f;

		/// How far a glide's client timestamps may run ahead of the server's receive clock since the
		/// glide started. Later timestamps are clamped, so forged times cannot buy distance or lift.
		constexpr GameTime TimestampLeadMs = 1000;

		/// The glide speed check only measures intervals at least this long. Shorter intervals are
		/// accumulated into the next one, so packets sent in quick succession (facing changes while
		/// steering) never produce near-zero elapsed times with inflated implied speeds.
		constexpr GameTime MinCheckIntervalMs = 250;

		/// Gravity of a falling character in units per second squared: 9.8 times the client
		/// movement's gravity scale of 2 (see RemoteMovementRenderer::kGravity).
		constexpr float FallGravity = 19.6f;

		/// Highest speed a falling character reaches, in units per second.
		constexpr float TerminalFallSpeed = 55.0f;
	}

	/// Result of CanStartGlide.
	enum class GlideStartDecision : uint8
	{
		/// The glide may start.
		Allowed,

		/// The character is not in the air (the glider only opens while falling).
		NotFalling,

		/// The character swims.
		Swimming,

		/// The character does not know the Glider spell (or has no Vigor pool).
		NoGlider,

		/// The character has no Vigor left.
		NoVigor,

		/// The character is stunned, rooted or dead.
		Incapacitated
	};

	/// How a movement packet changes the Gliding flag.
	enum class GlideFlagChange : uint8
	{
		/// The flag does not change (or a redundant MoveStopGlide arrived while not gliding, which
		/// happens when the client answers a ForceStopGlide after it already stopped).
		Unchanged,

		/// MoveStartGlide adds the flag.
		Start,

		/// MoveStopGlide removes the flag.
		Stop,

		/// Landing (MoveFallLand), entering water (MoveStartSwim) or the end of a server-controlled
		/// movement (MoveEnded) removes the flag: the glide ends without a MoveStopGlide.
		ImplicitStop,

		/// The packet changes the flag in a way it may not (a packet other than MoveStartGlide adds
		/// it, a packet that may not end a glide removes it, a glide packet that does not change it,
		/// or the flag without Falling).
		Invalid
	};

	/// Decides whether a glide may start.
	/// @param current The character's movement info before the MoveStartGlide packet.
	/// @param knowsGlider Whether the character knows the Glider spell and has a Vigor pool.
	/// @param vigor The character's current Vigor.
	/// @param incapacitated Whether the character is stunned, rooted or dead.
	/// @return Allowed, or the first reason that forbids the glide.
	[[nodiscard]] GlideStartDecision CanStartGlide(const MovementInfo& current, bool knowsGlider, float vigor, bool incapacitated);

	/// Whether a refused glide start is an anti-cheat violation. Refusals caused by a temporary state
	/// the client may not know of yet (Vigor mispredicted, a stun or a landing in flight) are not:
	/// the server only closes the glider. Refusals no honest client sends (no Glider, swimming)
	/// are. A MoveStartGlide while already gliding never gets here: ClassifyGlideFlagChange rejects it.
	/// @param decision The refusal from CanStartGlide.
	[[nodiscard]] bool IsGlideStartRefusalViolation(GlideStartDecision decision);

	/// @param fallHeight Height fallen from rest.
	/// @return The downward speed after falling that height (glide_rules::FallGravity), capped at
	///	        glide_rules::TerminalFallSpeed.
	[[nodiscard]] float GetFreeFallSpeed(float fallHeight);

	/// @param downwardSpeed Downward speed in units per second (upward speeds count as 0).
	/// @return The height a free fall from rest needs to reach that speed: v^2 / (2 g).
	[[nodiscard]] float GetEquivalentFallHeight(float downwardSpeed);

	/// The server's model of a glider's vertical speed, used for fall damage: a glider opened in a
	/// fast fall needs time to brake (the vertical speed approaches -glideSinkSpeed, or the updraft
	/// strength, at glideVerticalAccel like StepGlideVelocity), so opening it just before the ground
	/// does not avoid the fall damage.
	class GlideFallModel final
	{
	public:
		/// Starts a glide.
		/// @param fallStartY Height at which the fall that the glide interrupts started.
		/// @param glideStartY Height at which the glider opened.
		/// @param time When the glider opened.
		void Begin(float fallStartY, float glideStartY, GameTime time);

		/// Advances the vertical speed to the given time.
		/// @param time The new time. Times before the last one are ignored.
		/// @param updraftStrength The updraft at the glider's position, 0 if none.
		/// @param settings The traversal tuning values.
		void Advance(GameTime time, float updraftStrength, const TraversalSettings& settings);

		/// @return The vertical speed (Y up, negative while sinking).
		[[nodiscard]] float GetVerticalSpeed() const { return m_verticalSpeed; }

		/// @param y The glider's current height.
		/// @return The height a fall from rest would have started at to reach the current downward
		///	        speed at y. Fall damage of a landing (or of the fall after the glider closes) is
		///	        measured from there.
		[[nodiscard]] float GetVirtualFallStartY(float y) const;

	private:
		float m_verticalSpeed = 0.0f;
		GameTime m_time = 0;
	};

	/// Classifies how a client movement packet changes the Gliding flag.
	/// @param clientOpCode The game::client_realm_packet op code of the packet.
	/// @param previous The movement info before the packet.
	/// @param next The movement info of the packet.
	[[nodiscard]] GlideFlagChange ClassifyGlideFlagChange(uint16 clientOpCode, const MovementInfo& previous, const MovementInfo& next);

	/// Height a glider may still gain from upward speed it carries out of an updraft or into a glide
	/// (a glide opened during a jump): the glide physics brake the vertical speed with
	/// TraversalSettings::glideVerticalAccel, so the rise is v^2 / (2 a).
	/// @param upwardSpeed The upward speed in units per second (negative values count as 0).
	/// @param settings The traversal tuning values.
	/// @return The height in units.
	[[nodiscard]] float GetGlideRiseCarry(float upwardSpeed, const TraversalSettings& settings);

	/// Checks one interval of a glide.
	/// Horizontally the glider may move at most glideForwardSpeed x SpeedTolerance. Vertically it may
	/// gain at most RiseToleranceMeters plus the carried rise, and inside an updraft additionally
	/// updraftStrength x SpeedTolerance per second.
	/// @param from Position at the start of the interval.
	/// @param to Position at the end of the interval.
	/// @param dt Length of the interval in milliseconds. 0 skips the horizontal check (no time base).
	/// @param updraftStrength Strongest updraft at either end of the interval, 0 if none.
	/// @param settings The traversal tuning values.
	/// @param carriedRise Height the glider may gain from upward speed it carries (GetGlideRiseCarry).
	/// @return Whether the move is possible for an honest glider.
	[[nodiscard]] bool IsGlideSpeedValid(const Vector3& from, const Vector3& to, GameTime dt, float updraftStrength, const TraversalSettings& settings, float riseAllowance = glide_rules::RiseToleranceMeters);

	/// Clamps a glide packet's client time so it runs at most glide_rules::TimestampLeadMs ahead of the
	/// server's receive clock since the glide started. Counted over the whole glide, so packets that
	/// arrive bunched after a lag spike keep their (legitimate) client times.
	/// @param clientTime The packet's client time (converted to server time).
	/// @param beginClientTime The client time of the glide start.
	/// @param receiveTime When the server received the packet.
	/// @param beginReceiveTime When the server received the glide start.
	[[nodiscard]] GameTime ClampGlideClientTime(GameTime clientTime, GameTime beginClientTime, GameTime receiveTime, GameTime beginReceiveTime);

	/// @param now The current time.
	/// @param stopTime When the server ended the glide (ForceStopGlide sent, Vigor ran out).
	/// @return Whether a gliding packet arriving now is still within glide_rules::StopGraceMs.
	[[nodiscard]] bool IsWithinEmptyGrace(GameTime now, GameTime stopTime);

	/// Fall damage as a fraction of the maximum health: 0 up to minHeight of fall distance, rising
	/// linearly to 1 at lethalHeight and beyond.
	/// @param startY Height at which the fall started (jump, end of a glide).
	/// @param landY Height at which the character landed.
	/// @param minHeight Fall distance at which damage starts.
	/// @param lethalHeight Fall distance at which the fall is lethal.
	/// @return The damage fraction from 0 to 1.
	[[nodiscard]] float ComputeFallDamagePercent(float startY, float landY, float minHeight, float lethalHeight);

	/// Builds the updraft volumes of a map from its area triggers (those with updraft_strength > 0).
	/// Interprets the trigger shape like the world server's area-trigger containment: a radius makes a
	/// sphere, box_x/y/z are the full box size (halved into half extents), box_o its facing.
	/// @param triggers All area triggers of the project.
	/// @param mapId The map whose updrafts to collect.
	[[nodiscard]] std::vector<UpdraftVolume> BuildUpdraftVolumes(const proto::AreaTriggers& triggers, uint32 mapId);

	/// Follows one glide of one character and checks every interval of it with IsGlideSpeedValid,
	/// keeping the height the glider may still gain from carried upward speed.
	class GlideMoveValidator final
	{
	public:
		/// Result of Check.
		enum class Result : uint8
		{
			/// Not gliding, or the interval is shorter than glide_rules::MinCheckIntervalMs.
			Skipped,

			/// The interval is fine.
			Ok,

			/// The glider moved too fast or rose without lift.
			Violation
		};

	public:
		/// Starts following a glide.
		/// @param position Where the glide starts.
		/// @param time When the glide starts.
		/// @param receiveTime When the server received the glide start.
		/// @param upwardSpeed The upward speed at the start (a glide opened during a jump). Capped at
		///	       TraversalSettings::defaultUpdraftStrength, so a client cannot claim any lift it likes.
		/// @param settings The traversal tuning values (copied).
		void Begin(const Vector3& position, GameTime time, GameTime receiveTime, float upwardSpeed, const TraversalSettings& settings);

		/// Stops following the glide.
		void End() { m_active = false; }

		/// @return Whether a glide is followed.
		[[nodiscard]] bool IsActive() const { return m_active; }

		/// Checks the interval from the last checked position to the given one. Intervals shorter than
		/// glide_rules::MinCheckIntervalMs (and positions with an older time) are skipped and measured
		/// as part of the next interval.
		/// @param position The glider's new position.
		/// @param time The time of the new position (clamped with ClampGlideClientTime).
		/// @param receiveTime When the server received the position.
		/// @param updrafts The updraft volumes of the map.
		[[nodiscard]] Result Check(const Vector3& position, GameTime time, GameTime receiveTime, const std::vector<UpdraftVolume>& updrafts);

	private:
		TraversalSettings m_settings;
		Vector3 m_lastPosition;
		GameTime m_lastTime = 0;
		GameTime m_beginTime = 0;
		GameTime m_beginReceiveTime = 0;
		float m_carriedRise = 0.0f;
		float m_risePool = glide_rules::RiseToleranceMeters;
		bool m_active = false;
	};
}

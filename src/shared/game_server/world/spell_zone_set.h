// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <functional>
#include <vector>

namespace mmo
{
	/// A circular area on the ground created by a PersistentAreaAura spell effect. While it exists
	/// it periodically casts its trigger spell on every enemy of its caster standing inside.
	struct SpellZone
	{
		/// Unique within the owning world instance, never 0. Assigned by SpellZoneSet::Add.
		uint32 id = 0;

		/// Unit that cast the zone. Ticks are cast by this unit.
		uint64 casterGuid = 0;

		/// Spell whose PersistentAreaAura effect created the zone (drives the client visual).
		uint32 spellId = 0;

		/// Spell cast on every enemy inside the zone on each tick. 0 for a purely visual zone.
		uint32 triggerSpellId = 0;

		/// Centre of the zone on the ground.
		Vector3 position;

		/// Radius of the zone. Height is ignored when testing whether a unit stands inside.
		float radius = 0.0f;

		/// Time at which the zone ends.
		GameTime expiresAt = 0;

		/// Time of the next tick. Ticks fall at creation + n * tickInterval, the last one at or
		/// before expiresAt, so a zone whose interval equals its duration ticks exactly once,
		/// as it expires: a delayed detonation.
		GameTime nextTickAt = 0;

		/// Time between ticks. 0 means the zone never ticks.
		GameTime tickInterval = 0;
	};

	/// The live spell zones of one world instance. Pure bookkeeping: what a tick or an expiry
	/// does in the world is up to the callbacks passed to Advance.
	class SpellZoneSet final
	{
	public:
		/// Adds a zone and assigns its id.
		/// @param zone The zone; its id is overwritten.
		/// @return The id assigned to the zone.
		uint32 Add(SpellZone zone);

		/// Advances every zone to the given time. Due ticks fire in time order per zone; a zone
		/// that has expired fires its remaining ticks, then onExpired, and is removed.
		/// @param now Current time.
		/// @param onTick Called for every due tick.
		/// @param onExpired Called once for every zone that ran its full duration.
		void Advance(GameTime now, const std::function<void(const SpellZone&)>& onTick, const std::function<void(const SpellZone&)>& onExpired);

		/// Removes every zone matching a predicate without ticking it, e.g. those of a dead caster.
		/// @param predicate Returns true for zones to remove.
		/// @param onRemoved Called for every removed zone.
		void RemoveIf(const std::function<bool(const SpellZone&)>& predicate, const std::function<void(const SpellZone&)>& onRemoved);

		/// Number of live zones.
		[[nodiscard]] size_t Size() const { return m_zones.size(); }

		/// Read access to the live zones.
		[[nodiscard]] const std::vector<SpellZone>& GetZones() const { return m_zones; }

	private:
		std::vector<SpellZone> m_zones;
		uint32 m_nextId = 1;
	};

	/// Whether a position lies inside a zone's circle; height is ignored.
	/// @param zone The zone.
	/// @param position Position to test.
	/// @return True if the planar distance to the zone centre is at most its radius.
	bool IsInsideSpellZone(const SpellZone& zone, const Vector3& position);
}

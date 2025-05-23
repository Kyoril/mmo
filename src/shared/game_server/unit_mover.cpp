// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_mover.h"
#include "game/circle.h"
#include "game_server/objects/game_unit_s.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/universe.h"
#include "binary_io/vector_sink.h"
#include "game_protocol/game_protocol.h"
#include "game_server/world/each_tile_in_sight.h"
#include "game_server/world/tile_subscriber.h"
#include "log/default_log_levels.h"

namespace mmo
{
	const GameTime UnitMover::UpdateFrequency = constants::OneSecond / 2;

	UnitMover::UnitMover(GameUnitS& unit)
		: m_unit(unit)
		, m_moveReached(unit.GetTimers())
		, m_moveUpdated(unit.GetTimers())
		, m_moveStart(0)
		, m_moveEnd(0)
		, m_customSpeed(false)
		, m_debugOutputEnabled(false)
	{
		m_moveUpdated.ended.connect([this]()
			{
				const GameTime time = GetAsyncTimeMs();
				if (time >= m_moveEnd) {
					return;
				}

				// Calculate new position
				const Radian o = GetMoved().GetAngle(m_target.x, m_target.z);
				const Vector3 oldPosition = GetCurrentLocation();
				GetMoved().Relocate(oldPosition, o);

				// Trigger next update if needed
				if (time < m_moveEnd - UnitMover::UpdateFrequency)
				{
					m_moveUpdated.SetEnd(time + UnitMover::UpdateFrequency);
				}
			});

		m_moveReached.ended.connect([this]()
			{
				// Clear path
				m_path.Clear();

				// Cancel update timer
				m_moveUpdated.Cancel();

				// Check if we are still in the world
				if (const auto* world = GetMoved().GetWorldInstance(); !world)
				{
					return;
				}

				// Fire signal since we reached our target
				targetReached();

				const Radian o = m_customFacing.value_or(GetMoved().GetAngle(m_target.x, m_target.z));
				auto& target = m_target;

				// Update creatures position
				const auto strongUnit = GetMoved().shared_from_this();
				std::weak_ptr weakUnit(strongUnit);
				GetMoved().GetWorldInstance()->GetUniverse().Post([weakUnit, target, o]()
					{
						if (const auto strongUnit = weakUnit.lock())
						{
							strongUnit->Relocate(target, o);

							auto info = strongUnit->GetMovementInfo();
							info.movementFlags = movement_flags::None;
							strongUnit->ApplyMovementInfo(info);
						}
					});
			});
	}

	UnitMover::~UnitMover()
	= default;

	void UnitMover::OnMoveSpeedChanged(MovementType moveType)
	{
		if (!m_customSpeed &&
			moveType == movement_type::Run &&
			m_moveReached.IsRunning())
		{
			// Restart move command
			MoveTo(m_target, 0.0f);
		}
	}

	bool UnitMover::MoveTo(const Vector3& target, float acceptanceRadius, const Radian* targetFacing, const IShape* clipping/* = nullptr*/)
	{
		const bool result = MoveTo(target, m_unit.GetSpeed(movement_type::Run), acceptanceRadius, targetFacing, clipping);
		m_customSpeed = false;
		return result;
	}

	static void WriteCreatureMove(
		game::OutgoingPacket& out_packet,
		uint64 guid,
		const Vector3& oldPosition,
		const std::vector<Vector3>& path,
		const Radian* targetFacing,
		GameTime startTime,
		GameTime endTime
	)
	{
		ASSERT(!path.empty());
		ASSERT(path.size() >= 1);

		out_packet.Start(game::realm_client_packet::CreatureMove);
		out_packet
			<< io::write_packed_guid(guid)
			<< io::write<float>(oldPosition.x)
			<< io::write<float>(oldPosition.y)
			<< io::write<float>(oldPosition.z)
			<< io::write<GameTime>(startTime)	// Start time
			<< io::write<GameTime>(endTime)	// End time
			<< io::write<uint32>(path.size() - 1);

		// Write destination point (this counts as the first point)
		const auto& pt = path.back();
		out_packet
			<< io::write<float>(pt.x)
			<< io::write<float>(pt.y)
			<< io::write<float>(pt.z);

		if (targetFacing)
		{
			out_packet << io::write<uint8>(1) << io::write<float>(targetFacing->GetValueRadians());
		}
		else
		{
			out_packet << io::write<uint8>(0);
		}

		// Write points in between (if any)
		if (path.size() > 1)
		{
			// all other points are relative to the center of the path
			const Vector3 mid = (oldPosition + pt) * 0.5f;
			for (uint32 i = 1; i < path.size() - 1; ++i)
			{
				auto& p = path[i];
				uint32 packed = 0;
				packed |= ((int)((mid.x - p.x) / 0.25f) & 0x7FF);
				packed |= ((int)((mid.y - p.y) / 0.25f) & 0x7FF) << 11;
				packed |= ((int)((mid.z - p.z) / 0.25f) & 0x3FF) << 22;
				out_packet
					<< io::write<uint32>(packed);
			}
		}

		out_packet.Finish();
	}

	bool UnitMover::MoveTo(const Vector3& target, float customSpeed, float acceptanceRadius, const Radian* targetFacing, const IShape* clipping/* = nullptr*/)
	{
		auto& moved = GetMoved();

		if (!moved.IsAlive() || moved.IsRooted())
		{
			return false;
		}

		// Get current location
		m_customSpeed = true;
		auto currentLoc = GetCurrentLocation();

		if (m_debugOutputEnabled)
		{
			DLOG("New target: " << target << " (Current: " << currentLoc << "; Speed: " << customSpeed << ")");
		}

		m_start = currentLoc;

		// Now we need to stop the current movement
		if (m_moveReached.IsRunning())
		{
			// Cancel movement timers
			m_moveReached.Cancel();
			m_moveUpdated.Cancel();

			// Calculate our orientation
			const Radian o = moved.GetAngle(target.x, target.z);
			moved.Relocate(currentLoc, o);
		}

		auto* world = moved.GetWorldInstance();
		if (!world)
		{
			WLOG("Unable to find world instance");
			return false;
		}

		auto* map = world->GetMapData();
		if (!map)
		{
			WLOG("Unable to find map data");
			return false;
		}

		// Calculate path
		std::vector<Vector3> path;
		if (!map->CalculatePath(currentLoc, target, path))
		{
			return false;
		}

		if (path.empty())
		{
			return false;
		}
		
		// Clear the current movement path
		m_path.Clear();

		// Update timing
		m_moveStart = GetAsyncTimeMs();
		if (m_debugOutputEnabled)
		{
			DLOG("Move start: " << m_moveStart << " ( with speed: " << customSpeed << ")");
		}

		if (acceptanceRadius > 0.0f && path.size() >= 2)
		{
			// Add acceptance radius to the last point
			Vector3& lastPoint = path.back();
			const Vector3& prevPoint = path[path.size() - 2];
			const Vector3 diff = lastPoint - prevPoint;
			const float dist = diff.GetLength();

			// Close enough? Then just drop the last point from path entirely
			if (dist <= acceptanceRadius)
			{
				path.pop_back();
			}
			else
			{
				lastPoint -= diff * (acceptanceRadius / dist);
			}
		}

		GameTime moveTime = m_moveStart;
		for (uint32 i = 0; i < path.size(); ++i)
		{
			const float dist =
				(i == 0) ? ((path[i] - currentLoc).GetLength()) : (path[i] - path[i - 1]).GetLength();
			moveTime += (dist / customSpeed) * constants::OneSecond;
			m_path.AddPosition(moveTime, path[i]);
		}

		// Use new values
		m_start = currentLoc;
		m_target = path.back();

		// Calculate time of arrival
		m_moveEnd = moveTime;

		auto movementInfo = moved.GetMovementInfo();
		movementInfo.movementFlags = movement_flags::None;
		moved.ApplyMovementInfo(movementInfo);

		// Send movement packet
		TileIndex2D tile;
		if (moved.GetTileIndex(tile))
		{
			std::vector<char> buffer;
			io::VectorSink sink(buffer);
			game::Protocol::OutgoingPacket packet(sink);
			WriteCreatureMove(packet, moved.GetGuid(), currentLoc, path, targetFacing, m_moveStart, m_moveEnd);

			ForEachSubscriberInSight(
				moved.GetWorldInstance()->GetGrid(),
				tile,
				[&packet, &buffer, &moved](TileSubscriber& subscriber)
				{
					subscriber.SendPacket(packet, buffer);
				});
		}

		if (targetFacing)
		{
			m_customFacing = *targetFacing;
		}
		else
		{
			m_customFacing.reset();
		}

		// Setup update timer if needed
		GameTime nextUpdate = m_moveStart + UnitMover::UpdateFrequency;
		if (nextUpdate < m_moveEnd)
		{
			m_moveUpdated.SetEnd(nextUpdate);
		}

		// Setup end timer
		m_moveReached.SetEnd(m_moveEnd);
		if (m_debugOutputEnabled)
		{
			DLOG("Move end: " << m_moveEnd << " (Time: " << m_moveEnd - m_moveStart << ")");
		}

		// Raise signal
		targetChanged();

		if (m_debugOutputEnabled)
		{
			m_path.PrintDebugInfo();
		}

		return true;
	}

	void UnitMover::StopMovement()
	{
		if (!IsMoving())
		{
			return;
		}

		// Update current location
		auto currentLoc = GetCurrentLocation();
		const float dx = m_target.x - currentLoc.x;
		const float dy = m_target.z - currentLoc.z;
		float o = ::atan2(dy, dx);
		o = (o >= 0) ? o : 2 * 3.1415927f + o;

		// Cancel timers before relocate, in order to prevent stack overflow (because isMoving()
		// simply checks if m_moveReached is running)
		m_moveReached.Cancel();
		m_moveUpdated.Cancel();

		// Update with grid notification
		auto& moved = GetMoved();
		moved.Relocate(currentLoc, Radian(o));

		// Send movement packet
		TileIndex2D tile;
		if (moved.GetTileIndex(tile))
		{
			GameTime now = GetAsyncTimeMs();

			// TODO: Maybe, player characters need another movement packet for this...
			std::vector<char> buffer;
			io::VectorSink sink(buffer);
			game::Protocol::OutgoingPacket packet(sink);
			WriteCreatureMove(packet, moved.GetGuid(), currentLoc, { currentLoc }, nullptr, now, now);

			ForEachSubscriberInSight(
				moved.GetWorldInstance()->GetGrid(),
				tile,
				[&packet, &buffer, &moved](TileSubscriber& subscriber)
				{
					subscriber.SendPacket(packet, buffer);
				});
		}

		m_customFacing.reset();

		// Fire this trigger only here, not when movement was updated,
		// since only then we are really stopping
		movementStopped();
	}

	Vector3 UnitMover::GetCurrentLocation() const
	{
		// Unit didn't move yet or isn't moving at all
		if (m_moveStart == 0 || !IsMoving() || !m_path.HasPositions()) {
			return GetMoved().GetMovementInfo().position;
		}

		// Determine the current waypoints
		return m_path.GetPosition(GetAsyncTimeMs());
	}

	void UnitMover::SendMovementPackets(TileSubscriber& subscriber)
	{
		if (!IsMoving())
			return;

		const GameTime now = GetAsyncTimeMs();
		if (now >= m_moveEnd)
			return;

		// Take a sample of the current location
		auto location = GetCurrentLocation();

		// Remove all points that are too early
		std::vector<Vector3> path;
		for (auto& p : m_path.GetPositions())
		{
			if (p.first < now)
			{
				continue;
			}

			path.push_back(p.second);
		}

		if (path.empty())
			return;

		Radian* customFacing = nullptr;
		if (m_customFacing)
		{
			customFacing = &m_customFacing.value();
		}

		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		game::Protocol::OutgoingPacket packet(sink);
		WriteCreatureMove(packet, GetMoved().GetGuid(), location, path, customFacing, now, m_moveEnd);
		subscriber.SendPacket(packet, buffer);
	}
}

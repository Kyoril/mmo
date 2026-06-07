#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	class GamePlayerC;
	class GameUnitC;
	class GameItemC;
	class GameWorldObjectC;
	struct MovementEvent;

	/// @brief An interface for handling client network events related to units.
	class NetClient : public NonCopyable
	{
	public:
		virtual ~NetClient() = default;

	public:
		/// @brief An attack start request happened.
		virtual void SendAttackStart(const uint64 victim, const GameTime timestamp) = 0;

		/// @brief An attack stop request happened.
		virtual void SendAttackStop(const GameTime timestamp) = 0;

		virtual void GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player) = 0;

		virtual void GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature) = 0;

		virtual void GetItemData(uint64 guid, std::weak_ptr<GameItemC> item) = 0;

		virtual void GetItemData(uint64 guid, std::weak_ptr<GamePlayerC> player) = 0;

		virtual void GetObjectData(uint64 guid, std::weak_ptr<GameWorldObjectC> object) = 0;

		virtual void OnMoveEvent(GameUnitC& unit, const MovementEvent& moveEvent) = 0;

		/// @brief Queries the water surface height at the given world XZ position.
		/// Used by the client-side movement code to detect when a unit enters/leaves water.
		/// @param x World X coordinate.
		/// @param z World Z coordinate.
		/// @param outSurfaceY Receives the water surface height (Y) when water is present.
		/// @return True if there is water at the given position, false otherwise.
		virtual bool QueryWaterAt(float x, float z, float& outSurfaceY) const = 0;

		virtual void SetSelectedTarget(uint64 guid) = 0;

		virtual void OnGuildChanged(std::weak_ptr<GamePlayerC> player, uint64 guildGuid) = 0;
	};

}
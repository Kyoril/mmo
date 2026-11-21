#pragma once

#include "game_client/net_client.h"
#include "game_client/movement_event.h"

#include <optional>
#include <vector>

namespace mmo
{
	/// @brief A no-op NetClient for use in headless movement tests.
	class TestNetClient final : public NetClient
	{
	public:
		void SendAttackStart(const uint64 /*victim*/, const GameTime /*timestamp*/) override {}
		void SendAttackStop(const GameTime /*timestamp*/) override {}
		void GetPlayerName(uint64 /*guid*/, std::weak_ptr<GamePlayerC> /*player*/) override {}
		void GetCreatureData(uint64 /*guid*/, std::weak_ptr<GameUnitC> /*creature*/) override {}
		void GetItemData(uint64 /*guid*/, std::weak_ptr<GameItemC> /*item*/) override {}
		void GetItemData(uint64 /*guid*/, std::weak_ptr<GamePlayerC> /*player*/) override {}
		void GetObjectData(uint64 /*guid*/, std::weak_ptr<GameWorldObjectC> /*object*/) override {}
		void OnMoveEvent(GameUnitC& /*unit*/, const MovementEvent& moveEvent) override { m_moveEvents.push_back(moveEvent.eventType); }
		bool QueryWaterAt(float /*x*/, float /*z*/, float& outSurfaceY) const override
		{
			if (!m_waterSurfaceY)
			{
				return false;
			}

			outSurfaceY = *m_waterSurfaceY;
			return true;
		}

		/// @brief Covers the whole test world with water whose surface is at the given height.
		void SetWaterSurface(const std::optional<float> surfaceY) { m_waterSurfaceY = surfaceY; }

		/// @brief Movement events raised by units using this client, in order.
		[[nodiscard]] const std::vector<MovementEventType>& GetMoveEvents() const { return m_moveEvents; }

		/// @brief Whether a movement event of the given type was raised.
		[[nodiscard]] bool HasMoveEvent(const MovementEventType type) const
		{
			for (const MovementEventType recorded : m_moveEvents)
			{
				if (recorded == type)
				{
					return true;
				}
			}

			return false;
		}
		void SetSelectedTarget(uint64 /*guid*/) override {}
		void OnGuildChanged(std::weak_ptr<GamePlayerC> /*player*/, uint64 /*guildGuid*/) override {}

	private:
		/// Water surface height of the whole test world, if it has water at all.
		std::optional<float> m_waterSurfaceY;

		/// Movement events raised so far.
		std::vector<MovementEventType> m_moveEvents;
	};
}

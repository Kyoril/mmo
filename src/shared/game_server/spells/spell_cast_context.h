// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_server/spells/spell_cast.h"
#include "game_server/world/tile_subscriber.h"
#include "game_server/world/each_tile_in_sight.h"
#include "binary_io/vector_sink.h"
#include "game_protocol/game_protocol.h"
#include "game_server/objects/game_player_s.h"

namespace mmo
{
	class GameObjectS;
	class WorldInstance;

	class SpellCastContext final
	{
	public:
		SpellCastContext(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target)
			: m_cast(cast)
			, m_spell(spell)
			, m_target(target)
		{
		}

		SpellCast& GetCast() const { return m_cast; }
		GameUnitS& GetExecutor() const { return m_cast.GetExecuter(); }
		const proto::SpellEntry& GetSpell() const { return m_spell; }
		const SpellTargetMap& GetTarget() const { return m_target; }

		WorldInstance* GetWorldInstance() const { return m_cast.GetExecuter().GetWorldInstance(); }

		GameObjectS* FindObjectByGuid(uint64 guid) const
		{
			auto* world = GetWorldInstance();
			if (!world)
			{
				return nullptr;
			}

			return world->FindObjectByGuid(guid);
		}

		GameUnitS* FindUnitByGuid(uint64 guid) const
		{
			auto* world = GetWorldInstance();
			if (!world)
			{
				return nullptr;
			}

			return world->FindByGuid<GameUnitS>(guid);
		}

		GamePlayerS* GetPlayerExecutor() const
		{
			if (!GetExecutor().IsPlayer())
			{
				return nullptr;
			}

			return &GetExecutor().AsPlayer();
		}

		template <class T>
		void SendPacketFromCaster(T generator) const
		{
			auto* worldInstance = GetWorldInstance();
			if (!worldInstance)
			{
				return;
			}

			TileIndex2D tileIndex;
			worldInstance->GetGrid().GetTilePosition(GetExecutor().GetPosition(), tileIndex[0], tileIndex[1]);

			std::vector<char> buffer;
			io::VectorSink sink(buffer);
			game::Protocol::OutgoingPacket packet(sink);
			generator(packet);

			ForEachSubscriberInSight(
				worldInstance->GetGrid(),
				tileIndex,
				[&buffer, &packet](TileSubscriber& subscriber)
				{
					subscriber.SendPacket(packet, buffer);
				});
		}

		template <class T>
		void SendPacketToCaster(T generator) const
		{
			auto* worldInstance = GetWorldInstance();
			if (!worldInstance)
			{
				return;
			}

			TileIndex2D tileIndex;
			worldInstance->GetGrid().GetTilePosition(GetExecutor().GetPosition(), tileIndex[0], tileIndex[1]);

			std::vector<char> buffer;
			io::VectorSink sink(buffer);
			game::Protocol::OutgoingPacket packet(sink);
			generator(packet);

			ForEachSubscriberInSight(
				worldInstance->GetGrid(),
				tileIndex,
				[&buffer, &packet, this](TileSubscriber& subscriber)
				{
					if (&subscriber.GetGameUnit() == &GetExecutor())
					{
						subscriber.SendPacket(packet, buffer);
					}
				});
		}

	private:
		SpellCast& m_cast;
		const proto::SpellEntry& m_spell;
		const SpellTargetMap& m_target;
	};
}

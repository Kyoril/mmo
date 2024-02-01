
#pragma once

#include "creature_ai.h"
#include "game_unit_s.h"
#include "world_instance.h"
#include "base/linear_set.h"

namespace mmo
{
	namespace proto
	{
		class UnitEntry;
		class ItemEntry;
	}

	/// Represents an AI controlled creature unit in the game.
	class GameCreatureS final : public GameUnitS
	{
	public:

		typedef LinearSet<uint64> LootRecipients;
		typedef std::function<const Vector3& ()> RandomPointProc;

	public:

		/// Executed when the unit entry was changed after this creature has spawned. This
		/// can happen if the unit transforms.
		signal<void()> entryChanged;

	public:

		/// Creates a new instance of the GameCreature class.
		explicit GameCreatureS(
			const proto::Project& project,
			TimerQueue& timers,
			const proto::UnitEntry& entry);

		virtual void Initialize() override;

		ObjectTypeId GetTypeId() const override {
			return ObjectTypeId::Unit;
		}

		/// Gets the original unit entry (the one, this creature was spawned with).
		/// This is useful for restoring the original creature state.
		const proto::UnitEntry& GetOriginalEntry() const {
			return m_originalEntry;
		}

		/// Gets the unit entry on which base this creature has been created.
		const proto::UnitEntry& GetEntry() const {
			return *m_entry;
		}

		/// Changes the creatures entry index. Remember, that the creature always has to
		/// have a valid base entry.
		void SetEntry(const proto::UnitEntry& entry);
		
	private:

		std::unique_ptr<CreatureAI> m_ai;
		const proto::UnitEntry& m_originalEntry;
		const proto::UnitEntry* m_entry;
		scoped_connection m_onSpawned;
	};
}

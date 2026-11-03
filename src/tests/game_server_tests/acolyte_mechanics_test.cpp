// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Pins the existing engine mechanics the acolyte kit relies on: Corrupting Touch spreading on
// death through Plaguebearer's proc chance, curse exclusivity (stacking rule CategoryExclusive),
// procs from non-damaging negative spells (Grasping Dread, Lingering Dread), and spell mods on
// summon effects (Gloomling count and lifetime).

#include "catch.hpp"
#include "persistent_pet_fixture.h"

#include "game_server/spells/aura_container.h"
#include "game_server/world/tile_subscriber.h"
#include "game_server/world/visibility_grid.h"
#include "game_server/world/visibility_tile.h"
#include "game/aura.h"
#include "base/clock.h"

#include <set>
#include <utility>

using namespace mmo;
using namespace mmo::pet_tests;

namespace
{
	// Corrupting Touch shape: spreads to enemies around the dying unit
	constexpr uint32 TouchSpellId = 900;
	constexpr uint32 PlaguebearerSpellId = 901;

	// Two curses sharing stacking category 3
	constexpr uint32 CurseASpellId = 910;
	constexpr uint32 CurseBSpellId = 911;

	// Curse of the Mire shape and the passive proccing from it
	constexpr uint32 MireSpellId = 920;
	constexpr uint32 OtherCurseSpellId = 921;
	constexpr uint32 GraspingPassiveSpellId = 922;
	constexpr uint32 GraspingRootSpellId = 923;
	constexpr uint32 MagicDmgPassiveSpellId = 924;
	constexpr uint32 MagicDmgTriggerSpellId = 925;

	// Gloomling shape
	constexpr uint32 SummonSpellId = 930;
	constexpr uint32 SummonCountTalentId = 931;
	constexpr uint32 SummonDurationTalentId = 932;
	constexpr uint32 GloomlingUnitId = 30;

	constexpr uint32 TouchFamily = 0x2;
	constexpr uint32 MireFamily = 0x100;
	constexpr uint32 SummonFamily = 0x4;

	/// A client watching the units around it: on every update it receives, it records the aura
	/// list the world node would send along (Player::NotifyObjectsUpdated sends BuildAuraPacket).
	struct AuraWatcher final : TileSubscriber
	{
		explicit AuraWatcher(GameUnitS& unit)
			: unit(unit)
		{
		}

		GameUnitS& GetGameUnit() const override { return unit; }

		void NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects) override
		{
			for (const GameObjectS* object : objects)
			{
				if (!object->IsUnit() || object->GetGuid() != watchedGuid)
				{
					continue;
				}

				std::vector<char> buffer;
				io::VectorSink sink(buffer);
				io::Writer writer(sink);
				object->AsUnit().BuildAuraPacket(writer);

				io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
				io::Reader reader(source);
				uint64 guid = 0;
				uint32 count = 0;
				reader >> io::read_packed_guid(guid) >> io::read<uint32>(count);

				std::set<std::pair<uint32, uint64>> auras;
				for (uint32 i = 0; i < count; ++i)
				{
					uint32 spellId = 0, remaining = 0;
					uint64 casterId = 0;
					uint8 effectCount = 0, stackCount = 0;
					reader >> io::read<uint32>(spellId) >> io::read<uint32>(remaining) >> io::read_packed_guid(casterId) >> io::read<uint8>(effectCount);
					for (uint8 e = 0; e < effectCount; ++e)
					{
						int32 basePoints = 0;
						reader >> io::read<int32>(basePoints);
					}
					reader >> io::read<uint8>(stackCount);
					auras.emplace(spellId, casterId);
				}

				REQUIRE(reader);
				auraUpdates.push_back(std::move(auras));
			}
		}

		void NotifyObjectsSpawned(const std::vector<GameObjectS*>&, game::spawn_reason::Type) override {}
		void NotifyObjectsDespawned(const std::vector<GameObjectS*>&, game::despawn_reason::Type) override {}
		GameTime ClientToServerTime(const GameTime clientTimestamp) const override { return clientTimestamp; }
		GameTime ServerToClientTime(const GameTime serverTimestamp) const override { return serverTimestamp; }
		bool HasReceivedTimeSyncResponse() const override { return true; }
		void SendPacket(game::Protocol::OutgoingPacket&, const std::vector<char>&, bool) override {}
		bool IsObjectKnown(const uint64 guid) const override { return guid == watchedGuid; }

		GameUnitS& unit;
		uint64 watchedGuid = 0;

		/// (spell id, caster guid) of every visible aura, one set per received update.
		std::vector<std::set<std::pair<uint32, uint64>>> auraUpdates;
	};

	struct AcolyteMechanicsFixture : PersistentPetFixture
	{
		AcolyteMechanicsFixture()
		{
			// Corrupting Touch (live spell 24): the spread is a Death proc on the debuff itself,
			// cast by the debuff's caster on up to maxtargets enemies around the dying unit.
			// procchance 0: only Plaguebearer's ChanceOfSuccess mod makes it proc.
			auto* touch = AddSpell(TouchSpellId, "Corrupting Touch", TouchFamily | 0x1);
			touch->set_duration(30000);
			touch->set_procflags(spell_proc_flags::Death);
			touch->set_procchance(0);
			touch->set_maxtargets(2);
			auto* spread = AddAuraEffect(*touch, aura_type::ProcTriggerSpell, 0, spell_effect_targets::TargetEnemy);
			spread->set_targetb(spell_effect_targets::TargetAreaEnemy);
			spread->set_radius(12.0f);
			spread->set_triggerspell(TouchSpellId);

			// Plaguebearer (live spell 63): +100 ChanceOfSuccess on family 0x2
			auto* plaguebearer = AddSpell(PlaguebearerSpellId, "Plaguebearer", 0, spell_attributes::Passive);
			auto* chance = AddAuraEffect(*plaguebearer, aura_type::AddFlatModifier, 100, spell_effect_targets::Caster);
			chance->set_miscvaluea(spell_mod_op::ChanceOfSuccess);
			chance->set_affectmask(TouchFamily);

			// Two curses in the same exclusive category
			for (const uint32 id : { CurseASpellId, CurseBSpellId })
			{
				auto* curse = AddSpell(id, "Curse", 0x1000);
				curse->set_duration(20000);
				curse->set_stacking_rule(static_cast<uint32>(SpellStackingRule::CategoryExclusive));
				curse->set_stacking_category_id(3);
				AddAuraEffect(*curse, aura_type::ModCastingSpeedPct, 30, spell_effect_targets::TargetEnemy);
			}

			// Curse of the Mire: a negative aura that deals no damage
			auto* mire = AddSpell(MireSpellId, "Curse of the Mire", MireFamily);
			mire->set_duration(10000);
			AddAuraEffect(*mire, aura_type::ModDecreaseSpeed, -30, spell_effect_targets::TargetEnemy);

			auto* otherCurse = AddSpell(OtherCurseSpellId, "Other Curse", 0x200);
			otherCurse->set_duration(10000);
			AddAuraEffect(*otherCurse, aura_type::ModDecreaseSpeed, -30, spell_effect_targets::TargetEnemy);

			// Grasping Dread: roots the victim of the Mire
			AddProcPassive(GraspingPassiveSpellId, spell_proc_flags::DoneSpellNoneDmgClassNeg, MireFamily, GraspingRootSpellId);
			auto* root = AddSpell(GraspingRootSpellId, "Grasping Dread", 0);
			root->set_duration(2000);
			AddAuraEffect(*root, aura_type::ModRoot, 0, spell_effect_targets::TargetEnemy);

			// The same proc keyed on the magic damage flag, which a non-damaging spell must not fire
			AddProcPassive(MagicDmgPassiveSpellId, spell_proc_flags::DoneSpellMagicDmgClassNeg, MireFamily, MagicDmgTriggerSpellId);
			auto* magicTrigger = AddSpell(MagicDmgTriggerSpellId, "Magic Damage Proc", 0);
			magicTrigger->set_duration(2000);
			AddAuraEffect(*magicTrigger, aura_type::ModDecreaseSpeed, -10, spell_effect_targets::TargetEnemy);

			// Gloomlings: two summons for 10 seconds
			auto* gloomling = AddUnitEntry(GloomlingUnitId, "Gloomling", 1);
			gloomling->set_factiontemplate(PlayerTemplate);
			auto* summon = AddSpell(SummonSpellId, "Summon Gloomlings", SummonFamily);
			summon->set_duration(10000);
			auto* summonEffect = summon->add_effects();
			summonEffect->set_index(0);
			summonEffect->set_type(spell_effects::Summon);
			summonEffect->set_targeta(spell_effect_targets::Caster);
			summonEffect->set_basepoints(2);
			summonEffect->set_summonunit(GloomlingUnitId);

			AddModifierTalent(SummonCountTalentId, spell_mod_op::AllEffects, 1, SummonFamily);
			AddModifierTalent(SummonDurationTalentId, spell_mod_op::Duration, 5000, SummonFamily);
		}

		proto::SpellEntry* AddSpell(const uint32 id, const char* name, const uint64 familyFlags, const uint32 attributes = 0)
		{
			auto* spell = project.spells.add(id);
			spell->set_name(name);
			spell->add_attributes(attributes);
			spell->add_attributes(0);
			spell->set_familyflags(familyFlags);
			spell->set_rank(1);
			spell->set_baseid(id);
			return spell;
		}

		proto::SpellEffect* AddAuraEffect(proto::SpellEntry& spell, const aura_type::Type aura, const int32 basePoints, const spell_effect_targets::Type target)
		{
			auto* effect = spell.add_effects();
			effect->set_index(spell.effects_size() - 1);
			effect->set_type(spell_effects::ApplyAura);
			effect->set_aura(static_cast<uint32>(aura));
			effect->set_basepoints(basePoints);
			effect->set_targeta(target);
			return effect;
		}

		void AddProcPassive(const uint32 id, const uint32 procFlags, const uint64 procFamily, const uint32 triggerSpell)
		{
			auto* passive = AddSpell(id, "Proc Passive", 0, spell_attributes::Passive);
			passive->set_procflags(procFlags);
			passive->set_procfamily(procFamily);
			passive->set_procchance(100);
			auto* effect = AddAuraEffect(*passive, aura_type::ProcTriggerSpell, 0, spell_effect_targets::Caster);
			effect->set_targetb(spell_effect_targets::Instigator);
			effect->set_triggerspell(triggerSpell);
		}

		void AddModifierTalent(const uint32 id, const spell_mod_op::Type op, const int32 value, const uint64 mask)
		{
			auto* talent = AddSpell(id, "Modifier Talent", 0, spell_attributes::Passive);
			auto* effect = AddAuraEffect(*talent, aura_type::AddFlatModifier, value, spell_effect_targets::Caster);
			effect->set_miscvaluea(op);
			effect->set_affectmask(mask);
		}

		std::shared_ptr<GameCreatureS> SpawnEnemy(const Vector3& position)
		{
			auto creature = SpawnCreature(*rockUnit, position);
			creature->Set<uint32>(object_fields::MaxHealth, 1000);
			creature->Set<uint32>(object_fields::Health, 1000);
			return creature;
		}

		/// Counts the applied auras of a spell from a caster on a unit.
		static uint32 CountAuras(const GameUnitS& unit, const uint32 spellId, const uint64 casterId)
		{
			uint32 count = 0;
			unit.ForEachAura([&count, spellId, casterId](const AuraContainer& aura)
				{
					if (aura.IsApplied() && aura.GetSpellId() == spellId && aura.GetCasterId() == casterId)
					{
						++count;
					}
				});
			return count;
		}

		static void Kill(GameUnitS& victim, GameUnitS& killer)
		{
			victim.Damage(victim.GetHealth(), spell_school::Normal, &killer, damage_type::MagicalAbility);
			REQUIRE_FALSE(victim.IsAlive());
		}

		std::vector<GameCreatureS*> Gloomlings() const
		{
			std::vector<GameCreatureS*> result;
			world->ForEachObject([&result](GameObjectS& object)
				{
					if (object.IsUnit() && !object.IsPlayer() && object.Get<uint32>(object_fields::Entry) == GloomlingUnitId)
					{
						result.push_back(static_cast<GameCreatureS*>(&object));
					}
					return true;
				});
			return result;
		}
	};
}

TEST_CASE_METHOD(AcolyteMechanicsFixture, "Plaguebearer makes Corrupting Touch spread to two enemies when its victim dies", "[acolyte_mechanics]")
{
	const auto caster = SpawnPlayer(4);
	const auto a = SpawnEnemy(Vector3(5.0f, 0.0f, 0.0f));
	const auto b = SpawnEnemy(Vector3(5.0f, 0.0f, 4.0f));
	const auto c = SpawnEnemy(Vector3(5.0f, 0.0f, -4.0f));
	const auto d = SpawnEnemy(Vector3(9.0f, 0.0f, 0.0f));
	const std::vector<GameUnitS*> neighbours{ b.get(), c.get(), d.get() };
	const uint64 casterId = caster->GetGuid();

	const auto countSpread = [&]()
	{
		uint32 count = 0;
		for (const GameUnitS* unit : neighbours)
		{
			count += CountAuras(*unit, TouchSpellId, casterId);
		}
		return count;
	};

	SECTION("With Plaguebearer exactly two neighbours catch it")
	{
		caster->AddSpell(PlaguebearerSpellId);

		REQUIRE(CastAt(*caster, TouchSpellId, a.get()) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return CountAuras(*a, TouchSpellId, casterId) == 1; }));

		Kill(*a, *caster);
		CHECK(RunUntil([&] { return countSpread() >= 2; }));
		RunFor(std::chrono::milliseconds(200));

		CHECK(countSpread() == 2);
		for (const GameUnitS* unit : neighbours)
		{
			CHECK(CountAuras(*unit, TouchSpellId, casterId) <= 1);
		}
	}

	SECTION("Without Plaguebearer nothing spreads")
	{
		REQUIRE(CastAt(*caster, TouchSpellId, a.get()) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return CountAuras(*a, TouchSpellId, casterId) == 1; }));

		Kill(*a, *caster);
		RunFor(std::chrono::milliseconds(200));

		CHECK(countSpread() == 0);
	}
}

TEST_CASE_METHOD(AcolyteMechanicsFixture, "Corrupting Touch victims dying together spread at most one instance per neighbour", "[acolyte_mechanics]")
{
	const auto caster = SpawnPlayer(4);
	caster->AddSpell(PlaguebearerSpellId);
	const uint64 casterId = caster->GetGuid();

	const auto a = SpawnEnemy(Vector3(5.0f, 0.0f, 0.0f));
	const auto b = SpawnEnemy(Vector3(5.0f, 0.0f, 2.0f));
	const auto c = SpawnEnemy(Vector3(5.0f, 0.0f, -2.0f));
	const auto d = SpawnEnemy(Vector3(8.0f, 0.0f, 1.0f));
	const auto e = SpawnEnemy(Vector3(8.0f, 0.0f, -1.0f));

	for (const auto& victim : { a, b, c })
	{
		REQUIRE(CastAt(*caster, TouchSpellId, victim.get()) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return CountAuras(*victim, TouchSpellId, casterId) == 1; }));
	}

	// All three die before the posted spread casts run
	Kill(*a, *caster);
	Kill(*b, *caster);
	Kill(*c, *caster);

	// C died last, when only D and E were left alive around it
	CHECK(RunUntil([&] { return CountAuras(*d, TouchSpellId, casterId) >= 1 && CountAuras(*e, TouchSpellId, casterId) >= 1; }));
	RunFor(std::chrono::milliseconds(200));

	CHECK(CountAuras(*d, TouchSpellId, casterId) == 1);
	CHECK(CountAuras(*e, TouchSpellId, casterId) == 1);
	for (const auto& dead : { a, b, c })
	{
		CHECK(CountAuras(*dead, TouchSpellId, casterId) == 0);
	}
}

TEST_CASE_METHOD(AcolyteMechanicsFixture, "Curses of one stacking category replace each other per caster", "[acolyte_mechanics]")
{
	const auto x = SpawnPlayer(4);
	const auto y = SpawnPlayer(4);
	const auto target = SpawnEnemy(Vector3(5.0f, 0.0f, 0.0f));
	const uint64 xId = x->GetGuid();
	const uint64 yId = y->GetGuid();

	AuraWatcher watcher(*x);
	watcher.watchedGuid = target->GetGuid();
	TileIndex2D tileIndex;
	world->GetGrid().GetTilePosition(target->GetPosition(), tileIndex[0], tileIndex[1]);
	VisibilityTile& tile = world->GetGrid().RequireTile(tileIndex);
	tile.GetWatchers().add(&watcher);

	std::vector<std::pair<uint32, uint64>> removed;
	scoped_connection removedConnection{ target->auraApplyChanged.connect([&removed](const AuraContainer& aura, const bool applied)
		{
			if (!applied)
			{
				removed.emplace_back(aura.GetSpellId(), aura.GetCasterId());
			}
		}) };

	REQUIRE(CastAt(*x, CurseASpellId, target.get()) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountAuras(*target, CurseASpellId, xId) == 1; }));
	REQUIRE(RunUntil([&] { return !watcher.auraUpdates.empty() && watcher.auraUpdates.back().count({ CurseASpellId, xId }) > 0; }));

	// The newer curse of X replaces X's older one
	watcher.auraUpdates.clear();
	REQUIRE(CastAt(*x, CurseBSpellId, target.get()) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountAuras(*target, CurseBSpellId, xId) == 1; }));
	CHECK(CountAuras(*target, CurseASpellId, xId) == 0);
	CHECK(std::find(removed.begin(), removed.end(), std::make_pair(CurseASpellId, xId)) != removed.end());

	// The observer is sent the aura list without the replaced curse
	REQUIRE(RunUntil([&] { return !watcher.auraUpdates.empty(); }));
	CHECK(watcher.auraUpdates.back().count({ CurseBSpellId, xId }) > 0);
	CHECK_FALSE(watcher.auraUpdates.back().count({ CurseASpellId, xId }) > 0);

	// Another caster's curse of the same category coexists
	REQUIRE(CastAt(*y, CurseASpellId, target.get()) == spell_cast_result::CastOkay);
	REQUIRE(RunUntil([&] { return CountAuras(*target, CurseASpellId, yId) == 1; }));
	CHECK(CountAuras(*target, CurseBSpellId, xId) == 1);

	tile.GetWatchers().remove(&watcher);
}

TEST_CASE_METHOD(AcolyteMechanicsFixture, "A non-damaging negative spell fires DoneSpellNoneDmgClassNeg procs of its family", "[acolyte_mechanics]")
{
	const auto caster = SpawnPlayer(4);
	const auto victim = SpawnEnemy(Vector3(5.0f, 0.0f, 0.0f));
	const uint64 casterId = caster->GetGuid();
	caster->AddSpell(GraspingPassiveSpellId);
	caster->AddSpell(MagicDmgPassiveSpellId);

	SECTION("The Mire roots its victim")
	{
		REQUIRE(CastAt(*caster, MireSpellId, victim.get()) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return CountAuras(*victim, MireSpellId, casterId) == 1; }));

		CHECK(RunUntil([&] { return CountAuras(*victim, GraspingRootSpellId, casterId) == 1; }));

		// The magic damage flag only fires for spells that deal damage
		RunFor(std::chrono::milliseconds(100));
		CHECK(CountAuras(*victim, MagicDmgTriggerSpellId, casterId) == 0);
		CHECK(CountAuras(*caster, GraspingRootSpellId, casterId) == 0);
	}

	SECTION("A curse of another family does not")
	{
		REQUIRE(CastAt(*caster, OtherCurseSpellId, victim.get()) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return CountAuras(*victim, OtherCurseSpellId, casterId) == 1; }));

		RunFor(std::chrono::milliseconds(100));
		CHECK(CountAuras(*victim, GraspingRootSpellId, casterId) == 0);
	}
}

TEST_CASE_METHOD(AcolyteMechanicsFixture, "Summon effects honour AllEffects and Duration spell mods", "[acolyte_mechanics]")
{
	const auto caster = SpawnPlayer(4);

	SECTION("Without talents: base points summons for the spell duration")
	{
		const GameTime castAt = GetAsyncTimeMs();
		REQUIRE(CastAt(*caster, SummonSpellId, nullptr) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return Gloomlings().size() == 2; }));
		RunFor(std::chrono::milliseconds(50));
		REQUIRE(Gloomlings().size() == 2);

		for (const GameCreatureS* gloomling : Gloomlings())
		{
			const GameTime despawnAt = gloomling->GetDespawnTimerEnd();
			CHECK(despawnAt >= castAt + 10000);
			CHECK(despawnAt < castAt + 12000);
		}
	}

	SECTION("With talents: one more summon that lives five seconds longer")
	{
		caster->AddSpell(SummonCountTalentId);
		caster->AddSpell(SummonDurationTalentId);

		const GameTime castAt = GetAsyncTimeMs();
		REQUIRE(CastAt(*caster, SummonSpellId, nullptr) == spell_cast_result::CastOkay);
		REQUIRE(RunUntil([&] { return Gloomlings().size() == 3; }));
		RunFor(std::chrono::milliseconds(50));
		REQUIRE(Gloomlings().size() == 3);

		for (const GameCreatureS* gloomling : Gloomlings())
		{
			const GameTime despawnAt = gloomling->GetDespawnTimerEnd();
			CHECK(despawnAt >= castAt + 15000);
			CHECK(despawnAt < castAt + 17000);
		}
	}
}

TEST_CASE_METHOD(AcolyteMechanicsFixture, "Damage dealt by a proc cast still fires DoneSpellMagicDmgClassNeg procs of its family", "[acolyte_mechanics]")
{
	// Rot Field's zone ticks and Drain Essence's channel ticks are proc casts. Miasma, Creeping Rot
	// and Shared Night key on their damage, which works because the damage effects fire their proc
	// event as a regular one (only proc casts' own non-damage proc events are suppressed).
	constexpr uint32 TickSpellId = 940;
	constexpr uint32 TickPassiveSpellId = 941;
	constexpr uint32 TickSlowSpellId = 942;
	constexpr uint64 RotFamily = 0x800;

	auto* tick = AddSpell(TickSpellId, "Rot Field Tick", RotFamily | 0x1);
	auto* damage = tick->add_effects();
	damage->set_index(0);
	damage->set_type(spell_effects::SchoolDamage);
	damage->set_basepoints(10);
	damage->set_targeta(spell_effect_targets::TargetEnemy);

	AddProcPassive(TickPassiveSpellId, spell_proc_flags::DoneSpellMagicDmgClassNeg, RotFamily, TickSlowSpellId);
	auto* slow = AddSpell(TickSlowSpellId, "Creeping Rot", 0);
	slow->set_duration(4000);
	AddAuraEffect(*slow, aura_type::ModDecreaseSpeed, -30, spell_effect_targets::TargetEnemy);

	const auto caster = SpawnPlayer(4);
	const auto victim = SpawnEnemy(Vector3(5.0f, 0.0f, 0.0f));
	const uint64 casterId = caster->GetGuid();
	caster->AddSpell(TickPassiveSpellId);

	SpellTargetMap targetMap;
	targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	targetMap.SetUnitTarget(victim->GetGuid());
	REQUIRE(caster->CastSpell(targetMap, *project.spells.getById(TickSpellId), 0, true) == spell_cast_result::CastOkay);

	REQUIRE(RunUntil([&] { return victim->GetHealth() < 1000; }));
	CHECK(RunUntil([&] { return CountAuras(*victim, TickSlowSpellId, casterId) == 1; }));
}

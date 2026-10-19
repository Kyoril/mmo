// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_death_state.h"

#include <algorithm>
#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/loot_instance.h"
#include "game_server/player_group.h"
#include "game_server/inventory_types.h"
#include "game_server/world/tile_subscriber.h"
#include "base/clock.h"
#include "binary_io/vector_sink.h"
#include "game/experience.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

namespace mmo
{
	namespace
	{
		/// Horizontal distance from the dying creature within which a player still earns the kill's
		/// experience. The group scan below uses the same radius to decide which party members of a
		/// tagger share in the rewards, so this is what "being at the kill" means here.
		constexpr float KillRewardDistance = 100.0f;
	}

	CreatureAIDeathState::CreatureAIDeathState(CreatureAI& ai)
		: CreatureAIState(ai)
		, m_rollTimeout(ai.GetControlled().GetTimers())
	{
	}

	CreatureAIDeathState::~CreatureAIDeathState()
	{
	}

	void CreatureAIDeathState::OnEnter()
	{
		CreatureAIState::OnEnter();

		// Stop movement immediately
		auto& controlled = GetControlled();
		controlled.GetMover().StopMovement();
		controlled.StopAttack();
		controlled.SetTarget(0);

		const proto::UnitEntry& entry = controlled.GetEntry();

		GameTime despawnDelay = constants::OneSecond * 30;

		// Summons reward nobody: a boss raising adds on a timer would otherwise be a loot and
		// experience farm.
		if (controlled.IsTagged() && controlled.GrantsKillRewards())
		{
			uint32 sumLevel = 0;

			GamePlayerS* maxLevelCharacter = nullptr;
			std::shared_ptr<GamePlayerS> primaryLootRecipient;
			std::map<uint64, std::shared_ptr<GamePlayerS>> lootRecipients;
			controlled.ForEachLootRecipient([&controlled, &lootRecipients, &sumLevel, &maxLevelCharacter, &primaryLootRecipient](const std::shared_ptr<GamePlayerS>& character)
				{
					if (!primaryLootRecipient)
					{
						primaryLootRecipient = character;
					}

					// First add this to the sum of levels
					const uint32 characterLevel = character->GetLevel();
					sumLevel += characterLevel;

					const uint32 xpCutoffLevel = xp::GetExpCutoffLevel(characterLevel);
					if (controlled.GetLevel() > xpCutoffLevel)
					{
						if (!maxLevelCharacter || maxLevelCharacter->GetLevel() < characterLevel)
						{
							maxLevelCharacter = character.get();
						}
					}

					lootRecipients[character->GetGuid()] = character;
				});

			// Now for each loot recipient, check their groups as well and add nearby members as loot recipients
			for (const auto& recipient : lootRecipients)
			{
				if (auto groupId = recipient.second->GetGroupId(); groupId != 0)
				{
					const Vector3 location(controlled.GetPosition());

					// Find nearby group members and make them loot recipients, too
					controlled.GetWorldInstance()->GetUnitFinder().FindUnits(Circle(location.x, location.z, KillRewardDistance), [&lootRecipients, &controlled, groupId, &maxLevelCharacter, &sumLevel](GameUnitS& unit) -> bool
						{
							// Not even a player? Only players get rewards
							if (!unit.IsPlayer())
							{
								return true;
							}

							// Already rewarded?
							if (lootRecipients.contains(unit.GetGuid()))
							{
								return true;
							}

							// Check player group id
							const auto player = std::static_pointer_cast<GamePlayerS>(unit.shared_from_this());
							if (player->GetGroupId() != groupId)
							{
								// Not in our group (we already checked that we are in a group before so this check is enough)
								return true;
							}

							// Alright, this player needs to be considered for rewards as well, increase the sumLevel
							const uint32 characterLevel = unit.GetLevel();
							sumLevel += characterLevel;

							// Remember we rewarded the player
							lootRecipients[player->GetGuid()] = player;

							// Check if this is the max level character if it
							const uint32 xpCutoffLevel = xp::GetExpCutoffLevel(characterLevel);
							if (controlled.GetLevel() > xpCutoffLevel)
							{
								if (!maxLevelCharacter || maxLevelCharacter->GetLevel() < characterLevel)
								{
									maxLevelCharacter = player.get();
								}
							}

							return true;
						});
				}
			}

			// Reward all recipients
			for (const auto& recipient : lootRecipients)
			{
				recipient.second->OnQuestKillCredit(controlled.GetGuid(), controlled.GetEntry());
			}

			// Reward the killer with experience points
			const float t =
				controlled.GetEntry().maxlevel() != controlled.GetEntry().minlevel() ?
				(controlled.GetLevel() - controlled.GetEntry().minlevel()) / (controlled.GetEntry().maxlevel() - controlled.GetEntry().minlevel()) :
				0.0f;

			// Base XP for equal level
			const uint32 baseXp = Interpolate(controlled.GetEntry().minlevelxp(), controlled.GetEntry().maxlevelxp(), t);

			// Scales the base XP by the level difference between the creature and the given level:
			// small bonus for higher-level creatures, shrinking towards zero for creatures below the
			// given level until the cutoff ("grey") level is reached.
			const auto scaleXpByLevelDiff = [&controlled](const uint32 base, const uint32 level) -> uint32
			{
				float xp = static_cast<float>(base);
				if (controlled.GetLevel() > level)
				{
					float levelDiff = controlled.GetLevel() - level;
					levelDiff = std::min<float>(levelDiff, 4);
					xp *= 1.0f + 0.05f * levelDiff;
				}
				else if (controlled.GetLevel() < level)
				{
					const uint32 cutoffLevel = xp::GetExpCutoffLevel(level);
					if (controlled.GetLevel() > cutoffLevel)
					{
						const float zd = static_cast<float>(xp::GetZeroDifference(level));
						xp *= 1.0f - static_cast<float>(level - controlled.GetLevel()) / zd;
					}
					else
					{
						xp = 0.0f;
					}
				}

				return static_cast<uint32>(xp);
			};

			const uint32 xp = maxLevelCharacter ? scaleXpByLevelDiff(baseXp, maxLevelCharacter->GetLevel()) : 0;

			// Class XP mirrors the character XP formula with CLASS levels substituted everywhere:
			// scaling against the highest eligible class level in the group, the grey cutoff against
			// each recipient's own class level, and the group split by class-level share. This way a
			// low-level class still earns meaningful class XP from creatures the character itself has
			// outgrown, while creatures below the class's own grey level grant none.
			uint32 sumClassLevel = 0;
			uint32 maxEligibleClassLevel = 0;
			for (const auto& character : lootRecipients)
			{
				const uint32 classLevel = character.second->GetActiveClassLevel();
				sumClassLevel += classLevel;

				if (controlled.GetLevel() > xp::GetExpCutoffLevel(classLevel) && classLevel > maxEligibleClassLevel)
				{
					maxEligibleClassLevel = classLevel;
				}
			}

			const uint32 classXp = maxEligibleClassLevel > 0 ? scaleXpByLevelDiff(baseXp, maxEligibleClassLevel) : 0;

			// Group xp modifier
			const float groupModifier = xp::GetGroupXpRate(lootRecipients.size(), false);
			for (const auto& character : lootRecipients)
			{
				if (!character.second->IsAlive())
				{
					continue;
				}

				// Experience is earned at the kill. Group members are only picked up within
				// KillRewardDistance of the corpse above, and the player who tagged the creature is
				// no different: once it has died and released to its bind point, or was teleported
				// away, it is not there to earn anything either.
				if (character.second->GetSquaredDistanceTo(controlled.GetPosition(), false) > KillRewardDistance * KillRewardDistance)
				{
					continue;
				}

				// Character XP (skipped when the creature is grey to this character's level).
				const uint32 cutoffLevel = xp::GetExpCutoffLevel(character.second->GetLevel());
				if (controlled.GetLevel() > cutoffLevel)
				{
					character.second->RewardExperience(static_cast<uint32>(
						xp * groupModifier *
							(static_cast<float>(character.second->GetLevel()) / static_cast<float>(sumLevel))));
				}

				// Class XP for the active class, gated by the CLASS level's grey cutoff instead of
				// the character's. Deliberately independent of the character XP gate above: a capped
				// or over-leveled character still levels its class on class-appropriate creatures
				// (RewardClassExperience handles the class-level cap itself).
				const uint32 classLevel = character.second->GetActiveClassLevel();
				if (classXp > 0 && sumClassLevel > 0 && controlled.GetLevel() > xp::GetExpCutoffLevel(classLevel))
				{
					character.second->RewardClassExperience(static_cast<uint32>(
						classXp * groupModifier *
							(static_cast<float>(classLevel) / static_cast<float>(sumClassLevel))));
				}
			}

			// Resolve the loot tables assigned to this creature. Loot tables act as reusable modules and
			// multiple can be assigned to a single creature. For backwards compatibility, a creature that
			// still uses the legacy single 'unitlootentry' field is treated as having a single loot table.
			const auto& unitLoot = controlled.GetProject().unitLoot;
			std::vector<const proto::LootEntry*> lootEntries;
			if (entry.unitlootentries_size() > 0)
			{
				lootEntries.reserve(entry.unitlootentries_size());
				for (int i = 0; i < entry.unitlootentries_size(); ++i)
				{
					if (const auto* loot = unitLoot.getById(entry.unitlootentries(i)))
					{
						lootEntries.push_back(loot);
					}
				}
			}
			else if (const auto* legacyLoot = unitLoot.getById(entry.unitlootentry()))
			{
				lootEntries.push_back(legacyLoot);
			}

			std::unique_ptr<LootInstance> loot;

			// The party of whoever tagged the creature decides how its loot is shared. Only a group has
			// loot rules: a solo tagger loots everything, whatever loot method a previous group left on
			// the character.
			PlayerGroup* lootGroup = primaryLootRecipient ? primaryLootRecipient->GetGroup() : nullptr;
			const LootMethod groupLootMethod = lootGroup ? lootGroup->GetLootMethod() : loot_method::FreeForAll;
			const uint64 lootMasterGuid = lootGroup ? lootGroup->GetLootMasterGuid() : 0;
			if (!lootEntries.empty())
			{
				// Generate loot
				std::vector<std::weak_ptr<GamePlayerS>> weakRecipients;
				for (const auto& recipient : lootRecipients)
				{
					weakRecipients.push_back(recipient.second);
				}

				ASSERT(controlled.GetWorldInstance());
				loot = std::make_unique<LootInstance>(
					controlled.GetProject().items,
					controlled.GetWorldInstance()->GetConditionMgr(),
					controlled.GetGuid(),
					lootEntries,
					weakRecipients,
					groupLootMethod,
					lootMasterGuid);

				// A roll that produced neither gold nor items must not leave a lootable corpse behind:
				// opening an empty loot window over and over reads like a bug to players.
				if (loot->IsEmpty())
				{
					loot.reset();
				}
			}

			if (loot)
			{
				if (primaryLootRecipient && lootGroup)
				{
					loot->SetLootMethod(groupLootMethod, lootGroup->GetLootThreshold());

					if (groupLootMethod != loot_method::FreeForAll)
					{
						std::vector<uint64> nearbyGroupMembers;
						std::set<uint64> nearbyGroupMemberSet;
						for (const auto& recipient : lootRecipients)
						{
							if (recipient.second->GetGroupId() != primaryLootRecipient->GetGroupId())
							{
								continue;
							}

							nearbyGroupMembers.push_back(recipient.first);
							nearbyGroupMemberSet.insert(recipient.first);
						}

						// Round robin, group loot and master loot all hand whatever the party does not
						// distribute to the member whose turn it is.
						loot->SetRoundRobinLooter(lootGroup->GetNextRoundRobinRecipient(nearbyGroupMembers));

						if (groupLootMethod == loot_method::GroupLoot)
						{
							loot->SetupGroupRollItems(nearbyGroupMemberSet);
						}
						else if (groupLootMethod == loot_method::MasterLoot)
						{
							// A loot master who was not around for the kill cannot hand anything out, so
							// the party rolls for it instead of the corpse staying locked.
							if (!loot->SetupMasterLootItems(nearbyGroupMemberSet))
							{
								loot->SetupGroupRollItems(nearbyGroupMemberSet);
							}
						}
					}
				}

				// 3 Minutes of despawn delay if creature still has loot
				despawnDelay = constants::OneMinute * 3;

				// As soon as the loot window is cleared, toggle the flag
				m_onLootCleared = loot->cleared.connect([&controlled]()
					{
						controlled.RemoveFlag<uint32>(object_fields::Flags, unit_flags::Lootable);
					});

				m_onRollWon = loot->rollWon.connect([&controlled](uint64 lootGuid, uint8 slot, uint32 itemId, uint64 winnerGuid, uint8 winningRoll, RollVote winningVote, const String& winnerName)
					{
						const std::shared_ptr<LootInstance> lootInstance = controlled.GetLoot();
						if (!lootInstance)
						{
							return;
						}

						const std::set<uint64> recipientSet(lootInstance->GetRecipients().begin(), lootInstance->GetRecipients().end());

						if (winnerGuid == 0)
						{
							std::vector<char> buffer;
							io::VectorSink sink(buffer);
							game::OutgoingPacket packet(sink);
							packet.Start(game::realm_client_packet::LootAllPassed);
							packet
								<< io::write<uint64>(lootGuid)
								<< io::write<uint8>(slot)
								<< io::write<uint32>(itemId);
							packet.Finish();

							controlled.ForEachSubscriberInSight([&recipientSet, &packet, &buffer](TileSubscriber& subscriber)
							{
								if (!subscriber.GetGameUnit().IsPlayer())
								{
									return;
								}

								if (!recipientSet.contains(subscriber.GetGameUnit().GetGuid()))
								{
									return;
								}

								subscriber.SendPacket(packet, buffer);
							});
							return;
						}

						std::vector<char> wonBuffer;
						io::VectorSink wonSink(wonBuffer);
						game::OutgoingPacket wonPacket(wonSink);
						wonPacket.Start(game::realm_client_packet::LootRollWon);
						wonPacket
							<< io::write<uint64>(lootGuid)
							<< io::write<uint8>(slot)
							<< io::write<uint32>(itemId)
							<< io::write<uint64>(winnerGuid)
							<< io::write<uint8>(winningRoll)
							<< io::write<uint8>(static_cast<uint8>(winningVote))
							<< io::write_dynamic_range<uint8>(winnerName);
						wonPacket.Finish();

						controlled.ForEachSubscriberInSight([&recipientSet, &wonPacket, &wonBuffer](TileSubscriber& subscriber)
						{
							if (!subscriber.GetGameUnit().IsPlayer())
							{
								return;
							}

							if (!recipientSet.contains(subscriber.GetGameUnit().GetGuid()))
							{
								return;
							}

							subscriber.SendPacket(wonPacket, wonBuffer);
						});

						const proto::ItemEntry* itemEntry = controlled.GetProject().items.getById(itemId);
						const LootItem* lootItem = lootInstance->GetLootDefinition(slot);
						if (!itemEntry || !lootItem)
						{
							return;
						}

						controlled.ForEachSubscriberInSight([winnerGuid, itemEntry, lootItem, itemId, &controlled, &recipientSet, lootInstance, slot](TileSubscriber& subscriber)
						{
							if (!subscriber.GetGameUnit().IsPlayer())
							{
								return;
							}

							if (subscriber.GetGameUnit().GetGuid() != winnerGuid)
							{
								return;
							}

							auto& winner = subscriber.GetGameUnit().AsPlayer();
							std::map<uint16, uint16> addedBySlot;
							const auto result = winner.GetInventory().CreateItems(*itemEntry, lootItem->count, &addedBySlot);
							if (result != inventory_change_failure::Okay)
							{
								ELOG("Failed to auto-deliver rolled item " << itemId << " to winner: " << result);
								return;
							}

							for (const auto& [inventorySlot, amount] : addedBySlot)
							{
								const uint8 bag = static_cast<uint8>(inventorySlot >> 8);
								const uint8 subslot = static_cast<uint8>(inventorySlot & 0xFF);

								std::vector<char> pushBuffer;
								io::VectorSink pushSink(pushBuffer);
								game::OutgoingPacket pushPacket(pushSink);
								pushPacket.Start(game::realm_client_packet::ItemPushResult);
								pushPacket
									<< io::write<uint64>(winnerGuid)
									<< io::write<uint8>(1)
									<< io::write<uint8>(0)
									<< io::write<uint8>(bag)
									<< io::write<uint8>(subslot)
									<< io::write<uint32>(itemId)
									<< io::write<uint16>(amount)
									<< io::write<uint16>(amount);
								pushPacket.Finish();
								subscriber.SendPacket(pushPacket, pushBuffer);
							}

							for (const auto& [inventorySlot, amount] : addedBySlot)
							{
								std::vector<char> groupBuffer;
								io::VectorSink groupSink(groupBuffer);
								game::OutgoingPacket groupPacket(groupSink);
								groupPacket.Start(game::realm_client_packet::ItemPushResult);
								groupPacket
									<< io::write<uint64>(winnerGuid)
									<< io::write<uint8>(1)
									<< io::write<uint8>(0)
									<< io::write<uint8>(0xFF)
									<< io::write<uint8>(0xFF)
									<< io::write<uint32>(itemId)
									<< io::write<uint16>(amount)
									<< io::write<uint16>(0);
								groupPacket.Finish();

								controlled.ForEachSubscriberInSight([winnerGuid, &recipientSet, &groupPacket, &groupBuffer](TileSubscriber& other)
								{
									if (!other.GetGameUnit().IsPlayer())
									{
										return;
									}

									if (other.GetGameUnit().GetGuid() == winnerGuid)
									{
										return;
									}

									if (!recipientSet.contains(other.GetGameUnit().GetGuid()))
									{
										return;
									}

									other.SendPacket(groupPacket, groupBuffer);
								});
							}

							lootInstance->TakeItem(slot, winnerGuid);
						});
					});

				m_onRollVoted = loot->rollVoted.connect([&controlled](uint64 lootGuid, uint8 slot, uint32 itemId, uint64 playerGuid, RollVote vote, uint8 rollValue, const String& playerName)
					{
						const std::shared_ptr<LootInstance> lootInstance = controlled.GetLoot();
						if (!lootInstance)
						{
							return;
						}

						const std::set<uint64> recipientSet(lootInstance->GetRecipients().begin(), lootInstance->GetRecipients().end());

						std::vector<char> buffer;
						io::VectorSink sink(buffer);
						game::OutgoingPacket outPacket(sink);
						outPacket.Start(game::realm_client_packet::LootRollResult);
						outPacket
							<< io::write<uint64>(lootGuid)
							<< io::write<uint8>(slot)
							<< io::write<uint32>(itemId)
							<< io::write<uint64>(playerGuid)
							<< io::write<uint8>(static_cast<uint8>(vote))
							<< io::write<uint8>(rollValue)
							<< io::write_dynamic_range<uint8>(playerName);
						outPacket.Finish();

						controlled.ForEachSubscriberInSight([&recipientSet, &outPacket, &buffer](TileSubscriber& subscriber)
						{
							if (!subscriber.GetGameUnit().IsPlayer())
							{
								return;
							}

							if (!recipientSet.contains(subscriber.GetGameUnit().GetGuid()))
							{
								return;
							}

							subscriber.SendPacket(outPacket, buffer);
						});
					});

				// Once the party was asked to roll, the vote ends after RollDuration whether or not
				// everybody answered.
				m_onRollsStarted = loot->rollsStarted.connect([this]()
					{
						m_rollTimeout.SetEnd(GetAsyncTimeMs() + LootInstance::RollDuration);
					});
				m_onRollTimeout = m_rollTimeout.ended.connect([&controlled]()
					{
						// Hold a reference: handing out an expired roll may clear the loot.
						if (const std::shared_ptr<LootInstance> lootInstance = controlled.GetLoot())
						{
							lootInstance->ExpireRolls();
						}
					});

				controlled.SetUnitLoot(std::move(loot));
			}
		}

		// Activate despawn timer
		controlled.TriggerDespawnTimer(despawnDelay);
	}

	void CreatureAIDeathState::OnLeave()
	{
		m_rollTimeout.Cancel();

		CreatureAIState::OnLeave();
	}
}

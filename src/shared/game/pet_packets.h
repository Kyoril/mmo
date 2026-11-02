// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Payloads of the pet packets of the game protocol (game::client_realm_packet::PetCommand and
// following, game::realm_client_packet::PetSpells and following). Each payload is defined together
// with its serializer, so this file is on the protocol fingerprint watch list.

#pragma once

#include "base/typedefs.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "game/pet_defines.h"
#include "game/spell_target_map.h"

#include <algorithm>
#include <vector>

namespace mmo
{
	namespace game
	{
		namespace detail
		{
			/// Writes a string with a uint8 length, cut to 255 bytes.
			inline io::Writer& WritePetString(io::Writer& writer, const String& text)
			{
				const size_t length = std::min<size_t>(text.size(), 255);
				return writer << io::write_dynamic_range<uint8>(text.begin(), text.begin() + length);
			}

			/// The most entries a uint8 counted list can hold.
			constexpr size_t MaxPetListCount = 255;
		}

		/// Payload of client_realm_packet::PetCommand: a command for all pets of the sender.
		struct PetCommandRequest
		{
			/// The command (pet_command).
			uint8 command = 0;
			/// Guid of the target (Attack), otherwise 0.
			uint64 target = 0;
			/// Command parameter: the pet_strategy for SetStrategy, otherwise 0.
			uint32 param = 0;
		};

		/// Writes a PetCommand payload: uint8 command, uint64 target, uint32 param.
		inline io::Writer& operator<<(io::Writer& writer, const PetCommandRequest& data)
		{
			return writer
				<< io::write<uint8>(data.command)
				<< io::write<uint64>(data.target)
				<< io::write<uint32>(data.param);
		}

		/// Reads a PetCommand payload.
		inline io::Reader& operator>>(io::Reader& reader, PetCommandRequest& data)
		{
			return reader
				>> io::read<uint8>(data.command)
				>> io::read<uint64>(data.target)
				>> io::read<uint32>(data.param);
		}

		/// Payload of client_realm_packet::PetCastSpell: the summoned persistent pet casts a spell.
		struct PetCastSpellRequest
		{
			/// The spell, which the pet must know.
			uint32 spellId = 0;
			/// Spell targets, as in CastSpell.
			SpellTargetMap targetMap;
		};

		/// Writes a PetCastSpell payload: uint32 spellId, SpellTargetMap (as CastSpell).
		inline io::Writer& operator<<(io::Writer& writer, const PetCastSpellRequest& data)
		{
			return writer
				<< io::write<uint32>(data.spellId)
				<< data.targetMap;
		}

		/// Reads a PetCastSpell payload.
		inline io::Reader& operator>>(io::Reader& reader, PetCastSpellRequest& data)
		{
			return reader
				>> io::read<uint32>(data.spellId)
				>> data.targetMap;
		}

		/// Payload of client_realm_packet::PetRename: a new name for the summoned persistent pet.
		struct PetRenameRequest
		{
			/// The new name (validated by the world node with IsValidPetName).
			String name;
		};

		/// Writes a PetRename payload: uint8-length name.
		inline io::Writer& operator<<(io::Writer& writer, const PetRenameRequest& data)
		{
			return detail::WritePetString(writer, data.name);
		}

		/// Reads a PetRename payload.
		inline io::Reader& operator>>(io::Reader& reader, PetRenameRequest& data)
		{
			return reader >> io::read_container<uint8>(data.name);
		}

		/// Payload of client_realm_packet::PetNameQuery: asks for the name of a persistent pet.
		struct PetNameQueryRequest
		{
			/// Guid of the pet.
			uint64 guid = 0;
		};

		/// Writes a PetNameQuery payload: uint64 guid.
		inline io::Writer& operator<<(io::Writer& writer, const PetNameQueryRequest& data)
		{
			return writer << io::write<uint64>(data.guid);
		}

		/// Reads a PetNameQuery payload.
		inline io::Reader& operator>>(io::Reader& reader, PetNameQueryRequest& data)
		{
			return reader >> io::read<uint64>(data.guid);
		}

		/// Payload of realm_client_packet::PetNameQueryResult.
		struct PetNameQueryResultData
		{
			/// Guid of the pet that was asked for.
			uint64 guid = 0;
			/// Whether the pet was found.
			bool found = false;
			/// The pet's name; empty if it was not found.
			String name;
		};

		/// Writes a PetNameQueryResult payload: uint64 guid, uint8 found, uint8-length name.
		inline io::Writer& operator<<(io::Writer& writer, const PetNameQueryResultData& data)
		{
			writer
				<< io::write<uint64>(data.guid)
				<< io::write<uint8>(data.found ? 1 : 0);
			return detail::WritePetString(writer, data.name);
		}

		/// Reads a PetNameQueryResult payload.
		inline io::Reader& operator>>(io::Reader& reader, PetNameQueryResultData& data)
		{
			uint8 found = 0;
			if (reader >> io::read<uint64>(data.guid) >> io::read<uint8>(found) >> io::read_container<uint8>(data.name))
			{
				data.found = found != 0;
			}

			return reader;
		}

		/// Payload of realm_client_packet::PetSpells: the summoned persistent pet of the receiver.
		/// A petGuid of 0 means there is no persistent pet (the client clears its pet state).
		struct PetSpellsData
		{
			/// Guid of the summoned persistent pet, 0 if there is none.
			uint64 petGuid = 0;
			/// Its pet entry.
			uint32 petEntry = 0;
			/// Its name.
			String name;
			/// Its level mode (pet_level_mode).
			uint8 levelMode = 0;
			/// Its current strategy (pet_strategy).
			uint8 strategy = 0;
			/// Whether the owner may rename it (the pet entry's allow_player_naming).
			bool canRename = false;
			/// The spells it knows. At most 255 are written.
			std::vector<uint32> spells;
		};

		/// Writes a PetSpells payload: uint64 petGuid, uint32 petEntry, uint8-length name,
		/// uint8 levelMode, uint8 strategy, uint8 canRename, uint8 count, count x uint32 spellId.
		inline io::Writer& operator<<(io::Writer& writer, const PetSpellsData& data)
		{
			writer
				<< io::write<uint64>(data.petGuid)
				<< io::write<uint32>(data.petEntry);
			detail::WritePetString(writer, data.name);

			const size_t count = std::min(data.spells.size(), detail::MaxPetListCount);
			writer
				<< io::write<uint8>(data.levelMode)
				<< io::write<uint8>(data.strategy)
				<< io::write<uint8>(data.canRename ? 1 : 0)
				<< io::write<uint8>(static_cast<uint8>(count));
			for (size_t i = 0; i < count; ++i)
			{
				writer << io::write<uint32>(data.spells[i]);
			}

			return writer;
		}

		/// Reads a PetSpells payload, replacing the content of @p data.
		inline io::Reader& operator>>(io::Reader& reader, PetSpellsData& data)
		{
			uint8 canRename = 0;
			uint8 count = 0;
			if (!(reader
				>> io::read<uint64>(data.petGuid)
				>> io::read<uint32>(data.petEntry)
				>> io::read_container<uint8>(data.name)
				>> io::read<uint8>(data.levelMode)
				>> io::read<uint8>(data.strategy)
				>> io::read<uint8>(canRename)
				>> io::read<uint8>(count)))
			{
				return reader;
			}

			data.canRename = canRename != 0;
			data.spells.resize(count);
			for (uint8 i = 0; i < count; ++i)
			{
				if (!(reader >> io::read<uint32>(data.spells[i])))
				{
					return reader;
				}
			}

			return reader;
		}

		/// Payload of realm_client_packet::PetLearnedSpell: the summoned persistent pet learned a spell.
		struct PetLearnedSpellData
		{
			/// The learned spell.
			uint32 spellId = 0;
		};

		/// Writes a PetLearnedSpell payload: uint32 spellId.
		inline io::Writer& operator<<(io::Writer& writer, const PetLearnedSpellData& data)
		{
			return writer << io::write<uint32>(data.spellId);
		}

		/// Reads a PetLearnedSpell payload.
		inline io::Reader& operator>>(io::Reader& reader, PetLearnedSpellData& data)
		{
			return reader >> io::read<uint32>(data.spellId);
		}

		/// Payload of realm_client_packet::PetXp: experience of the summoned persistent pet
		/// (pet_level_mode::OwnLevel pets only).
		struct PetXpData
		{
			/// Experience towards the next level.
			uint32 xp = 0;
			/// Experience needed for the next level.
			uint32 nextLevelXp = 0;
		};

		/// Writes a PetXp payload: uint32 xp, uint32 nextLevelXp.
		inline io::Writer& operator<<(io::Writer& writer, const PetXpData& data)
		{
			return writer
				<< io::write<uint32>(data.xp)
				<< io::write<uint32>(data.nextLevelXp);
		}

		/// Reads a PetXp payload.
		inline io::Reader& operator>>(io::Reader& reader, PetXpData& data)
		{
			return reader
				>> io::read<uint32>(data.xp)
				>> io::read<uint32>(data.nextLevelXp);
		}

		/// Payload of realm_client_packet::PetResult: why a pet action failed.
		struct PetResultData
		{
			/// The result (pet_result).
			uint8 result = pet_result::Ok;
		};

		/// Writes a PetResult payload: uint8 pet_result.
		inline io::Writer& operator<<(io::Writer& writer, const PetResultData& data)
		{
			return writer << io::write<uint8>(data.result);
		}

		/// Reads a PetResult payload.
		inline io::Reader& operator>>(io::Reader& reader, PetResultData& data)
		{
			return reader >> io::read<uint8>(data.result);
		}
	}
}

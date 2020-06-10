
#pragma once

#include "gender.h"

#include <algorithm>


namespace io
{
	class Reader;
	class Writer;
}


namespace mmo
{
	/// This class contains data of a character for previewing that character.
	/// Currently, this class is used for displaying character in the character
	/// screen of a player.
	class CharacterView final
	{
		friend io::Writer& operator<<(io::Writer& writer, const CharacterView& characterView);
		friend io::Reader& operator>>(io::Reader& reader, CharacterView& out_characterView);

	public:
		/// Default constructor.
		CharacterView() noexcept = default;
		/// Initialize a constant character view.
		CharacterView(
			uint64 guid,
			std::string name, 
			uint8 level, 
			uint32 mapId, 
			uint32 zoneId,
			uint32 raceId, 
			uint32 classId,
			uint8 gender, 
			bool dead) noexcept
			: m_guid(guid)
			, m_name(std::move(name))
			, m_level(level)
			, m_mapId(mapId)
			, m_zoneId(zoneId)
			, m_raceId(raceId)
			, m_classId(classId)
			, m_gender(gender)
			, m_dead(dead)
		{
		}

	public:
		/// Gets the Guid of the character.
		inline uint64 GetGuid() const { return m_guid; }
		/// Gets the name of the character.
		inline const std::string& GetName() const { return m_name; }
		/// Gets the character level.
		inline uint8 GetLevel() const { return m_level; }
		/// Gets the map id of the character.
		inline uint32 GetMapId() const { return m_mapId; }
		/// Gets the zone id of the character.
		inline uint32 GetZoneId() const { return m_zoneId; }
		/// Gets the race id of the character.
		inline uint32 GetRaceId() const { return m_raceId; }
		/// Gets the class id of the character.
		inline uint32 GetClassId() const { return m_classId; }
		/// Gets the gender of the character.
		inline uint8 GetGender() const { return m_gender; }
		/// Gets whether the character is currently dead.
		inline bool IsDead() const { return m_dead; }

	private:
		/// The character guid.
		uint64 m_guid;
		/// The character name.
		std::string m_name;
		/// The character level.
		uint8 m_level = 1;
		/// The current map id.
		uint32 m_mapId = 0;
		/// The current zone id.
		uint32 m_zoneId = 0;
		/// The id of the player race.
		uint32 m_raceId = 0;
		/// The id of the player class.
		uint32 m_classId = 0;
		/// Gets the gender id of the character.
		uint8 m_gender = Male;
		/// Whether the character is currently dead.
		bool m_dead = false;

		// TODO: Add more attributes of a character in here, for example item display id's
		// for previewing a character's equipment, pet display id etc. etc.
	};


	io::Writer& operator<<(io::Writer& writer, const CharacterView& characterView);
	io::Reader& operator>>(io::Reader& reader, CharacterView& out_characterView);
}
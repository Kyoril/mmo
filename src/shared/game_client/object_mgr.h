#pragma once

#include "base/typedefs.h"

#include <memory>

#include "game_client/game_object_c.h"
#include "game_client/game_player_c.h"

namespace mmo
{
	class UnitHandle;
	class PartyInfo;

	namespace proto_client
	{
		class ModelDataEntry;
		class Project;
	}

	class ObjectMgr
	{
	public:
		static void Initialize(const proto_client::Project& project, PartyInfo& partyInfo);

		static void SetUnitNameFontSettings(FontPtr font, MaterialPtr material);

		static const FontPtr& GetUnitNameFont() { return ms_unitNameFont; }

		static const MaterialPtr& GetUnitNameFontMaterial() { return ms_unitNameFontMaterial; }

		template<typename TObject>
		static std::shared_ptr<TObject> Get(uint64 guid)
		{
			if (guid == 0)
			{
				return nullptr;
			}

			if (const auto it = ms_objectyByGuid.find(guid); it != ms_objectyByGuid.end())
			{
				return std::dynamic_pointer_cast<TObject>(it->second);
			}

			return nullptr;
		}

		static void UpdateObjects(float deltaTime);

		static void AddObject(std::shared_ptr<GameObjectC> object);

		static void RemoveObject(uint64 guid);

		/// @brief Removes an object the server destroyed (despawn_reason::Destroyed). A unit whose
		///        display model has a despawn effect leaves the object list at once (no lookups,
		///        targeting or UI find it any more) but stays in the scene until its effect ends.
		///        Anything else is removed like RemoveObject.
		/// @return True if the object now plays its despawn effect.
		static bool DespawnObject(uint64 guid);

		/// @brief Ends the despawn effect of a unit with the given guid at once and destroys it.
		///        Called when the server spawns that guid again while its old unit still dissolves:
		///        the new object wins. No-op if no unit with that guid is despawning.
		static void CancelDespawn(uint64 guid);

		/// @brief Returns true if a unit with the given guid plays its despawn effect.
		static bool IsDespawning(uint64 guid);

		static void RemoveAllObjects();

		static void SetActivePlayer(uint64 guid);

		static uint64 GetActivePlayerGuid();

		/// @brief Sets the guid of the active player's summoned persistent pet (the "pet" unit token).
		/// @param guid The pet's guid, 0 if there is none.
		static void SetActivePetGuid(uint64 guid);

		/// @brief Gets the guid of the active player's summoned persistent pet, 0 if there is none.
		static uint64 GetActivePetGuid();

		static uint64 GetSelectedObjectGuid();
		
		static std::shared_ptr<GameUnitC> GetSelectedObject();

		static uint64 GetHoveredObjectGuid();

		static std::shared_ptr<GameUnitC> GetHoveredUnit();

		static void SetHoveredObject(uint64 hoveredObjectGuid);

		static void SetSelectedObjectGuid(uint64 guid);

		static uint32 GetItemCount(uint32 itemId);

		static std::shared_ptr<GamePlayerC> GetActivePlayer();

		static const proto_client::ModelDataEntry* GetModelData(uint32 displayId);

		static std::shared_ptr<UnitHandle> GetUnitHandleByName(const std::string& unitName);

		static bool FindItem(uint32 entryId, uint8& out_bag, uint8& out_slot, uint64& out_guid);

		template<typename C>
		static void ForEachUnit(const C& callback)
		{
			for (const auto& [guid, object] : ms_objectyByGuid)
			{
				if (!object->IsUnit())
				{
					continue;
				}

				GameUnitC& unit = object->AsUnit();
				callback(unit);
			}
		}

	private:
		static void OnItemStackCountChanged(uint64 itemGuid);

	public:

		template<typename U, typename T>
		static void ForEachObject(T callback)
		{
			for (const auto& [guid, object] : ms_objectyByGuid)
			{
				if (std::shared_ptr<U> unit = std::dynamic_pointer_cast<U>(object))
				{
					callback(unit);
				}
			}
		}

	private:
		static std::map<uint64, std::shared_ptr<GameObjectC>> ms_objectyByGuid;

		/// Units removed from ms_objectyByGuid that still play their despawn effect, by guid.
		static std::map<uint64, std::shared_ptr<GameUnitC>> ms_despawningUnits;
		static uint64 ms_activePlayerGuid;
		static uint64 ms_activePetGuid;
		static uint64 ms_selectedObjectGuid;
		static uint64 ms_hoveredObjectGuid;
		static const proto_client::Project* ms_project;

		static std::map<uint32, uint32> ms_itemCount;
		static std::map<uint64, scoped_connection> ms_itemConnections;

		static PartyInfo* ms_partyInfo;

		static FontPtr ms_unitNameFont;
		static MaterialPtr ms_unitNameFontMaterial;
	};
}

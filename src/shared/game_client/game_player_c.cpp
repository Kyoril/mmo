
#include "game_player_c.h"

#include "net_client.h"
#include "object_mgr.h"
#include "client_data/project.h"
#include "game/guild_info.h"
#include "game/spell.h"
#include "log/default_log_levels.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"
#include "scene_graph/tag_point.h"
#include "shared/client_data/proto_client/item_display.pb.h"

namespace mmo
{
	GamePlayerC::~GamePlayerC()
	{
		ClearAllAttachments();
	}

	void GamePlayerC::Deserialize(io::Reader& reader, bool complete)
	{
		GameUnitC::Deserialize(reader, complete);

		if (complete)
		{
			if (!(reader >> m_configuration))
			{
				ELOG("Failed to read player configuration");
			}

			OnDisplayIdChanged();

			m_equipmentChangedHandler = RegisterMirrorHandler(object_fields::InvSlotHead, player_inventory_pack_slots::Start * 2, *this, &GamePlayerC::OnEquipmentChanged);
			OnEquipmentChanged(GetGuid());

			m_guildChangedHandler = RegisterMirrorHandler(object_fields::Guild, 2, *this, &GamePlayerC::OnGuildChanged);
			OnGuildChanged(GetGuid());
		}

		m_netDriver.GetPlayerName(GetGuid(), std::static_pointer_cast<GamePlayerC>(shared_from_this()));
	}

	void GamePlayerC::Update(const float deltaTime)
	{
		GameUnitC::Update(deltaTime);
	}

	void GamePlayerC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::PlayerFieldCount);
	}

	const String& GamePlayerC::GetName() const
	{
		if (m_name.empty())
		{
			return GameObjectC::GetName();
		}

		return m_name;
	}

	void GamePlayerC::SetName(const String& name)
	{
		m_name = name;
		RefreshUnitName();
	}

	uint8 GamePlayerC::GetAttributeCost(const uint32 attribute) const
	{
		ASSERT(attribute < 5);

		const uint64 attributeCostPacked = Get<uint64>(object_fields::AttributePointCost);
		return (attributeCostPacked >> (attribute * 8)) & 0xFF;
	}

	void GamePlayerC::NotifyItemData(const ItemInfo& data)
	{
		if (data.displayId == 0)
		{
			return;
		}

		const auto* displayData = m_project.itemDisplays.getById(data.displayId);
		if (!displayData)
		{
			return;
		}

		for (const auto& variant : displayData->variants())
		{
			// Does this variant affect us?
			if (variant.model() != 0 && variant.model() != Get<uint32>(object_fields::DisplayId))
			{
				continue;
			}

			// It does affect us!
			for (const auto& subEntity : variant.hidden_by_name())
			{
				auto* sub = m_entity->GetSubEntity(subEntity);
				if (!sub)
				{
					//ELOG("Could not find sub entity " << subEntity << " while trying to apply item display id " << displayData->name() << " to mesh " << m_entity->GetMesh()->GetName());
					continue;
				}

				sub->SetVisible(false);
			}

			for (const auto& subEntity : variant.hidden_by_tag())
			{
				for (uint16 j = 0; j < m_entity->GetNumSubEntities(); ++j)
				{
					SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(j);
					if (subMesh.HasTag(subEntity))
					{
						SubEntity* subEntity = m_entity->GetSubEntity(j);
						ASSERT(subEntity);
						subEntity->SetVisible(false);
					}
				}
			}

			// Now show all entities by name
			for (const auto& subEntity : variant.shown_by_name())
			{
				auto* sub = m_entity->GetSubEntity(subEntity);
				if (!sub)
				{
					//ELOG("Could not find sub entity " << subEntity << " while trying to apply item display id " << displayData->name() << " to mesh " << m_entity->GetMesh()->GetName());
					continue;
				}
				sub->SetVisible(true);
			}

			// Now show all entities by tag
			for (const auto& subEntity : variant.shown_by_tag())
			{
				for (uint16 j = 0; j < m_entity->GetNumSubEntities(); ++j)
				{
					SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(j);
					if (subMesh.HasTag(subEntity))
					{
						SubEntity* subEntity = m_entity->GetSubEntity(j);
						ASSERT(subEntity);
						subEntity->SetVisible(true);
					}
				}
			}

			// Apply sub entity material overrides
			for (const auto& materialOverride : variant.material_overrides())
			{
				auto* sub = m_entity->GetSubEntity(materialOverride.first);
				if (!sub)
				{
					//ELOG("Could not find sub entity " << materialOverride.first << " while trying to apply item display id " << displayData->name() << " to mesh " << m_entity->GetMesh()->GetName());
					continue;
				}

				auto mat = MaterialManager::Get().Load(materialOverride.second);
				if (!mat)
				{
					//ELOG("Could not load material " << materialOverride.second << " while trying to apply item display id " << displayData->name() << " to mesh " << m_entity->GetMesh()->GetName());
					continue;
				}

				sub->SetMaterial(mat);
			}

			// Do we already have that item display applied?
			if (!m_itemAttachments.contains(data.displayId))
			{
				if (variant.has_mesh() && !variant.mesh().empty())
				{
					if (m_entity->GetSkeleton()->HasBone(variant.attached_bone_default().bone_name()))
					{
						ItemAttachment attachment;
						attachment.entity = m_scene.CreateEntity(m_entity->GetName() + "_ITEM_" + std::to_string(data.displayId), variant.mesh());
						attachment.attachment = m_entity->AttachObjectToBone(variant.attached_bone_default().bone_name(), *attachment.entity);
						attachment.attachment->SetScale(Vector3(variant.attached_bone_default().scale_x(), variant.attached_bone_default().scale_y(), variant.attached_bone_default().scale_z()));
						m_itemAttachments[data.displayId] = attachment;
					}
				}
			}
		}
	}

	void GamePlayerC::NotifyGuildInfo(const GuildInfo* guild)
	{
		if (guild == m_guild)
		{
			return;
		}

		m_guild = guild;
		RefreshUnitName();
	}

	const proto_client::ClassEntry* GamePlayerC::GetClass() const
	{
		const uint32 classId = Get<uint32>(object_fields::Class); // Ensure
		if (classId == 0)
		{
			return nullptr;
		}

		return m_project.classes.getById(classId);
	}

	void GamePlayerC::SetupSceneObjects()
	{
		GameUnitC::SetupSceneObjects();

		// TODO: For now we attach a shield to all player characters. This should be done based on the player's equipment though.
		ASSERT(m_entity);

		/*
		if (m_shieldEntity)
		{
			m_entity->DetachObjectFromBone(*m_shieldEntity);
			m_shieldAttachment = nullptr;

			m_scene.DestroyEntity(*m_shieldEntity);
			m_shieldEntity = nullptr;
		}

		if (m_entity->GetSkeleton()->HasBone("Back_M"))
		{
			m_shieldEntity = m_scene.CreateEntity(m_entity->GetName() + "_SHIELD", "Models/Character/Items/Weapons/Shield_Newbie_02.hmsh");
			m_shieldAttachment = m_entity->AttachObjectToBone("Back_M", *m_shieldEntity);
			m_shieldAttachment->SetScale(Vector3::UnitScale * 0.75f);
		}

		if (m_entity->GetSkeleton()->HasBone("Hip_L"))
		{
			m_weaponEntity = m_scene.CreateEntity(m_entity->GetName() + "_WEAPON", "Models/Character/Items/Weapons/Sword_1H_Newbie_01.hmsh");
			m_weaponAttachment = m_entity->AttachObjectToBone("Hip_L", *m_weaponEntity);
			m_weaponAttachment->SetScale(Vector3::UnitScale * 1.0f);
		}
		*/
	}

	void GamePlayerC::RefreshDisplay()
	{
	}

	void GamePlayerC::OnGuildChanged(uint64)
	{
		const uint64 guildId = Get<uint64>(object_fields::Guild);
		if (guildId != 0)
		{
			const auto strong = std::static_pointer_cast<GamePlayerC>(shared_from_this());
			m_netDriver.OnGuildChanged(strong, guildId);
		}
		else if (m_guild)
		{
			m_guild = nullptr;
			RefreshUnitName();
		}
	}

	void GamePlayerC::RefreshUnitName()
	{
		if (!m_nameComponent)
		{
			return;
		}

		std::ostringstream strm;
		strm << GetName();
		if (m_guild && !m_guild->name.empty())
		{
			strm << "\n<" << m_guild->name << ">";
		}

		m_nameComponent->SetText(strm.str());
	}

	void GamePlayerC::ClearAllAttachments()
	{
		for (const auto& attachment : m_itemAttachments)
		{
			if (attachment.second.entity)
			{
				m_entity->DetachObjectFromBone(*attachment.second.entity);
				m_scene.DestroyEntity(*attachment.second.entity);
			}
		}

		m_itemAttachments.clear();
	}

	void GamePlayerC::OnEquipmentChanged(uint64)
	{
		// First ensure customization options are applied to properly display the character
		if (!m_customizationDefinition)
		{
			return;
		}

		// Reset entity to default configuration
		m_entity->ResetSubEntities();

		ClearAllAttachments();

		m_configuration.Apply(*this, *m_customizationDefinition);

		// Now apply each visible item slot
		size_t pendingItems = 0, totalItems = 0;

		static constexpr size_t visibleItemFields = object_fields::VisibleItem2_CREATOR - object_fields::VisibleItem1_CREATOR;

		for (uint16 i = 0; i < 19; ++i)
		{
			const uint32 itemEntry = Get<uint32>(object_fields::VisibleItem1_0 + i * visibleItemFields);
			if (itemEntry == 0)
			{
				continue;
			}

			m_netDriver.GetItemData(itemEntry, std::static_pointer_cast<GamePlayerC>(shared_from_this()));
		}
	}
}

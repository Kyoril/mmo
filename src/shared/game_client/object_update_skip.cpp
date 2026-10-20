// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "object_update_skip.h"

#include "game/field_map.h"
#include "game/movement_info.h"
#include "game/movement_type.h"
#include "binary_io/reader.h"

namespace mmo
{
	bool SkipObjectValueUpdate(io::Reader& reader, const ObjectTypeId typeId)
	{
		// The field count of each type's client object (its InitializeFieldMap), and whether it is
		// a unit, which GameUnitC::Deserialize follows with its movement speeds.
		size_t fieldCount = 0;
		bool isUnit = false;
		switch (typeId)
		{
		case ObjectTypeId::Object:
			fieldCount = object_fields::WorldObjectFieldCount;
			break;
		case ObjectTypeId::Item:
			fieldCount = object_fields::ItemFieldCount;
			break;
		case ObjectTypeId::Container:
			fieldCount = object_fields::BagFieldCount;
			break;
		case ObjectTypeId::Unit:
			fieldCount = object_fields::UnitFieldCount;
			isUnit = true;
			break;
		case ObjectTypeId::Player:
			fieldCount = object_fields::PlayerFieldCount;
			isUnit = true;
			break;
		default:
			return false;
		}

		uint32 updateFlags = 0;
		if (!(reader >> io::read<uint32>(updateFlags)))
		{
			return false;
		}

		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			MovementInfo ignoredMovement;
			if (!(reader >> ignoredMovement))
			{
				return false;
			}
		}

		FieldMap<uint32> ignoredFields;
		ignoredFields.Initialize(fieldCount);
		if (!ignoredFields.DeserializeChanges(reader))
		{
			return false;
		}

		if (isUnit)
		{
			// GameUnitS::WriteObjectUpdateBlock writes one speed per movement type.
			for (int i = 0; i < movement_type::Count; ++i)
			{
				float ignoredSpeed;
				if (!(reader >> io::read<float>(ignoredSpeed)))
				{
					return false;
				}
			}
		}

		return true;
	}
}

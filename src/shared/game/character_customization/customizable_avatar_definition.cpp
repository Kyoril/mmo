// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "customizable_avatar_definition.h"

#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const ChunkMagic VersionChunk = MakeChunkMagic('MVER');
	static const ChunkMagic AvatarDefinitionChunk = MakeChunkMagic('AVDF');
	static const ChunkMagic PropertyGroupChunk = MakeChunkMagic('PRGP');

	constexpr uint32 CurrentVersion = 2;

	CustomizableAvatarDefinition::CustomizableAvatarDefinition(String baseMesh)
		: m_baseMesh(std::move(baseMesh))
	{
		AddChunkHandler(*VersionChunk, true, *this, &CustomizableAvatarDefinition::ReadVersionChunk);
	}

	CustomizableAvatarDefinition::CustomizableAvatarDefinition()
	{
		AddChunkHandler(*VersionChunk, true, *this, &CustomizableAvatarDefinition::ReadVersionChunk);
	}

	void CustomizableAvatarDefinition::AddProperty(std::unique_ptr<CustomizationPropertyGroup> property)
	{
		m_properties.push_back(std::move(property));
	}

	void CustomizableAvatarDefinition::Serialize(io::Writer& writer) const
	{
		// Version chunk
		{
			ChunkWriter versionChunk(VersionChunk, writer);
			writer << io::write<uint32>(CurrentVersion);
			versionChunk.Finish();
		}

		// Avatar definition chunk
		{
			ChunkWriter definitionChunk(AvatarDefinitionChunk, writer);
			writer << io::write_dynamic_range<uint16>(m_baseMesh);

			// Write the next id for the property id generator
			writer << io::write<uint32>(m_propertyIdGenerator.GetCurrentId());

			definitionChunk.Finish();
		}

		// Write property group chunks
		for (auto& property : m_properties)
		{
			ChunkWriter propertyChunk(PropertyGroupChunk, writer);
			writer << io::write_dynamic_range<uint8>(property->GetName());
			writer << io::write<uint32>(static_cast<uint32>(property->GetType()));
			writer << io::write<uint32>(property->GetId());

			switch (property->GetType())
			{
			case CharacterCustomizationPropertyType::MaterialOverride:
			{
				auto& matProp = static_cast<MaterialOverridePropertyGroup&>(*property);

				writer << io::write<uint32>(matProp.idGenerator.GetCurrentId());
				writer << io::write<uint8>(matProp.possibleValues.size());

				for (const auto& value : matProp.possibleValues)
				{
					writer << io::write<uint32>(value.valueId);
					writer << io::write_dynamic_range<uint8>(value.valueName);
					writer << io::write<uint8>(value.subEntityToMaterial.size());
					for (const auto& kvp : value.subEntityToMaterial)
					{
						writer << io::write_dynamic_range<uint8>(kvp.first);
						writer << io::write_dynamic_range<uint16>(kvp.second);
					}
				}
				break;
			}
			case CharacterCustomizationPropertyType::VisibilitySet:
			{
				auto& visProp = static_cast<VisibilitySetPropertyGroup&>(*property);
				writer << io::write_dynamic_range<uint8>(visProp.subEntityTag);
				writer << io::write<uint32>(visProp.idGenerator.GetCurrentId());
				writer << io::write<uint8>(visProp.possibleValues.size());

				for (const auto& value : visProp.possibleValues)
				{
					writer << io::write<uint32>(value.valueId);
					writer << io::write_dynamic_range<uint8>(value.valueName);
					writer << io::write<uint8>(value.visibleSubEntities.size());
					for (const auto& visibleEntityName : value.visibleSubEntities)
					{
						writer << io::write_dynamic_range<uint8>(visibleEntityName);
					}
				}
				break;
			}
			case CharacterCustomizationPropertyType::ScalarParameter:
			{
				auto& scalarProp = static_cast<ScalarParameterPropertyGroup&>(*property);
				writer << io::write<float>(scalarProp.minValue);
				writer << io::write<float>(scalarProp.maxValue);
				break;
			}
			default:
				break;
			}
			propertyChunk.Finish();
		}
	}

	CustomizationPropertyGroup* CustomizableAvatarDefinition::GetProperty(const std::string& name)
	{
		for (auto& property : m_properties)
		{
			if (property->GetName() == name)
			{
				return property.get();
			}
		}
		return nullptr;
	}

	bool CustomizableAvatarDefinition::ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		// We can only have this chunk once
		RemoveChunkHandler(*VersionChunk);

		uint32 version;
		if (!(reader >> io::read<uint32>(version)))
		{
			return false;
		}

		if (version == 0 || version > CurrentVersion)
		{
			ELOG("Unsupported version of customizable avatar definition: " << version);
			return false;
		}

		m_version = version;

		// Add new chunk readers
		AddChunkHandler(*AvatarDefinitionChunk, true, *this, &CustomizableAvatarDefinition::ReadAvatarDefinitionChunk);
		AddChunkHandler(*PropertyGroupChunk, false, *this, &CustomizableAvatarDefinition::ReadPropertyGroupChunk);

		return reader;
	}

	bool CustomizableAvatarDefinition::ReadAvatarDefinitionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		// We can only have this chunk once
		RemoveChunkHandler(*AvatarDefinitionChunk);

		if (!(reader >> io::read_container<uint16>(m_baseMesh)))
		{
			return false;
		}

		m_propertyIdGenerator.Reset();

		if (m_version >= 2)
		{
			uint32 nextId;
			if (!(reader >> io::read<uint32>(nextId)))
			{
				return false;
			}

			m_propertyIdGenerator.NotifyId(nextId);
		}

		return reader;
	}

	bool CustomizableAvatarDefinition::ReadPropertyGroupChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		// Read property group name
		std::string name;
		if (!(reader >> io::read_container<uint8>(name)))
		{
			return false;
		}

		// Read property group type
		uint32 type;
		if (!(reader >> io::read<uint32>(type)))
		{
			return false;
		}

		uint32 nextId;
		if (m_version >= 2)
		{
			// Read the id generator state
			if (!(reader >> io::read<uint32>(nextId)))
			{
				return false;
			}

			m_propertyIdGenerator.NotifyId(nextId);
		}
		else
		{
			nextId = m_propertyIdGenerator.GenerateId();
		}

		std::unique_ptr<CustomizationPropertyGroup> property;
		switch (static_cast<CharacterCustomizationPropertyType>(type))
		{
		case CharacterCustomizationPropertyType::MaterialOverride:
		{
			property = std::make_unique<MaterialOverridePropertyGroup>(nextId, name);
			auto& matProp = static_cast<MaterialOverridePropertyGroup&>(*property);

			if (m_version >= 2)
			{
				if (!(reader >> io::read<uint32>(nextId)))
				{
					return false;
				}

				matProp.idGenerator.NotifyId(nextId);
			}

			uint8 numValues;
			if (!(reader >> io::read<uint8>(numValues)))
			{
				return false;
			}
			for (uint8 i = 0; i < numValues; ++i)
			{
				MaterialOverrideValue value;

				if (m_version >= 2)
				{
					if (!(reader >> io::read<uint32>(value.valueId)))
					{
						return false;
					}

					// Just in case!
					matProp.idGenerator.NotifyId(value.valueId);
				}
				else
				{
					// No id stored before, generate a new one!
					value.valueId = matProp.idGenerator.GenerateId();
				}

				if (!(reader >> io::read_container<uint8>(value.valueName)))
				{
					return false;
				}

				uint8 numPairs;
				if (!(reader >> io::read<uint8>(numPairs)))
				{
					return false;
				}

				for (uint8 j = 0; j < numPairs; ++j)
				{
					String subEntity;
					String material;
					if (!(reader >> io::read_container<uint8>(subEntity)) ||
						!(reader >> io::read_container<uint16>(material)))
					{
						return false;
					}

					value.subEntityToMaterial[subEntity] = material;
				}

				matProp.possibleValues.push_back(value);
			}
			break;
		}
		case CharacterCustomizationPropertyType::VisibilitySet:
		{
			property = std::make_unique<VisibilitySetPropertyGroup>(nextId, name);
			auto& visProp = static_cast<VisibilitySetPropertyGroup&>(*property);
			if (!(reader >> io::read_container<uint8>(visProp.subEntityTag)))
			{
				return false;
			}

			if (m_version >= 2)
			{
				uint32 nextId;
				if (!(reader >> io::read<uint32>(nextId)))
				{
					return false;
				}

				visProp.idGenerator.NotifyId(nextId);
			}

			uint8 numValues;
			if (!(reader >> io::read<uint8>(numValues)))
			{
				return false;
			}
			for (uint8 i = 0; i < numValues; ++i)
			{
				VisibilitySetValue value;

				if (m_version >= 2)
				{
					if (!(reader >> io::read<uint32>(value.valueId)))
					{
						return false;
					}

					// Just in case!
					visProp.idGenerator.NotifyId(value.valueId);
				}
				else
				{
					// No id stored before, generate a new one!
					value.valueId = visProp.idGenerator.GenerateId();
				}

				if (!(reader >> io::read_container<uint8>(value.valueName)))
				{
					return false;
				}
				uint8 numEntities;
				if (!(reader >> io::read<uint8>(numEntities)))
				{
					return false;
				}
				for (uint8 j = 0; j < numEntities; ++j)
				{
					std::string entityName;
					if (!(reader >> io::read_container<uint8>(entityName)))
					{
						return false;
					}
					value.visibleSubEntities.push_back(entityName);
				}
				visProp.possibleValues.push_back(value);
			}
			break;
		}
		case CharacterCustomizationPropertyType::ScalarParameter:
		{
			property = std::make_unique<ScalarParameterPropertyGroup>(nextId, name);
			auto& scalarProp = static_cast<ScalarParameterPropertyGroup&>(*property);
			if (!(reader >> io::read<float>(scalarProp.minValue)) ||
				!(reader >> io::read<float>(scalarProp.maxValue)))
			{
				return false;
			}
			break;
		}
		}

		m_properties.push_back(std::move(property));

		return reader;
	}

	io::Writer& operator<<(io::Writer& writer, const AvatarConfiguration& configuration)
	{
		writer << io::write<uint16>(configuration.chosenOptionPerGroup.size());
		for (const auto& kvp : configuration.chosenOptionPerGroup)
		{
			writer << io::write<uint32>(kvp.first);
			writer << io::write<uint32>(kvp.second);
		}

		writer << io::write<uint16>(configuration.scalarValues.size());
		for (const auto& kvp : configuration.scalarValues)
		{
			writer << io::write<uint32>(kvp.first);
			writer << io::write<float>(kvp.second);
		}

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, AvatarConfiguration& configuration)
	{
		uint16 numGroups;
		if (!(reader >> io::read<uint16>(numGroups)))
		{
			return reader;
		}

		for (uint16 i = 0; i < numGroups; ++i)
		{
			uint32 groupId;
			uint32 valueId;
			if (!(reader >> io::read<uint32>(groupId)) ||
				!(reader >> io::read<uint32>(valueId)))
			{
				return reader;
			}
			configuration.chosenOptionPerGroup[groupId] = valueId;
		}

		uint16 numScalars;
		if (!(reader >> io::read<uint16>(numScalars)))
		{
			return reader;
		}

		for (uint16 i = 0; i < numScalars; ++i)
		{
			uint32 groupId;
			float scalarValue;
			if (!(reader >> io::read<uint32>(groupId)) ||
				!(reader >> io::read<float>(scalarValue)))
			{
				return reader;
			}
			configuration.scalarValues[groupId] = scalarValue;
		}

		return reader;
	}
}

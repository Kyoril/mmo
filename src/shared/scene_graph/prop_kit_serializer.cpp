// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "prop_kit_serializer.h"

#include "base/chunk_writer.h"
#include "base/macros.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace
	{
		const ChunkMagic VersionChunk = MakeChunkMagic('KVER');
		const ChunkMagic NameChunk = MakeChunkMagic('KNAM');
		const ChunkMagic PreviewChunk = MakeChunkMagic('KPRV');
		const ChunkMagic PropsChunk = MakeChunkMagic('KPRP');
		const ChunkMagic LightsChunk = MakeChunkMagic('KLIT');

		constexpr uint32 PropKitVersion = 1;
	}

	void PropKitSerializer::Serialize(const PropKit& kit, io::Writer& writer)
	{
		{
			ChunkWriter chunk(VersionChunk, writer);
			writer << io::write<uint32>(PropKitVersion);
			chunk.Finish();
		}

		{
			ChunkWriter chunk(NameChunk, writer);
			writer << io::write_dynamic_range<uint16>(kit.name);
			chunk.Finish();
		}

		{
			ChunkWriter chunk(PreviewChunk, writer);
			writer << io::write_dynamic_range<uint16>(kit.previewWorldModel);
			chunk.Finish();
		}

		{
			ChunkWriter chunk(PropsChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(kit.props.size()));
			for (const auto& prop : kit.props)
			{
				writer
					<< io::write_dynamic_range<uint16>(prop.meshPath)
					<< io::write_dynamic_range<uint16>(prop.name)
					<< io::write_dynamic_range<uint16>(prop.materialOverride)
					<< io::write<float>(prop.position.x)
					<< io::write<float>(prop.position.y)
					<< io::write<float>(prop.position.z)
					<< io::write<float>(prop.rotation.w)
					<< io::write<float>(prop.rotation.x)
					<< io::write<float>(prop.rotation.y)
					<< io::write<float>(prop.rotation.z)
					<< io::write<float>(prop.scale.x)
					<< io::write<float>(prop.scale.y)
					<< io::write<float>(prop.scale.z)
					<< io::write<uint8>(prop.visible ? 1 : 0);
			}
			chunk.Finish();
		}

		{
			// Same 60 byte entry layout as MOLT version 2.1 in world_model_serializer.cpp
			ChunkWriter chunk(LightsChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(kit.lights.size()));
			for (const auto& light : kit.lights)
			{
				writer
					<< io::write<uint8>(static_cast<uint8>(light.type))
					<< io::write<uint8>(light.useAttenuation ? 1 : 0)
					<< io::write<uint8>(0) // pad
					<< io::write<uint8>(0) // pad
					<< io::write<uint32>(light.color)
					<< io::write<float>(light.position.x)
					<< io::write<float>(light.position.y)
					<< io::write<float>(light.position.z)
					<< io::write<float>(light.intensity)
					<< io::write<float>(light.rotation.x)
					<< io::write<float>(light.rotation.y)
					<< io::write<float>(light.rotation.z)
					<< io::write<float>(light.rotation.w)
					<< io::write<float>(light.attenuationStart)
					<< io::write<float>(light.attenuationEnd)
					<< io::write<float>(light.innerConeAngle)
					<< io::write<float>(light.outerConeAngle)
					<< io::write<float>(light.fogScattering);
			}
			chunk.Finish();
		}
	}

	PropKitDeserializer::PropKitDeserializer(PropKit& kit)
		: ChunkReader(true)
		, m_kit(kit)
	{
		AddChunkHandler(*VersionChunk, true, *this, &PropKitDeserializer::ReadVersionChunk);
	}

	bool PropKitDeserializer::ReadVersionChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *VersionChunk);
		RemoveChunkHandler(*VersionChunk);

		uint32 version = 0;
		if (!(reader >> io::read<uint32>(version)))
		{
			ELOG("Failed to read prop kit version!");
			return false;
		}

		if (version != PropKitVersion)
		{
			ELOG("Unsupported prop kit version: " << version);
			return false;
		}

		AddChunkHandler(*NameChunk, false, *this, &PropKitDeserializer::ReadNameChunk);
		AddChunkHandler(*PreviewChunk, false, *this, &PropKitDeserializer::ReadPreviewChunk);
		AddChunkHandler(*PropsChunk, false, *this, &PropKitDeserializer::ReadPropsChunk);
		AddChunkHandler(*LightsChunk, false, *this, &PropKitDeserializer::ReadLightsChunk);

		return true;
	}

	bool PropKitDeserializer::ReadNameChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		return reader >> io::read_container<uint16>(m_kit.name);
	}

	bool PropKitDeserializer::ReadPreviewChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		return reader >> io::read_container<uint16>(m_kit.previewWorldModel);
	}

	bool PropKitDeserializer::ReadPropsChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		uint32 count = 0;
		if (!(reader >> io::read<uint32>(count)))
		{
			return false;
		}

		m_kit.props.clear();

		for (uint32 i = 0; i < count; ++i)
		{
			WorldModelMeshRef prop;
			float px, py, pz;
			float rw, rx, ry, rz;
			float sx, sy, sz;
			uint8 visible = 1;

			reader
				>> io::read_container<uint16>(prop.meshPath)
				>> io::read_container<uint16>(prop.name)
				>> io::read_container<uint16>(prop.materialOverride)
				>> io::read<float>(px)
				>> io::read<float>(py)
				>> io::read<float>(pz)
				>> io::read<float>(rw)
				>> io::read<float>(rx)
				>> io::read<float>(ry)
				>> io::read<float>(rz)
				>> io::read<float>(sx)
				>> io::read<float>(sy)
				>> io::read<float>(sz)
				>> io::read<uint8>(visible);

			if (!reader)
			{
				return false;
			}

			prop.position = Vector3(px, py, pz);
			prop.rotation = Quaternion(rw, rx, ry, rz);
			prop.scale = Vector3(sx, sy, sz);
			prop.visible = visible != 0;
			m_kit.props.push_back(std::move(prop));
		}

		return true;
	}

	bool PropKitDeserializer::ReadLightsChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		uint32 count = 0;
		if (!(reader >> io::read<uint32>(count)))
		{
			return false;
		}

		m_kit.lights.clear();

		for (uint32 i = 0; i < count; ++i)
		{
			WorldModelLight light;
			uint8 type, useAtten, pad1, pad2;
			float posX, posY, posZ;
			float rotX, rotY, rotZ, rotW;

			reader
				>> io::read<uint8>(type)
				>> io::read<uint8>(useAtten)
				>> io::read<uint8>(pad1)
				>> io::read<uint8>(pad2)
				>> io::read<uint32>(light.color)
				>> io::read<float>(posX)
				>> io::read<float>(posY)
				>> io::read<float>(posZ)
				>> io::read<float>(light.intensity)
				>> io::read<float>(rotX)
				>> io::read<float>(rotY)
				>> io::read<float>(rotZ)
				>> io::read<float>(rotW)
				>> io::read<float>(light.attenuationStart)
				>> io::read<float>(light.attenuationEnd)
				>> io::read<float>(light.innerConeAngle)
				>> io::read<float>(light.outerConeAngle)
				>> io::read<float>(light.fogScattering);

			if (!reader)
			{
				return false;
			}

			light.type = static_cast<WorldModelLight::LightType>(type);
			light.useAttenuation = useAtten != 0;
			light.position = Vector3(posX, posY, posZ);
			light.rotation = Quaternion(rotW, rotX, rotY, rotZ);
			m_kit.lights.push_back(light);
		}

		return true;
	}
}

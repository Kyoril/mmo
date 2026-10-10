// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "collision_recipe.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Upper bound for any count in a recipe. Read counts are checked against it before anything is
		/// sized from them, so a corrupt chunk fails fast instead of allocating gigabytes.
		constexpr uint32 MaxRecipeElements = 65536;

		/// Names are stored with a one-byte length.
		constexpr size_t MaxShapeNameLength = 255;
	}

	io::Writer& operator<<(io::Writer& writer, const CollisionRecipe& recipe)
	{
		writer
			<< io::write<uint32>(CollisionRecipeVersion)
			<< io::write<uint8>(recipe.useRenderGeometry ? 1 : 0)
			<< io::write_dynamic_range<uint32>(recipe.includedSubMeshes)
			<< io::write<uint32>(recipe.shapes.size());

		for (const CollisionShape& shape : recipe.shapes)
		{
			// A longer name would overflow the one-byte length and desync everything after it.
			const size_t nameLength = std::min(shape.name.size(), MaxShapeNameLength);

			writer
				<< io::write<uint8>(shape.type)
				<< io::write<uint8>(shape.op)
				<< io::write_dynamic_range<uint8>(shape.name.begin(), shape.name.begin() + nameLength)
				<< shape.position
				<< io::write<float>(shape.rotation.w)
				<< io::write<float>(shape.rotation.x)
				<< io::write<float>(shape.rotation.y)
				<< io::write<float>(shape.rotation.z)
				<< shape.scale
				<< io::write<uint16>(shape.surfaceSubMesh)
				<< io::write<uint16>(shape.segments)
				<< io::write<float>(shape.innerRadius)
				<< io::write<float>(shape.sweepDegrees)
				<< io::write<float>(shape.thickness)
				<< io::write<uint8>(shape.clockwise ? 1 : 0)
				<< io::write<uint8>(shape.twoSided ? 1 : 0);
		}

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, CollisionRecipe& recipe)
	{
		uint32 version = 0;
		if (!(reader >> io::read<uint32>(version)) || version != CollisionRecipeVersion)
		{
			reader.setFailure();
			return reader;
		}

		uint8 useRender = 0;
		uint32 subMeshCount = 0;
		if (!(reader >> io::read<uint8>(useRender) >> io::read<uint32>(subMeshCount)) || subMeshCount > MaxRecipeElements)
		{
			reader.setFailure();
			return reader;
		}

		recipe.useRenderGeometry = useRender != 0;
		recipe.includedSubMeshes.resize(subMeshCount);
		for (uint16& id : recipe.includedSubMeshes)
		{
			reader >> io::read<uint16>(id);
		}

		uint32 shapeCount = 0;
		if (!(reader >> io::read<uint32>(shapeCount)) || shapeCount > MaxRecipeElements)
		{
			reader.setFailure();
			return reader;
		}

		recipe.shapes.clear();
		for (uint32 i = 0; reader && i < shapeCount; ++i)
		{
			CollisionShape shape;
			uint8 type = 0, op = 0, clockwise = 0, twoSided = 0;
			reader
				>> io::read<uint8>(type)
				>> io::read<uint8>(op)
				>> io::read_container<uint8>(shape.name)
				>> shape.position
				>> io::read<float>(shape.rotation.w)
				>> io::read<float>(shape.rotation.x)
				>> io::read<float>(shape.rotation.y)
				>> io::read<float>(shape.rotation.z)
				>> shape.scale
				>> io::read<uint16>(shape.surfaceSubMesh)
				>> io::read<uint16>(shape.segments)
				>> io::read<float>(shape.innerRadius)
				>> io::read<float>(shape.sweepDegrees)
				>> io::read<float>(shape.thickness)
				>> io::read<uint8>(clockwise)
				>> io::read<uint8>(twoSided);

			if (type >= collision_shape_type::Count_ || op > collision_shape_op::Cut)
			{
				reader.setFailure();
				return reader;
			}

			shape.type = static_cast<collision_shape_type::Type>(type);
			shape.op = static_cast<collision_shape_op::Type>(op);
			shape.clockwise = clockwise != 0;
			shape.twoSided = twoSided != 0;
			recipe.shapes.push_back(shape);
		}

		return reader;
	}

	std::vector<uint16> InferIncludedSubMeshes(const std::vector<uint16>& faceSubMeshes)
	{
		std::vector<uint16> result = faceSubMeshes;
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

	void SanitizeCollisionRecipe(CollisionRecipe& recipe, const uint16 subMeshCount)
	{
		auto& ids = recipe.includedSubMeshes;
		ids.erase(std::remove_if(ids.begin(), ids.end(), [subMeshCount](const uint16 id) { return id >= subMeshCount; }), ids.end());
		ids = InferIncludedSubMeshes(ids);

		for (CollisionShape& shape : recipe.shapes)
		{
			shape = SanitizeCollisionShape(shape);
			if (shape.surfaceSubMesh >= subMeshCount)
			{
				shape.surfaceSubMesh = 0;
			}
		}
	}
}

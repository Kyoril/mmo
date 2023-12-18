
#include "skeleton_serializer.h"

#include "skeleton.h"
#include "base/chunk_writer.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const ChunkMagic SkeletonVersionChunk = MakeChunkMagic('MVER');
	static const ChunkMagic SkeletonMainChunk = MakeChunkMagic('MAIN');
	static const ChunkMagic SkeletonBoneChunk = MakeChunkMagic('BONE');
	static const ChunkMagic SkeletonHierarchyChunk = MakeChunkMagic('HIER');

	void SkeletonSerializer::Export(const Skeleton& skeleton, io::Writer& writer, SkeletonVersion version)
	{
		if (version == skeleton_version::Latest)
		{
			version = skeleton_version::Version_0_1;
		}

		// Main chunk
		ChunkWriter versionChunk{ SkeletonVersionChunk, writer };
		{
			writer << io::write<uint32>(version);
		}
		versionChunk.Finish();

		// Skeleton info
		ChunkWriter mainChunk{ SkeletonMainChunk, writer };
		{
			writer
				<< io::write<uint16>(skeleton.GetBlendMode());
		}
		mainChunk.Finish();

		// Write bones
		for (uint16 i = 0; i < skeleton.GetNumBones(); ++i)
		{
			ChunkWriter boneChunk{ SkeletonBoneChunk, writer };
			{
				const Bone* bone = skeleton.GetBone(i);
				ASSERT(bone);

				writer
					<< io::write_dynamic_range<uint8>(bone->GetName())
					<< io::write<uint16>(bone->GetHandle());

				const auto& pos = bone->GetPosition();
				writer
					<< io::write<float>(pos.x)
					<< io::write<float>(pos.y)
					<< io::write<float>(pos.z);

				const auto& rot = bone->GetOrientation();
				writer
					<< io::write<float>(rot.w)
					<< io::write<float>(rot.x)
					<< io::write<float>(rot.y)
					<< io::write<float>(rot.z);

				const auto& scale = bone->GetScale();
				writer
					<< io::write<float>(scale.x)
					<< io::write<float>(scale.y)
					<< io::write<float>(scale.z);
			}
			boneChunk.Finish();
		}

		// Write bone hierarchy
		for (uint16 i = 0; i < skeleton.GetNumBones(); ++i)
		{
			const Bone* bone = skeleton.GetBone(i);
			ASSERT(bone);

			if (const auto parent = dynamic_cast<Bone*>(bone->GetParent()); parent != nullptr)
			{
				ChunkWriter parentChunk{ SkeletonHierarchyChunk, writer };
				{
					writer
						<< io::write<uint16>(bone->GetHandle())
						<< io::write<uint16>(parent->GetHandle());
				}
				parentChunk.Finish();
			}
		}
	}

	SkeletonDeserializer::SkeletonDeserializer(Skeleton& skeleton)
		: ChunkReader()
		, m_skeleton(skeleton)
	{
		skeleton.Reset(true);

		AddChunkHandler(*SkeletonVersionChunk, true, *this, &SkeletonDeserializer::ReadVersionChunk);
	}

	bool SkeletonDeserializer::ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 version;
		reader >> io::read<uint32>(version);

		m_version = static_cast<SkeletonVersion>(version);

		if (reader)
		{
			if (version >= skeleton_version::Version_0_1)
			{
				AddChunkHandler(*SkeletonMainChunk, true, *this, &SkeletonDeserializer::ReadSkeletonChunk);
				AddChunkHandler(*SkeletonBoneChunk, false, *this, &SkeletonDeserializer::ReadBoneChunk);
				AddChunkHandler(*SkeletonHierarchyChunk, false, *this, &SkeletonDeserializer::ReadHierarchyChunk);
			}
			else
			{
				ELOG("Unknown skeleton version!");
				return false;
			}
		}

		return reader;
	}

	bool SkeletonDeserializer::ReadSkeletonChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint16 blendMode;
		reader >> io::read<uint16>(blendMode);
		if (blendMode >= static_cast<uint16>(SkeletonAnimationBlendMode::Count_))
		{
			ELOG("Unknown blend mode!");
			return false;
		}

		m_skeleton.SetBlendMode(static_cast<SkeletonAnimationBlendMode>(blendMode));

		return reader;
	}

	bool SkeletonDeserializer::ReadBoneChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		String name;
		uint16 handle;
		Vector3 pos;
		Quaternion rot;
		Vector3 scale;

		reader
			>> io::read_container<uint8>(name)
			>> io::read<uint16>(handle)
			>> io::read<float>(pos.x)
			>> io::read<float>(pos.y)
			>> io::read<float>(pos.z)
			>> io::read<float>(rot.w)
			>> io::read<float>(rot.x)
			>> io::read<float>(rot.y)
			>> io::read<float>(rot.z)
			>> io::read<float>(scale.x)
			>> io::read<float>(scale.y)
			>> io::read<float>(scale.z);

		if (reader)
		{
			Bone* bone = m_skeleton.CreateBone(name, handle);
			ASSERT(bone);

			bone->SetPosition(pos);
			bone->SetOrientation(rot);
			bone->SetScale(scale);
			bone->SetBindingPose();
		}

		return reader;
	}

	bool SkeletonDeserializer::ReadHierarchyChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint16 childHandle;
		uint16 parentHandle;
		reader
			>> io::read<uint16>(childHandle)
			>> io::read<uint16>(parentHandle);
		if (childHandle == parentHandle)
		{
			ELOG("Child and parent handle are the same!");
			return false;
		}

		if (childHandle >= m_skeleton.GetNumBones() || parentHandle >= m_skeleton.GetNumBones())
		{
			ELOG("Bone handle is out of range!");
			return false;
		}

		if (reader)
		{
			Bone* child = m_skeleton.GetBone(childHandle);
			ASSERT(child);
			Bone* parent = m_skeleton.GetBone(parentHandle);
			ASSERT(parent);
			parent->AddChild(*child);
		}

		return reader;
	}

	bool SkeletonDeserializer::OnReadFinished() noexcept
	{
		return ChunkReader::OnReadFinished();
	}
}

#pragma once
#include "base/chunk_reader.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class Skeleton;

	namespace skeleton_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_1 = 0x0100,
		};
	}

	typedef skeleton_version::Type SkeletonVersion;

	class SkeletonSerializer
	{
	public:
		void Export(const Skeleton& skeleton, io::Writer& writer, SkeletonVersion version = skeleton_version::Latest);
	};

	class SkeletonDeserializer : public ChunkReader
	{
	public:
		/// @brief Creates a new instance of the MeshDeserializer class and initializes it.
		/// @param skeleton The skeleton which will be updated by this reader.
		explicit SkeletonDeserializer(Skeleton& skeleton);

	protected:
		bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadSkeletonChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadBoneChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadHierarchyChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadAnimationChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	protected:
		bool OnReadFinished() noexcept override;

	private:
		SkeletonVersion m_version;
		Skeleton& m_skeleton;
	};
}
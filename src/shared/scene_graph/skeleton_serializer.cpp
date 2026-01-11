
#include "skeleton_serializer.h"

#include <ranges>

#include "animation_notify.h"
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
	static const ChunkMagic AnimationChunk = MakeChunkMagic('ANIM');
	static const ChunkMagic AnimationNotifyChunk = MakeChunkMagic('NTFY');

	void SkeletonSerializer::Export(const Skeleton& skeleton, io::Writer& writer, SkeletonVersion version)
	{
		if (version == skeleton_version::Latest)
		{
			version = skeleton_version::Version_0_2;
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

		// Write animations
		for(uint16 i = 0; i < skeleton.GetNumAnimations(); ++i)
		{
			const Animation* animation = skeleton.GetAnimation(i);
			ASSERT(animation);

			ChunkWriter animChunk{ AnimationChunk, writer };
			{
				// Write animation header
				writer
					<< io::write_dynamic_range<uint8>(animation->GetName())
					<< io::write<float>(animation->GetDuration())
					<< io::write<float>(animation->GetBaseKeyFrameTime())
					<< io::write_dynamic_range<uint8>(animation->GetBaseKeyFrameAnimationName());

				// Write tracks
				writer << io::write<uint16>(animation->GetNumNodeTracks());
				for (const auto& track : animation->GetNodeTrackList() | std::views::values)
				{
					writer
						<< io::write<uint16>(track->GetHandle())
						<< io::write<uint16>(track->GetNumKeyFrames());

					// Write keyframes
					for (uint16 frameIndex = 0; frameIndex < track->GetNumKeyFrames(); ++frameIndex)
					{
						const auto keyFrame = track->GetNodeKeyFrame(frameIndex);
						writer
							<< io::write<float>(keyFrame->GetTime());

						const auto& translation = keyFrame->GetTranslate();
						writer
							<< io::write<float>(translation.x)
							<< io::write<float>(translation.y)
							<< io::write<float>(translation.z);

						const auto& rotation = keyFrame->GetRotation();
						writer
							<< io::write<float>(rotation.w)
							<< io::write<float>(rotation.x)
							<< io::write<float>(rotation.y)
							<< io::write<float>(rotation.z);

						const auto& scale = keyFrame->GetScale();
						writer
							<< io::write<float>(scale.x)
							<< io::write<float>(scale.y)
							<< io::write<float>(scale.z);
					}
				}

				// Write notifications
				writer << io::write<uint16>(animation->GetNumNotifies());
				for (const auto& notify : animation->GetNotifies())
				{
					writer
						<< io::write<uint8>(notify->GetType())
						<< io::write<float>(notify->GetTime())
						<< io::write_dynamic_range<uint16>(notify->GetName());

					// Write type-specific data
					notify->Serialize(writer);
				}
			}
			animChunk.Finish();
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
			if (m_version >= skeleton_version::Version_0_1)
			{
				AddChunkHandler(*SkeletonMainChunk, true, *this, &SkeletonDeserializer::ReadSkeletonChunk);
				AddChunkHandler(*SkeletonBoneChunk, false, *this, &SkeletonDeserializer::ReadBoneChunk);
				AddChunkHandler(*SkeletonHierarchyChunk, false, *this, &SkeletonDeserializer::ReadHierarchyChunk);
				AddChunkHandler(*AnimationChunk, false, *this, &SkeletonDeserializer::ReadAnimationChunk);
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

	bool SkeletonDeserializer::ReadAnimationChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		String name;
		float duration;
		if (!(reader
			>> io::read_container<uint8>(name)
			>> io::read<float>(duration)))
		{
			ELOG("Failed to read animation header");
			return false;
		}

		Animation& anim = m_skeleton.CreateAnimation(name, duration);

		float baseKeyFrameTime;
		String baseKeyFrameAnimationName;
		if (!(reader 
			>> io::read<float>(baseKeyFrameTime)
			>> io::read_container<uint8>(baseKeyFrameAnimationName)))
		{
			ELOG("Failed to read animation base key frame info");
			return false;
		}

		anim.SetUseBaseKeyFrame(false, baseKeyFrameTime, baseKeyFrameAnimationName);

		uint16 numTracks;
		if (!(reader >> io::read<uint16>(numTracks)))
		{
			ELOG("Failed to read animation track count");
			return false;
		}

		for (uint16 trackIndex = 0; trackIndex < numTracks; ++trackIndex)
		{
			uint16 trackHandle;
			uint16 numKeyFrames;
			if (!(reader
				>> io::read<uint16>(trackHandle)
				>> io::read<uint16>(numKeyFrames)))
			{
				ELOG("Failed to read animation track header");
				return false;
			}

			NodeAnimationTrack* track = anim.CreateNodeTrack(trackHandle, m_skeleton.GetBone(trackHandle));
			ASSERT(track);

			for (uint16 frameIndex = 0; frameIndex < numKeyFrames; ++frameIndex)
			{
				float time;
				Vector3 translation;
				Quaternion rotation;
				Vector3 scale;
				if (!(reader
					>> io::read<float>(time)
					>> io::read<float>(translation.x)
					>> io::read<float>(translation.y)
					>> io::read<float>(translation.z)
					>> io::read<float>(rotation.w)
					>> io::read<float>(rotation.x)
					>> io::read<float>(rotation.y)
					>> io::read<float>(rotation.z)
					>> io::read<float>(scale.x)
					>> io::read<float>(scale.y)
					>> io::read<float>(scale.z)))
				{
					ELOG("Failed to read animation key frame");
					return false;
				}

				auto keyFrame = track->CreateNodeKeyFrame(time);
				keyFrame->SetTranslate(translation);
				keyFrame->SetRotation(rotation);
				keyFrame->SetScale(scale);
			}
		}

		if (m_version >= skeleton_version::Version_0_2)
		{
			// Read notifications if present (for newer file versions)
			uint16 numNotifies = 0;
			if (reader >> io::read<uint16>(numNotifies))
			{
				for (uint16 i = 0; i < numNotifies; ++i)
				{
					auto notify = AnimationNotifyFactory::Deserialize(reader);
					if (notify)
					{
						anim.AddNotify(std::move(notify));
					}
					else
					{
						WLOG("Failed to deserialize animation notify for animation '" << name << "'");
					}
				}
			}
		}
	
		return reader;
	}

	bool SkeletonDeserializer::OnReadFinished()
	{
		m_skeleton.SetBindingPose();

		return ChunkReader::OnReadFinished();
	}
}

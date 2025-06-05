
#include "null_audio.h"

#include "assets/asset_registry.h"

#include "log/default_log_levels.h"

namespace mmo
{
	NullAudio::NullAudio()
	{
		ILOG("Using NULL audio system");

		m_nextSoundInstanceIndex = 0;
		m_soundInstanceVector.resize(100);

		for (auto& channel : m_channelArray)
		{
			channel.Clear();
		}
	}

    NullAudio::~NullAudio()
	{
	}

	void NullAudio::Create()
	{
	}

	void NullAudio::Destroy()
	{
	}

	void NullAudio::Update(const Vector3& listenerPos, float time)
	{
	}

	SoundIndex NullAudio::CreateSound(const String& fileName)
	{
		return CreateSound(fileName, SoundType::Sound3D);
	}

	SoundIndex NullAudio::CreateStream(const String& fileName)
	{
		return CreateSound(fileName, SoundType::Sound2D);
	}

	SoundIndex NullAudio::CreateLoopedSound(const String& fileName)
	{
		return CreateSound(fileName, SoundType::SoundLooped3D);
	}

	SoundIndex NullAudio::CreateLoopedStream(const String& fileName)
	{
		return CreateSound(fileName, SoundType::SoundLooped2D);
	}

	SoundIndex NullAudio::CreateSound(const String& fileName, SoundType type)
	{
		// Check if that sound already exists
		SoundIndex soundIndex = FindSound(fileName, type);
		if (soundIndex != InvalidSound)
		{
			return soundIndex;
		}

		if (!AssetRegistry::HasFile(fileName))
		{
			ELOG("Could not find sound " << fileName << "!");
			return InvalidSound;
		}
		
		// Create new sound instance
		IncrementNextSoundInstanceIndex();
		NullSoundInstance* newSoundInstance = &m_soundInstanceVector[m_nextSoundInstanceIndex];
		newSoundInstance->SetType(type);
		return m_nextSoundInstanceIndex;
	}

	void NullAudio::PlaySound(SoundIndex sound, ChannelIndex* channelIndex, float priority)
	{
		if (sound == InvalidSound)
		{
			return;
		}
        
        if (channelIndex)
        {
            *channelIndex = -1;
        }
	}

	void NullAudio::StopSound(ChannelIndex* channelIndex)
	{
		assert(channelIndex);

		if (*channelIndex == InvalidChannel)
		{
			return;
		}

		m_channelArray[*channelIndex].Clear();
		*channelIndex = InvalidChannel;
	}

	void NullAudio::StopAllSounds()
	{
	}

	SoundIndex NullAudio::FindSound(const String& fileName, SoundType type)
	{
		SoundIndex vectorIndex;
		SoundIndex vectorCapacity;

		vectorCapacity = m_soundInstanceVector.capacity();
		for (vectorIndex = 0; vectorIndex < vectorCapacity; vectorIndex++)
		{
			const auto &instance = m_soundInstanceVector[vectorIndex];
			if ((type == instance.GetType()) && (fileName == instance.GetFileName()))
			{
				return vectorIndex;
			}
		}

		return InvalidSound;
	}

	void NullAudio::Set3DMinMaxDistance(ChannelIndex channelIndex, float minDistance, float maxDistance)
	{
	}

	float NullAudio::GetSoundLength(SoundIndex sound)
	{
		return 0.0f;
	}

	ISoundInstance* NullAudio::GetSoundInstance(SoundIndex sound)
	{
		if (sound == InvalidSound)
		{
			return 0;
		}

		return &m_soundInstanceVector[sound];
	}

	IChannelInstance* NullAudio::GetChannelInstance(ChannelIndex channel)
	{
		if (channel == InvalidChannel)
		{
			return 0;
		}

		assert((channel > 0) && (channel < 8));

		return &m_channelArray[channel];
	}

	void NullAudio::IncrementNextSoundInstanceIndex()
	{
		SoundIndex oldVectorCapacity = m_soundInstanceVector.capacity();
		m_nextSoundInstanceIndex++;

		if (m_nextSoundInstanceIndex < oldVectorCapacity)
		{
			return;
		}

		m_soundInstanceVector.resize(oldVectorCapacity * 2);
	}
}


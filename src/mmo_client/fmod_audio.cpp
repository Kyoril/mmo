
#include "fmod_audio.h"

#include "assets/asset_registry.h"

#include "log/default_log_levels.h"

namespace mmo
{
	FMODSoundInstance::FMODSoundInstance()
	{
		Clear();
	}

	FMODSoundInstance::~FMODSoundInstance()
	{
		Clear();
	}

	void FMODSoundInstance::Clear()
	{
		m_Type = SoundType::Invalid;
		m_Sound = nullptr;
	}

	SoundType FMODSoundInstance::GetType() const
	{
		return m_Type;
	}

	void FMODSoundInstance::SetType(SoundType type)
	{
		m_Type = type;
	}

	void FMODSoundInstance::SetFMODSound(FMOD::Sound* sound)
	{
		m_Sound = sound;
	}

	FMOD::Sound* FMODSoundInstance::GetFMODSound()
	{
		return m_Sound;
	}

	FMODChannelInstance::FMODChannelInstance()
	{
		Clear();
	}

	FMODChannelInstance::~FMODChannelInstance()
	{
	}

	void FMODChannelInstance::Clear()
	{
		
	}

	FMODAudio::FMODAudio()
		: m_system(nullptr)
	{
		ILOG("Using FMOD audio system");

		m_nextSoundInstanceIndex = 0;
		m_soundInstanceVector.resize(100);

		for (auto& channel : m_channelArray)
		{
			channel.Clear();
		}
	}

	FMODAudio::~FMODAudio()
	{
		if (m_system)
		{
			m_system->release();
			m_system = nullptr;
		}
	}

	void FMODAudio::Create()
	{
		FMOD_RESULT result;

		result = FMOD::System_Create(&m_system);
		if (result != FMOD_OK)
		{
			ELOG("FMOD (" << result << "): " << FMOD_ErrorString(result));
			return;
		}

		result = m_system->init(MaximumSoundChannels, FMOD_INIT_NORMAL, 0);
		if (result != FMOD_OK)
		{
			m_system->release();
			m_system = nullptr;
			
			ELOG("FMOD (" << result << "): " << FMOD_ErrorString(result));
			return;
		}
		
		m_system->set3DSettings(DopperScale, DistanceFactor, RolloffScale);

		result = m_system->setFileSystem(&FMODFileOpenCallback, &FMODFileCloseCallback, &FMODFileReadCallback, &FMODFileSeekCallback, nullptr, nullptr, 2048);
		if (result != FMOD_OK)
		{
			m_system->release();
			m_system = 0;

			ELOG("FMOD (" << result << "): " << FMOD_ErrorString(result));
			return;
		}
	}

	void FMODAudio::Destroy()
	{
	}

	void FMODAudio::Update(const Vector3& listenerPos, float time)
	{
		FMOD_VECTOR listenerPosition;
		FMOD_VECTOR listenerForward;
		FMOD_VECTOR listenerUp;
		FMOD_VECTOR listenerVelocity;
		Vector3 vectorVelocity;
		Vector3 vectorForward;
		Vector3 vectorUp;
		
		// Calculate velocity
		if (time > 0.0f)
		{
			vectorVelocity = (listenerPos - m_prevListenerPosition) / time;
		}
		else
		{
			vectorVelocity = Vector3::Zero;
		}
		
		listenerPosition.x = listenerPos.x;
		listenerPosition.y = listenerPos.y;
		listenerPosition.z = listenerPos.z;
		
		// TODO
		listenerForward.x = 1.0f;
		listenerForward.y = 0.0f;
		listenerForward.z = 0.0f;
		
		// TODO
		listenerUp.x = 0.0f;
		listenerUp.y = 0.0f;
		listenerUp.z = 1.0f;
		
		listenerVelocity.x = vectorVelocity.x;
		listenerVelocity.y = vectorVelocity.y;
		listenerVelocity.z = vectorVelocity.z;

		m_system->set3DListenerAttributes(0, &listenerPosition, &listenerVelocity, &listenerForward, &listenerUp);
		m_system->update();
		
		m_prevListenerPosition = listenerPos;

		// TODO: Channel update
	}

	SoundIndex FMODAudio::CreateSound(const String& fileName)
	{
		return CreateSound(fileName, SoundType::Sound3D);
	}

	SoundIndex FMODAudio::CreateStream(const String& fileName)
	{
		return CreateSound(fileName, SoundType::Sound2D);
	}

	SoundIndex FMODAudio::CreateLoopedSound(const String& fileName)
	{
		return CreateSound(fileName, SoundType::SoundLooped3D);
	}

	SoundIndex FMODAudio::CreateLoopedStream(const String& fileName)
	{
		return CreateSound(fileName, SoundType::SoundLooped2D);
	}

	SoundIndex FMODAudio::CreateSound(const String& fileName, SoundType type)
	{
		FMOD_RESULT result;
		FMOD::Sound *sound;
		String fullPathName;
		FMODSoundInstance *newSoundInstance;

		// Check if that sound already exists
		SoundIndex soundIndex = FindSound(fileName, type);
		if (soundIndex != InvalidSound)
		{
			return soundIndex;
		}

		// Apply file name
		fullPathName = fileName;

		if (!AssetRegistry::HasFile(fileName))
		{
			ELOG("Could not find sound " << fileName << "!");
			return InvalidSound;
		}
		
		// Create new sound instance
		IncrementNextSoundInstanceIndex();
		newSoundInstance = &m_soundInstanceVector[m_nextSoundInstanceIndex];
		newSoundInstance->SetFileName(fullPathName);
		newSoundInstance->SetType(type);

		switch (type)
		{
		case SoundType::Sound3D:
			result = m_system->createSound((const char *)newSoundInstance, FMOD_3D, 0, &sound);
			break;

		case SoundType::SoundLooped3D:
			result = m_system->createSound((const char *)newSoundInstance, FMOD_LOOP_NORMAL | FMOD_3D, 0, &sound);
			break;

		case SoundType::Sound2D:
			result = m_system->createSound((const char *)newSoundInstance, FMOD_DEFAULT, 0, &sound);
			break;

		case SoundType::SoundLooped2D:
			result = m_system->createStream((const char *)newSoundInstance, FMOD_LOOP_NORMAL | FMOD_2D, 0, &sound);
			break;

		default:
			ELOG("Could not load sound " << fileName << ": Invalid sound type!");
			return InvalidSound;
		}
		
		if (result != FMOD_OK)
		{
			ELOG("Could not load sound " << fileName << " (" << result << "): " << FMOD_ErrorString(result));
			return InvalidSound;
		}
		
		newSoundInstance->SetFMODSound(sound);
		return m_nextSoundInstanceIndex;
	}

	void FMODAudio::PlaySound(SoundIndex sound, ChannelIndex* channelIndex)
	{
		if (sound == InvalidSound)
		{
			return;
		}

		int channelIndexTemp;
		FMOD::Channel* channel;

		if (channelIndex)
		{
			channelIndexTemp = *channelIndex;
		}
		else
		{
			channelIndexTemp = InvalidChannel;
		}

		assert((sound > 0) && (static_cast<size_t>(sound) < m_soundInstanceVector.capacity()));

		FMODSoundInstance &instance = m_soundInstanceVector[sound];
		FMOD_RESULT result = m_system->playSound(instance.GetFMODSound(), nullptr, true, &channel);
		if (result != FMOD_OK)
		{
			ELOG("Could not play sound (" << result << "): " << FMOD_ErrorString(result));
			if (channelIndex)
			{
				*channelIndex = InvalidChannel;
			}

			return;
		}

		channel->getIndex(&channelIndexTemp);
		channel->setVolume(1.0);
		channel->setPaused(false);

		if (channelIndex)
		{
			*channelIndex = channelIndexTemp;
		}
	}

	void FMODAudio::StopSound(ChannelIndex* channelIndex)
	{
		assert(channelIndex);

		if (*channelIndex == InvalidChannel)
		{
			return;
		}

		FMOD::Channel *soundChannel;
		assert((*channelIndex > 0) && (*channelIndex < MaximumSoundChannels));

		m_system->getChannel(*channelIndex, &soundChannel);
		soundChannel->stop();

		m_channelArray[*channelIndex].Clear();
		*channelIndex = InvalidChannel;
	}

	void FMODAudio::StopAllSounds()
	{
		ChannelIndex channelIndex;
		FMOD_RESULT result;
		FMOD::Channel *nextChannel;

		for (channelIndex = 0; channelIndex < MaximumSoundChannels; channelIndex++)
		{
			result = m_system->getChannel(channelIndex, &nextChannel);
			if ((result == FMOD_OK) && (nextChannel != 0))
			{
				nextChannel->stop();
			}

			m_channelArray[channelIndex].Clear();
		}
	}

	SoundIndex FMODAudio::FindSound(const String& fileName, SoundType type)
	{
		SoundIndex vectorIndex;
		SoundIndex vectorCapacity;

		vectorCapacity = m_soundInstanceVector.capacity();
		for (vectorIndex = 0; vectorIndex < vectorCapacity; vectorIndex++)
		{
			const FMODSoundInstance &instance = m_soundInstanceVector[vectorIndex];
			if ((type == instance.GetType()) && (fileName == instance.GetFileName()))
			{
				return vectorIndex;
			}
		}

		return InvalidSound;
	}

	void FMODAudio::Set3DMinMaxDistance(ChannelIndex channelIndex, float minDistance, float maxDistance)
	{
		FMOD_RESULT    result;
		FMOD::Channel *channel;

		if (channelIndex == InvalidChannel)
		{
			return;
		}

		result = m_system->getChannel(channelIndex, &channel);
		if (result == FMOD_OK)
		{
			channel->set3DMinMaxDistance(minDistance, maxDistance);
		}
	}

	float FMODAudio::GetSoundLength(SoundIndex sound)
	{
		if (sound == InvalidSound)
		{
			return 0.0f;
		}

		ASSERT((sound > 0) && (static_cast<size_t>(sound) < m_soundInstanceVector.capacity()));

		unsigned int   soundLength;   // length in milliseconds
		FMOD_RESULT    result;

		FMODSoundInstance &soundInstance = m_soundInstanceVector[sound];
		if (soundInstance.GetFMODSound())
		{
			result = soundInstance.GetFMODSound()->getLength(&soundLength, FMOD_TIMEUNIT_MS);
			if (result != FMOD_OK)
			{
				ELOG("Could not get sound length (" << result << "): " << FMOD_ErrorString(result));
				return 0.0f;
			}

			return (float)soundLength / 1000.0f;
		}

		return 0.0f;
	}

	ISoundInstance* FMODAudio::GetSoundInstance(SoundIndex sound)
	{
		if (sound == InvalidSound)
		{
			return 0;
		}

		return &m_soundInstanceVector[sound];
	}

	IChannelInstance* FMODAudio::GetChannelInstance(ChannelIndex channel)
	{
		if (channel == InvalidChannel)
		{
			return 0;
		}

		assert((channel > 0) && (channel < MaximumSoundChannels));

		return &m_channelArray[channel];
	}

	void FMODAudio::IncrementNextSoundInstanceIndex()
	{
		SoundIndex oldVectorCapacity = m_soundInstanceVector.capacity();
		m_nextSoundInstanceIndex++;

		if (m_nextSoundInstanceIndex < oldVectorCapacity)
		{
			return;
		}

		m_soundInstanceVector.resize(oldVectorCapacity * 2);
	}

	FMOD_RESULT F_CALLBACK FMODAudio::FMODFileOpenCallback(const char *name, unsigned int *filesize, void **handle, void *userdata)
	{
		assert(name);

		// Create sound instance pointer
		FMODSoundInstance *soundInstance = reinterpret_cast<FMODSoundInstance *>(const_cast<char *>(name));

		*handle = (void *)soundInstance;

		soundInstance->SetStream(AssetRegistry::OpenFile(soundInstance->GetFileName()));
		if (!soundInstance->GetStream())
		{
			*filesize = 0;
			return FMOD_ERR_FILE_NOTFOUND;
		}

		soundInstance->GetStream()->seekg(0, std::ios::end);
		auto size = soundInstance->GetStream()->tellg();
		soundInstance->GetStream()->seekg(0, std::ios::beg);

		*filesize = static_cast<unsigned int>(size);
		return FMOD_OK;
	}

	FMOD_RESULT F_CALLBACK FMODAudio::FMODFileCloseCallback(void* handle, void* userdata)
	{
		FMODSoundInstance *soundInstance;
		soundInstance = static_cast<FMODSoundInstance *>(handle);
		soundInstance->SetStream(nullptr);

		return FMOD_OK;
	}

	FMOD_RESULT F_CALLBACK FMODAudio::FMODFileReadCallback(void* handle, void* buffer, unsigned int sizebytes, unsigned int* bytesread, void* userdata)
	{
		FMODSoundInstance *soundInstance;
		soundInstance = static_cast<FMODSoundInstance *>(handle);

		soundInstance->GetStream()->read((char*)buffer, sizebytes);
		*bytesread = static_cast<unsigned int>(soundInstance->GetStream()->gcount());
		if (*bytesread < sizebytes)
		{
			soundInstance->GetStream()->clear();
			return FMOD_ERR_FILE_EOF;
		}

		return FMOD_OK;
	}

	FMOD_RESULT F_CALLBACK FMODAudio::FMODFileSeekCallback(void* handle, unsigned int pos, void* userdata)
	{
		FMODSoundInstance *soundInstance;

		soundInstance = static_cast<FMODSoundInstance *>(handle);
		soundInstance->GetStream()->seekg(pos, std::ios::beg);

		return FMOD_OK;
	}

}

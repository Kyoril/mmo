
#include "fmod_audio.h"

#include <unordered_set>

#ifdef _WIN32

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
		if (m_channel)
		{
			m_channel->stop();
			m_channel = nullptr;
		}
		Clear();
	}

	void FMODChannelInstance::Clear()
	{
		m_channel = nullptr;
		m_priority = 0.0f;
		m_lastPlayTime = 0;
		m_isPlaying = false;
		m_soundIndex = InvalidSound;
	}

	bool FMODChannelInstance::IsPlaying() const
	{
		if (!m_channel) return false;

		bool isPlaying = false;
		m_channel->isPlaying(&isPlaying);
		return isPlaying;
	}

	float FMODChannelInstance::GetPriority() const
	{
		return m_priority;
	}

	void FMODChannelInstance::SetPriority(float priority)
	{
		m_priority = priority;
	}

	uint64_t FMODChannelInstance::GetLastPlayTime() const
	{
		return m_lastPlayTime;
	}

	void FMODChannelInstance::SetLastPlayTime(uint64_t time)
	{
		m_lastPlayTime = time;
	}

	FMOD::Channel* FMODChannelInstance::GetFMODChannel() const
	{
		return m_channel;
	}

	void FMODChannelInstance::SetFMODChannel(FMOD::Channel* channel)
	{
		m_channel = channel;

		SetPitch(1.0f);
	}

	void FMODChannelInstance::SetPitch(float value)
	{
		if (m_channel)
		{
			m_channel->setPitch(value);
		}
	}

	float FMODChannelInstance::GetPitch() const
	{
		if (m_channel)
		{
			float pitch = 1.0f;
			m_channel->getPitch(&pitch);
			return pitch;
		}

		return 1.0f;
	}

	void FMODChannelInstance::SetVolume(float volume)
	{
		if (m_channel)
		{
			m_channel->setVolume(volume);
		}
	}

	float FMODChannelInstance::GetVolume() const
	{
		if (m_channel)
		{
			float volume = 1.0f;
			m_channel->getVolume(&volume);
			return volume;
		}
		return 1.0f;
	}

	SoundIndex FMODChannelInstance::GetSoundIndex() const
	{
		return m_soundIndex;
	}

	void FMODChannelInstance::SetSoundIndex(SoundIndex soundIndex)
	{
		m_soundIndex = soundIndex;
	}

	FMODAudio::FMODAudio()
		: m_system(nullptr), m_lastCleanupTime(0), m_currentTime(0)
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
		listenerUp.y = 1.0f;
		listenerUp.z = 0.0f;
		
		listenerVelocity.x = 0.0f;
		listenerVelocity.y = 0.0f;
		listenerVelocity.z = 0.0f;

		m_system->set3DListenerAttributes(0, &listenerPosition, &listenerVelocity, &listenerForward, &listenerUp);

		// Increment current time
		m_currentTime++;

		// Periodically clean up unused sounds
		CleanupUnusedSounds();

		// Update channel status
		for (int i = 0; i < MaximumSoundChannels; i++)
		{
			if (m_channelArray[i].GetFMODChannel())
			{
				bool isPlaying = false;
				m_channelArray[i].GetFMODChannel()->isPlaying(&isPlaying);

				if (!isPlaying)
				{
					m_channelArray[i].Clear();
				}
			}
		}

		m_system->update();

		m_prevListenerPosition = listenerPos;
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
		// Check cache first
		String cacheKey = fileName + "_" + std::to_string(static_cast<int>(type));
		auto it = m_soundCache.find(cacheKey);
		if (it != m_soundCache.end())
		{
			// Update last used time
			it->second.lastUsedTime = m_currentTime;
			return it->second.soundIndex;
		}

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

		// Add to cache
		SoundCacheEntry cacheEntry;
		cacheEntry.soundIndex = m_nextSoundInstanceIndex;
		cacheEntry.lastUsedTime = m_currentTime;
		m_soundCache[cacheKey] = cacheEntry;

		return m_nextSoundInstanceIndex;
	}

	void FMODAudio::PlaySound(SoundIndex sound, ChannelIndex* channelIndex, float priority)
	{
		if (sound == InvalidSound)
		{
			return;
		}

		// Increment time for channel tracking
		m_currentTime++;

		// Update last used time for this sound in cache
		for (auto& pair : m_soundCache)
		{
			if (pair.second.soundIndex == sound)
			{
				pair.second.lastUsedTime = m_currentTime;
				break;
			}
		}

		// Find a suitable channel
		ChannelIndex channelIndexTemp;
		if (channelIndex && *channelIndex != InvalidChannel)
		{
			// Use the specified channel if provided, but first stop any sound playing on it
			channelIndexTemp = *channelIndex;
			if (m_channelArray[channelIndexTemp].IsPlaying())
			{
				FMOD::Channel* oldChannel = m_channelArray[channelIndexTemp].GetFMODChannel();
				if (oldChannel)
				{
					oldChannel->stop();
				}
				m_channelArray[channelIndexTemp].Clear();
			}
		}
		else
		{
			// Find an available channel based on priority
			channelIndexTemp = FindAvailableChannel(priority);
			if (channelIndexTemp == InvalidChannel)
			{
				WLOG("Could not find available channel for sound with priority " << priority);
				if (channelIndex) *channelIndex = InvalidChannel;
				return;
			}
		}

		FMOD::Channel* channel;
		assert((sound > 0) && (static_cast<size_t>(sound) < m_soundInstanceVector.capacity()));

		FMODSoundInstance& instance = m_soundInstanceVector[sound];
		FMOD_RESULT result = m_system->playSound(instance.GetFMODSound(), nullptr, true, &channel);
		if (result != FMOD_OK)
		{
			ELOG("Could not play sound (" << result << "): " << FMOD_ErrorString(result));
			if (channelIndex) *channelIndex = InvalidChannel;
			return;
		}

		// Configure the channel
		channel->setVolume(1.0);

		// For 3D sounds, set proper rolloff mode to ensure sounds beyond max distance are inaudible
		if (instance.GetType() == SoundType::Sound3D || instance.GetType() == SoundType::SoundLooped3D)
		{
			channel->setMode(FMOD_3D | FMOD_3D_LINEARSQUAREROLLOFF);
			channel->set3DLevel(1.0f);
		}
			channel->setPaused(false);

		// Store the sound index with the channel for proper tracking
		int channelIndex32;
		channel->getIndex(&channelIndex32);
		channelIndexTemp = static_cast<ChannelIndex>(channelIndex32);

		// Update channel tracking info
		m_channelArray[channelIndexTemp].SetFMODChannel(channel);
		m_channelArray[channelIndexTemp].SetPriority(priority);
		m_channelArray[channelIndexTemp].SetLastPlayTime(m_currentTime);
		m_channelArray[channelIndexTemp].SetSoundIndex(sound);

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

		// Validate channel index is within bounds
		assert(*channelIndex < MaximumSoundChannels);

		// Get the channel from our tracking array first
		FMOD::Channel* channel = m_channelArray[*channelIndex].GetFMODChannel();

		// Only try to stop if we have a valid channel pointer
		if (channel)
		{
			bool isPlaying = false;
			channel->isPlaying(&isPlaying);

			if (isPlaying)
			{
				FMOD_RESULT result = channel->stop();
				if (result != FMOD_OK)
				{
					WLOG("Failed to stop channel " << *channelIndex << " (" << result << "): " << FMOD_ErrorString(result));
				}
			}
		}
		else
		{
			// Try to get the channel directly from FMOD if our tracking failed
			FMOD_RESULT result = m_system->getChannel(*channelIndex, &channel);
			if (result == FMOD_OK && channel)
			{
				channel->stop();
			}
		}

		// Always clear our tracking info for this channel
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
			channel->setMode(FMOD_3D | FMOD_3D_LINEARSQUAREROLLOFF);
		}
	}

	void FMODAudio::Set3DPosition(ChannelIndex channelIndex, const Vector3& position)
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
			FMOD_VECTOR fmodPos = { position.x, position.y, position.z };
			FMOD_VECTOR fmodVel = { 0.0f, 0.0f, 0.0f };  // No velocity for now
			channel->set3DAttributes(&fmodPos, &fmodVel);
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

	ChannelIndex FMODAudio::FindAvailableChannel(float priority)
	{
		// First try to find an inactive channel
		for (ChannelIndex i = 0; i < MaximumSoundChannels; i++)
		{
			if (!m_channelArray[i].IsPlaying())
			{
				return i;
			}
		}

		// If all channels are active, find the oldest, lowest priority channel
		ChannelIndex oldestChannel = 0;
		float lowestPriority = std::numeric_limits<float>::max();
		uint64_t oldestTime = m_currentTime;

		for (ChannelIndex i = 0; i < MaximumSoundChannels; i++)
		{
			// Skip channels with higher priority than the requested sound
			if (m_channelArray[i].GetPriority() > priority)
				continue;

			// Find the oldest channel with the lowest priority
			if (m_channelArray[i].GetPriority() < lowestPriority ||
				(m_channelArray[i].GetPriority() == lowestPriority &&
					m_channelArray[i].GetLastPlayTime() < oldestTime))
			{
				lowestPriority = m_channelArray[i].GetPriority();
				oldestTime = m_channelArray[i].GetLastPlayTime();
				oldestChannel = i;
			}
		}

		// If we found a channel with lower priority, stop it and reuse
		if (lowestPriority < priority || oldestTime < m_currentTime)
		{
			FMOD::Channel* channel = m_channelArray[oldestChannel].GetFMODChannel();
			if (channel)
			{
				channel->stop();
			}

			m_channelArray[oldestChannel].Clear();
			return oldestChannel;
		}

		// No suitable channel found
		return InvalidChannel;
	}

	SoundIndex FMODAudio::AllocateSoundIndex()
	{
		if (!m_freeSoundIndices.empty())
		{
			SoundIndex index = m_freeSoundIndices.back();
			m_freeSoundIndices.pop_back();
			return index;
		}

		IncrementNextSoundInstanceIndex();
		return m_nextSoundInstanceIndex;
	}

	void FMODAudio::ReleaseSoundIndex(SoundIndex index)
	{
		if (index != InvalidSound && index < m_soundInstanceVector.capacity())
		{
			// Release the FMOD sound object before clearing
			FMODSoundInstance& instance = m_soundInstanceVector[index];
			FMOD::Sound* sound = instance.GetFMODSound();
			if (sound)
			{
				sound->release();
			}
			
			m_soundInstanceVector[index].Clear();
			m_freeSoundIndices.push_back(index);
		}
	}

	void FMODAudio::CleanupUnusedSounds(bool forceCleanup)
	{
		if (!forceCleanup && (m_currentTime - m_lastCleanupTime < CleanupInterval))
		{
			return;
		}

		m_lastCleanupTime = m_currentTime;

		// Check which sounds are currently being used by active channels
		std::unordered_set<SoundIndex> activeSounds;

		for (int i = 0; i < MaximumSoundChannels; i++)
		{
			if (m_channelArray[i].IsPlaying())
			{
				SoundIndex soundIndex = m_channelArray[i].GetSoundIndex();
				if (soundIndex != InvalidSound)
				{
					activeSounds.insert(soundIndex);
				}
			}
		}

		// For cached sounds that haven't been used recently, release them
		std::vector<String> soundsToRemove;
		const uint64_t unusedTimeThreshold = 5000; // Remove sounds not used for 5000 updates

		for (const auto& pair : m_soundCache)
		{
			const SoundCacheEntry& entry = pair.second;
			
			// Skip if sound is currently active
			if (activeSounds.find(entry.soundIndex) != activeSounds.end())
			{
				continue;
			}

			// Check if sound hasn't been used recently
			if (m_currentTime - entry.lastUsedTime > unusedTimeThreshold)
			{
				soundsToRemove.push_back(pair.first);
			}
		}

		// Remove unused sounds from cache and release their indices
		for (const auto& soundName : soundsToRemove)
		{
			auto it = m_soundCache.find(soundName);
			if (it != m_soundCache.end())
			{
				ReleaseSoundIndex(it->second.soundIndex);
				m_soundCache.erase(it);
			}
		}
	}
}

#endif


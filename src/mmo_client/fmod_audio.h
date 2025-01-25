
#pragma once

#include "audio.h"

#include <fmod/inc/fmod.hpp>
#include <fmod/inc/fmod_errors.h>

#include <vector>

namespace mmo
{
	static const int32 MaximumSoundChannels = 64;
	static const float DopperScale = 1.0f;
	static const float DistanceFactor = 1.0f;
	static const float RolloffScale = 0.5f;

	
	class FMODSoundInstance final : public ISoundInstance
	{
	public:
		FMODSoundInstance();
		FMODSoundInstance(FMODSoundInstance&& other)
			: m_fileName(std::move(other.m_fileName))
			, m_Type(other.m_Type)
			, m_Sound(other.m_Sound)
			, m_stream(std::move(other.m_stream))
		{
			other.m_Type = SoundType::Invalid;
			other.m_Sound = nullptr;
		}
		~FMODSoundInstance() override;

	public:
		void Clear() override;

		void SetFileName(String fileName) { m_fileName = std::move(fileName); }

		const String& GetFileName() const { return m_fileName; }

		void SetStream(std::unique_ptr<std::istream> stream){ m_stream = std::move(stream); }

		std::istream* GetStream() const { return m_stream.get(); }

		SoundType GetType() const override;

		void SetType(SoundType type) override;

		void SetFMODSound(FMOD::Sound *sound);

		FMOD::Sound* GetFMODSound();

	private:
		String m_fileName;
		SoundType m_Type;
		FMOD::Sound *m_Sound;
		std::unique_ptr<std::istream> m_stream;
	};

	
	class FMODChannelInstance final : public IChannelInstance
	{
	public:
		FMODChannelInstance();
		~FMODChannelInstance() override;

	public:
		void Clear() override;
	};

	
	class FMODAudio final : public IAudio
	{
	public:
		explicit FMODAudio();
		~FMODAudio() override;

	public:
		void Create() override;

		void Destroy() override;

		void Update(const Vector3& listenerPosition, float time) override;

		SoundIndex CreateSound(const String &fileName) override;

		SoundIndex CreateStream(const String &fileName) override;

		SoundIndex CreateLoopedSound(const String &fileName) override;

		SoundIndex CreateLoopedStream(const String &fileName) override;

		SoundIndex CreateSound(const String &fileName, SoundType type) override;

		void PlaySound(SoundIndex sound, ChannelIndex *channelIndex) override;

		void StopSound(ChannelIndex *channelIndex) override;

		void StopAllSounds() override;

		SoundIndex FindSound(const String &fileName, SoundType type) override;

		void Set3DMinMaxDistance(ChannelIndex channelIndex, float minDistance, float maxDistance) override;

		float GetSoundLength(SoundIndex sound) override;

		ISoundInstance *GetSoundInstance(SoundIndex sound) override;

		IChannelInstance *GetChannelInstance(ChannelIndex channel) override;

	private:

		typedef std::vector<FMODSoundInstance> SoundInstanceVector;
		typedef SoundInstanceVector::iterator SoundInstanceVectorItr;

		SoundIndex m_nextSoundInstanceIndex;
		FMOD::System *m_system;
		Vector3	m_prevListenerPosition;
		SoundInstanceVector m_soundInstanceVector;
		FMODChannelInstance m_channelArray[MaximumSoundChannels];

		void IncrementNextSoundInstanceIndex();

		static FMOD_RESULT F_CALLBACK FMODFileOpenCallback(const char *name, unsigned int *filesize, void **handle, void *userdata);
		static FMOD_RESULT F_CALLBACK FMODFileCloseCallback(void *handle, void *userdata);
		static FMOD_RESULT F_CALLBACK FMODFileReadCallback(void *handle, void *buffer, unsigned int sizebytes, unsigned int *bytesread, void *userdata);
		static FMOD_RESULT F_CALLBACK FMODFileSeekCallback(void *handle, unsigned int pos, void *userdata);
	};
}


#pragma once

#include "shared/audio/audio.h"

#include <vector>

namespace mmo
{
	class NullSoundInstance final : public ISoundInstance
	{
	public:
        NullSoundInstance(){}
        NullSoundInstance(NullSoundInstance&& other)
			: m_fileName(std::move(other.m_fileName))
			, m_Type(other.m_Type)
			, m_stream(std::move(other.m_stream))
		{
			other.m_Type = SoundType::Invalid;
		}
        ~NullSoundInstance() override {}

	public:
		void Clear() override
        {
        }

		void SetFileName(String fileName) { m_fileName = std::move(fileName); }

		const String& GetFileName() const { return m_fileName; }

		void SetStream(std::unique_ptr<std::istream> stream){ m_stream = std::move(stream); }

		std::istream* GetStream() const { return m_stream.get(); }

		SoundType GetType() const override
        {
            return m_Type;
        }

		void SetType(SoundType type) override
        {
            m_Type = type;
        }

	private:
		String m_fileName;
		SoundType m_Type;
		std::unique_ptr<std::istream> m_stream;
	};

	
	class NullChannelInstance final : public IChannelInstance
	{
	public:
        NullChannelInstance() {}
        ~NullChannelInstance() override {}

	public:
        void Clear() override {}
	};

	
	class NullAudio final : public IAudio
	{
	public:
		explicit NullAudio();
		~NullAudio() override;

	public:
		void Create() override;

		void Destroy() override;

		void Update(const Vector3& listenerPosition, float time) override;

		SoundIndex CreateSound(const String &fileName) override;

		SoundIndex CreateStream(const String &fileName) override;

		SoundIndex CreateLoopedSound(const String &fileName) override;

		SoundIndex CreateLoopedStream(const String &fileName) override;

		SoundIndex CreateSound(const String &fileName, SoundType type) override;

		void PlaySound(SoundIndex sound, ChannelIndex *channelIndex, float priority = 1.0f) override;

		void StopSound(ChannelIndex *channelIndex) override;

		void StopAllSounds() override;

		SoundIndex FindSound(const String &fileName, SoundType type) override;

		void Set3DMinMaxDistance(ChannelIndex channelIndex, float minDistance, float maxDistance) override;

		float GetSoundLength(SoundIndex sound) override;

		ISoundInstance *GetSoundInstance(SoundIndex sound) override;

		IChannelInstance *GetChannelInstance(ChannelIndex channel) override;

	private:

		typedef std::vector<NullSoundInstance> SoundInstanceVector;
		typedef SoundInstanceVector::iterator SoundInstanceVectorItr;

		SoundIndex m_nextSoundInstanceIndex;
		Vector3	m_prevListenerPosition;
		SoundInstanceVector m_soundInstanceVector;
		NullChannelInstance m_channelArray[8];

		void IncrementNextSoundInstanceIndex();
	};
}


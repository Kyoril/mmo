
#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	enum class SoundType
	{
		Invalid,

		Sound3D,

		SoundLooped3D,

		Sound2D,

		SoundLooped2D,
	};

	typedef int32 SoundIndex;
	typedef int32 ChannelIndex;

	static constexpr SoundIndex InvalidSound = -1;
	static constexpr ChannelIndex InvalidChannel = -1;

	class ISoundInstance
	{
	public:
		virtual ~ISoundInstance() = default;

	public:
		virtual void Clear() = 0;
		virtual SoundType GetType() const = 0;
		virtual void SetType(SoundType type) = 0;
	};

	class IChannelInstance
	{
	public:
		virtual ~IChannelInstance() = default;

	public:
		virtual void Clear() = 0;
	};

	class IAudio
	{
	public:
		virtual ~IAudio() = default;

	public:
		virtual void Create() = 0;
		virtual void Destroy() = 0;
		virtual void Update(const Vector3& listenerPosition, float time) = 0;
		virtual SoundIndex CreateSound(const String& fileName) = 0;
		virtual SoundIndex CreateStream(const String& fileName) = 0;
		virtual SoundIndex CreateLoopedSound(const String& fileName) = 0;
		virtual SoundIndex CreateLoopedStream(const String &fileName) = 0;
		virtual SoundIndex CreateSound(const String& fileName, SoundType type) = 0;
		virtual void PlaySound(SoundIndex sound, ChannelIndex *channelIndex, float priority = 1.0f) = 0;
		virtual void StopSound(ChannelIndex *channelIndex) = 0;
		virtual void StopAllSounds() = 0;
		virtual SoundIndex FindSound(const String& fileName, SoundType type) = 0;
		virtual void Set3DMinMaxDistance(ChannelIndex channelIndex, float minDistance, float maxDistance) = 0;
		virtual float GetSoundLength(SoundIndex sound) = 0;
		virtual ISoundInstance* GetSoundInstance(SoundIndex sound) = 0;
		virtual IChannelInstance* GetChannelInstance(ChannelIndex channel) = 0;
	};
}


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

	/// Enumerates sound categories which can be individually adjusted in volume or muted
	/// entirely by the player. Keep the numeric values aligned with the SoundEntryCategory
	/// enum in proto_data/sounds.proto.
	enum class SoundCategory : uint8
	{
		/// Combat, footsteps, spells and other world sound effects. This is the default category.
		SoundEffects = 0,

		/// Background music.
		Music,

		/// Looping zone ambience.
		Ambience,

		/// UI interaction sounds.
		Interface,

		/// Voice lines (npc greetings, player cast error lines, quest narration).
		Voice,

		Count_
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

		virtual void SetPitch(float value) = 0;
		virtual float GetPitch() const = 0;

		virtual void SetVolume(float volume) = 0;
		virtual float GetVolume() const = 0;
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
		virtual void PlaySound(SoundIndex sound, ChannelIndex *channelIndex, float priority = 1.0f, SoundCategory category = SoundCategory::SoundEffects) = 0;
		/// Plays a 3D sound at the given world position, applying position and min/max distance
		/// before the sound becomes audible. Non-looped 3D sounds whose position is further away
		/// from the listener than maxDistance are not started at all and *channelIndex is set to
		/// InvalidChannel.
		virtual void PlaySound3D(SoundIndex sound, ChannelIndex *channelIndex, const Vector3& position, float minDistance, float maxDistance, float priority = 1.0f, SoundCategory category = SoundCategory::SoundEffects) = 0;
		virtual void StopSound(ChannelIndex *channelIndex) = 0;
		virtual void StopAllSounds() = 0;
		virtual SoundIndex FindSound(const String& fileName, SoundType type) = 0;
		virtual void Set3DMinMaxDistance(ChannelIndex channelIndex, float minDistance, float maxDistance) = 0;
		virtual void Set3DPosition(ChannelIndex channelIndex, const Vector3& position) = 0;
		virtual float GetSoundLength(SoundIndex sound) = 0;
		virtual ISoundInstance* GetSoundInstance(SoundIndex sound) = 0;
		virtual IChannelInstance* GetChannelInstance(ChannelIndex channel) = 0;

		/// Sets the master volume applied on top of all category volumes. Range [0, 1].
		virtual void SetMasterVolume(float volume) = 0;
		/// Mutes or unmutes all sound output without touching any volume values.
		virtual void SetMasterMuted(bool muted) = 0;
		/// Sets the volume of a single sound category. Range [0, 1].
		virtual void SetCategoryVolume(SoundCategory category, float volume) = 0;
		/// Mutes or unmutes a single sound category without touching any volume values.
		virtual void SetCategoryMuted(SoundCategory category, bool muted) = 0;
	};
}

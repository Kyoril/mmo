// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "animation_notify.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	void FootstepNotify::Serialize(io::Writer& writer) const
	{
		// Footstep notify has no additional data beyond base class
		// Base data (time, name) is serialized by the container
	}

	void FootstepNotify::Deserialize(io::Reader& reader)
	{
		// Footstep notify has no additional data beyond base class
		// Base data (time, name) is deserialized by the container
	}

	void PlaySoundNotify::Serialize(io::Writer& writer) const
	{
		writer << io::write_dynamic_range<uint16>(m_soundPath);
	}

	void PlaySoundNotify::Deserialize(io::Reader& reader)
	{
		reader >> io::read_container<uint16>(m_soundPath);
	}

	std::unique_ptr<AnimationNotify> AnimationNotifyFactory::Create(const AnimationNotifyType type)
	{
		switch (type)
		{
		case AnimationNotifyType::Footstep:
			return std::make_unique<FootstepNotify>();
		case AnimationNotifyType::PlaySound:
			return std::make_unique<PlaySoundNotify>();
		default:
			return nullptr;
		}
	}

	std::unique_ptr<AnimationNotify> AnimationNotifyFactory::Deserialize(io::Reader& reader)
	{
		AnimationNotifyType type;
		float time;
		String name;

		if (!(reader 
			>> io::read<uint8>(type)
			>> io::read<float>(time)
			>> io::read_container<uint16>(name)))
		{
			return nullptr;
		}

		auto notify = Create(type);
		if (notify)
		{
			notify->SetTime(time);
			notify->SetName(name);
			notify->Deserialize(reader);
		}

		return notify;
	}
}

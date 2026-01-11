// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <memory>
#include <vector>
#include <functional>

namespace io
{
	class Writer;
	class Reader;
}

namespace mmo
{

	/// Animation notification type enumeration
	enum class AnimationNotifyType : uint8
	{
		Footstep = 0,
		PlaySound = 1,
		// Add new types here as needed
	};

	/// Base class for animation notifications/events
	class AnimationNotify
	{
	public:
		virtual ~AnimationNotify() = default;

		/// Get the type of this notification
		[[nodiscard]] virtual AnimationNotifyType GetType() const = 0;

		/// Serialize notification data to writer
		virtual void Serialize(io::Writer& writer) const = 0;

		/// Deserialize notification data from reader
		virtual void Deserialize(io::Reader& reader) = 0;

		/// Get a display name for the notification
		[[nodiscard]] virtual String GetDisplayName() const = 0;

		/// Get a user-friendly type name
		[[nodiscard]] virtual String GetTypeName() const = 0;

		/// Clone this notification
		[[nodiscard]] virtual std::unique_ptr<AnimationNotify> Clone() const = 0;

		/// Get the time position of this notification in the animation in the animation
		[[nodiscard]] float GetTime() const
		{
			return m_time;
		}

		/// Set the time position of this notification in the animation
		void SetTime(const float time)
		{
			m_time = time;
		}

		/// Get the custom name of this notification
		[[nodiscard]] const String& GetName() const
		{
			return m_name;
		}

		/// Set the custom name of this notification
		void SetName(const String& name)
		{
			m_name = name;
		}

	protected:
		float m_time = 0.0f;
		String m_name;
	};

	/// Footstep notification - triggers footstep logic (sound, particles, etc.)
	class FootstepNotify final : public AnimationNotify
	{
	public:
		FootstepNotify() = default;

		[[nodiscard]] AnimationNotifyType GetType() const override
		{
			return AnimationNotifyType::Footstep;
		}

		void Serialize(io::Writer& writer) const override;
		void Deserialize(io::Reader& reader) override;

		[[nodiscard]] String GetDisplayName() const override
		{
			return m_name.empty() ? "Footstep" : m_name;
		}

		[[nodiscard]] String GetTypeName() const override
		{
			return "Footstep";
		}

		[[nodiscard]] std::unique_ptr<AnimationNotify> Clone() const override
		{
			auto clone = std::make_unique<FootstepNotify>();
			clone->m_time = m_time;
			clone->m_name = m_name;
			return clone;
		}
	};

	/// Play sound notification - triggers a sound effect at a specific time
	class PlaySoundNotify final : public AnimationNotify
	{
	public:
		PlaySoundNotify() = default;

		[[nodiscard]] AnimationNotifyType GetType() const override
		{
			return AnimationNotifyType::PlaySound;
		}

		void Serialize(io::Writer& writer) const override;
		void Deserialize(io::Reader& reader) override;

		[[nodiscard]] String GetDisplayName() const override
		{
			return m_name.empty() ? "PlaySound" : m_name;
		}

		[[nodiscard]] String GetTypeName() const override
		{
			return "PlaySound";
		}

		[[nodiscard]] std::unique_ptr<AnimationNotify> Clone() const override
		{
			auto clone = std::make_unique<PlaySoundNotify>();
			clone->m_time = m_time;
			clone->m_name = m_name;
			clone->m_soundPath = m_soundPath;
			return clone;
		}

		/// Get the sound asset path
		[[nodiscard]] const String& GetSoundPath() const
		{
			return m_soundPath;
		}

		/// Set the sound asset path
		void SetSoundPath(const String& path)
		{
			m_soundPath = path;
		}

	private:
		String m_soundPath;
	};

	/// Factory for creating animation notifications
	class AnimationNotifyFactory
	{
	public:
		/// Create a notification by type
		static std::unique_ptr<AnimationNotify> Create(AnimationNotifyType type);

		/// Deserialize a notification from reader
		static std::unique_ptr<AnimationNotify> Deserialize(io::Reader& reader);
	};
}

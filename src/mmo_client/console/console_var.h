// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "base/signal.h"

#include <string>


namespace mmo
{
	/// Enumerates possible console variable flags.
	enum ConsoleVarFlags
	{
		/// Default value.
		CVF_None			= 0,
		/// The console variable has been unregistered. We don't delete unregistered console variables as this
		/// bears the potential of access errors.
		CVF_Unregistered	= 1,
		/// Whether the console variable has been modified since it has been registered.
		CVF_Modified		= 2,
	};
	
	/// Class that represents a console variable. This class stores the values as string values but also keeps a 
	/// a int32 and float parse value cached for faster access so we avoid using std::atoi every time we try
	/// to get an int32 value for example.
	class ConsoleVar final : public NonCopyable
	{
	public:
		typedef signal<void(ConsoleVar& var, const std::string& oldValue)> ChangedSignal;

		/// A signal that is fired every time the value of this variable is changed.
		ChangedSignal Changed;

	public:
		explicit ConsoleVar(std::string name, std::string description, std::string defaultValue);

	public:
		// Make ConsoleVar movable
		ConsoleVar(ConsoleVar&& other) noexcept
			: m_floatValue(0.0f)
		{
			swap(other);
		}

		ConsoleVar& operator=(ConsoleVar&& other) noexcept
		{
			if (this != &other)
			{
				swap(other);
			}
			
			return *this;
		}

		void swap(ConsoleVar& other) noexcept
		{
			m_name = std::move(other.m_name);
			m_description = std::move(other.m_description);
			m_defaultValue = std::move(other.m_defaultValue);
			m_stringValue = std::move(other.m_stringValue);
			m_intValue = other.m_intValue;
			m_floatValue = other.m_floatValue;
			m_flags = other.m_flags;
		}

	public:
		/// @brief Determines whether a flag has been set.
		[[nodiscard]] bool HasFlag(ConsoleVarFlags flags) const 
		{
			return (m_flags & flags) != 0;
		}

		/// @brief Sets a given flag.
		void SetFlag(ConsoleVarFlags flags)
		{
			m_flags |= flags;
		}

		/// @brief Clears all flags.
		void ClearFlags()
		{
			m_flags = CVF_None;
		}

		/// @brief Removes the given flag or flags.
		void RemoveFlag(ConsoleVarFlags flags)
		{
			m_flags &= ~flags;
		}

		/// @brief Determines whether the value of this console variable has been modified.
		/// @remarks Note that this also returns true, if the value matches the default 
		/// value but has been set using the Set method instead of the Reset method.
		[[nodiscard]] bool HasBeenModified() const
		{
			return HasFlag(CVF_Modified);
		}

		/// @brief Whether this console variable is valid to use.
		[[nodiscard]] bool IsValid() const
		{
			return !HasFlag(CVF_Unregistered);
		}

		/// @brief Gets the name of this console variable.
		[[nodiscard]] const std::string& GetName() const 
		{
			return m_name;
		}

		/// @brief Gets a descriptive string.
		[[nodiscard]] const std::string& GetDescription() const
		{
			return m_description;
		}

		/// @brief Gets the default value of this variable.
		[[nodiscard]] const std::string& GetDefaultValue() const
		{
			return m_defaultValue;
		}

	private:
		/// Triggers the Changed signal if the variable has not been marked as unregistered.
		void NotifyChanged(const std::string& oldValue)
		{
			if (IsValid())
			{
				Changed(*this, oldValue);
			}
		}

	public:
		/// Sets the current value as string value. Also sets the CVF_Modified flag.
		void Set(std::string value)
		{
			const auto oldValue = std::move(m_stringValue);

			m_stringValue = std::move(value);
			m_intValue = std::atoi(m_stringValue.c_str());
			m_floatValue = static_cast<float>(std::atof(m_stringValue.c_str()));
			SetFlag(CVF_Modified);

			NotifyChanged(oldValue);
		}

		/// @brief Sets the current value as int32 value. Also sets the CVF_Modified flag.
		void Set(int32 value)
		{
			const auto oldValue = std::move(m_stringValue);

			m_stringValue = std::to_string(value);
			m_intValue = value;
			m_floatValue = static_cast<float>(value);
			SetFlag(CVF_Modified);

			NotifyChanged(oldValue);
		}

		/// @brief Sets the current value as float value. Also sets the CVF_Modified flag.
		void Set(float value)
		{
			const auto oldValue = std::move(m_stringValue);

			m_stringValue = std::to_string(value);
			m_intValue = static_cast<int32>(value);
			m_floatValue = value;
			SetFlag(CVF_Modified);

			NotifyChanged(oldValue);
		}

		void Set(bool value)
		{
			Set(value ? 1 : 0);
		}

		/// @brief Resets the current value to use the default value. Also removes the CVF_Modified flag.
		void Reset()
		{
			const std::string oldValue = std::move(m_stringValue);

			Set(m_defaultValue);
			RemoveFlag(CVF_Modified);

			NotifyChanged(oldValue);
		}

		/// @brief Gets the current string value.
		[[nodiscard]] const std::string& GetStringValue() const
		{
			return m_stringValue;
		}

		/// Gets the current int32 value.
		[[nodiscard]] int32 GetIntValue() const
		{
			return m_intValue;
		}

		/// Gets the current float value.
		[[nodiscard]] float GetFloatValue() const
		{
			return m_floatValue;
		}

		/// Gets the current bool value.
		[[nodiscard]] bool GetBoolValue() const
		{
			return GetIntValue() != 0;
		}

	private:
		/// Name of the variable.
		std::string m_name;
		/// A descriptive text used by help commands.
		std::string m_description;
		/// The default value as string.
		std::string m_defaultValue;
		/// The current value as string.
		std::string m_stringValue;
		/// A cache of the current value as int32.
		int32 m_intValue{};
		/// A cache of the current value as float.
		float m_floatValue{};
		/// Console variable flags
		uint32 m_flags{};
	};

	/// Class that manages console variables.
	class ConsoleVarMgr final : public NonCopyable
	{
	private:
		// This class can't be constructed.
		ConsoleVarMgr() = delete;

	public:
		/// Initializes the console variable manager class by registering some console
		/// commands that can be used with console variables, like setting a variable.
		static void Initialize();

		/// Counter-part of the Initialize method.
		static void Destroy();

	public:
		/// Registers a new console variable.
		static ConsoleVar* RegisterConsoleVar(const std::string& name, const std::string& description, std::string defaultValue = "");

		/// Remove a registered console variable.
		static bool UnregisterConsoleVar(const std::string& name);

		/// Finds a registered console variable if it exists.
		static ConsoleVar* FindConsoleVar(const std::string& name, bool allowUnregistered = false);
	};

}

// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "base/macros.h"		// For ASSERT
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
		explicit ConsoleVar(const std::string& name, const std::string& description, std::string defaultValue);

	public:
		// Make ConsoleVar movable
		ConsoleVar(ConsoleVar&& other)
			: m_intValue(0)
			, m_floatValue(0.0f)
			, m_flags(CVF_None)
		{
			swap(other);
		}

		ConsoleVar& operator=(ConsoleVar&& other)
		{
			if (this != &other)
			{
				swap(other);
			}
		}

		void swap(ConsoleVar& other)
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
		/// Determines whether a flag has been set.
		inline bool HasFlag(ConsoleVarFlags flags) const 
		{
			return (m_flags & flags) != 0;
		}
		/// Sets a given flag.
		inline void SetFlag(ConsoleVarFlags flags)
		{
			m_flags |= flags;
		}
		/// Clears all flags.
		inline void ClearFlags()
		{
			m_flags = CVF_None;
		}
		/// Removes the given flag or flags.
		inline void RemoveFlag(ConsoleVarFlags flags)
		{
			m_flags &= ~flags;
		}
		/// Determines whether the value of this console variable has been modified.
		/// @remarks Note that this also returns true, if the value matches the default 
		/// value but has been set using the Set method instead of the Reset method.
		inline bool HasBeenModified() const
		{
			return HasFlag(CVF_Modified);
		}
		/// Whether this console variable is valid to use.
		inline bool IsValid() const
		{
			return !HasFlag(CVF_Unregistered);
		}
		/// Gets the name of this console variable.
		inline const std::string& GetName() const 
		{
			return m_name;
		}
		/// Gets a descriptive string.
		inline const std::string& GetDescription() const
		{
			return m_description;
		}
		/// Gets the default value of this variable.
		inline const std::string& GetDefaultValue() const
		{
			return m_defaultValue;
		}

	private:
		/// Triggers the Changed signal if the variable has not been marked as unregistered.
		inline void NotifyChanged(const std::string& oldValue)
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
			const std::string oldValue = std::move(m_stringValue);

			m_stringValue = std::move(value);
			m_intValue = std::atoi(m_stringValue.c_str());
			m_floatValue = std::atof(m_stringValue.c_str());
			SetFlag(CVF_Modified);

			NotifyChanged(oldValue);
		}
		/// Sets the current value as int32 value. Also sets the CVF_Modified flag.
		void Set(int32 value)
		{
			const std::string oldValue = std::move(m_stringValue);

			m_stringValue = std::to_string(value);
			m_intValue = value;
			m_floatValue = static_cast<float>(value);
			SetFlag(CVF_Modified);

			NotifyChanged(oldValue);
		}
		/// Sets the current value as float value. Also sets the CVF_Modified flag.
		void Set(float value)
		{
			const std::string oldValue = std::move(m_stringValue);

			m_stringValue = std::to_string(value);
			m_intValue = static_cast<int32>(value);
			m_floatValue = value;
			SetFlag(CVF_Modified);

			NotifyChanged(oldValue);
		}
		inline void Set(bool value)
		{
			Set(value ? 1 : 0);
		}
		/// Resets the current value to use the default value. Also removes the CVF_Modified flag.
		void Reset()
		{
			const std::string oldValue = std::move(m_stringValue);

			Set(m_defaultValue);
			RemoveFlag(CVF_Modified);

			NotifyChanged(oldValue);
		}
		/// Gets the current string value.
		inline const std::string& GetStringValue() const
		{
			return m_stringValue;
		}
		/// Gets the current int32 value.
		inline int32 GetIntValue() const
		{
			return m_intValue;
		}
		/// Gets the current float value.
		inline float GetFloatValue() const
		{
			return m_floatValue;
		}
		/// Gets the current bool value.
		inline bool GetBoolValue() const
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
		int32 m_intValue;
		/// A cache of the current value as float.
		float m_floatValue;
		/// Console variable flags
		uint32 m_flags;
	};

	/// Class that manages console variables.
	class ConsoleVarMgr final : public NonCopyable
	{
	private:
		/// Make this class non-instancable
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
		/// Unregisters a registered console variable.
		static bool UnregisterConsoleVar(const std::string& name);
		/// Finds a registered console variable if it exists.
		static ConsoleVar* FindConsoleVar(const std::string& name, bool allowUnregistered = false);
	};

}

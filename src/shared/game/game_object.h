// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <bitset>
#include <array>
#include <vector>
#include <memory>

#include "base/typedefs.h"
#include "base/signal.h"

#include "math/vector3.h"
#include "math/angle.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	// Enumerates available object type ids.
	enum class ObjectTypeId
	{
		/// Default type. Generic object.
		Object = 0,
		/// The object is an item.
		Item = 1,
		/// An item container object.
		Container = 2,
		/// A living unit with health etc.
		Unit = 3,
		/// A player character, which is also a unit.
		Player = 4,
		/// A dynamic object which is temporarily spawned.
		DynamicObject = 5,
		/// A player corpse.
		Corpse = 6
	};

	enum class GuidType
	{
		Player = 0,
		Object = 1,
		Transport = 2,
		Unit = 3,
		Pet = 4,
		Item = 5
	};

	/// Defines object field visibility modifiers.
	enum class FieldVisibilityModifier : uint8
	{
		/// The field is ownly visible to the owning client.
		Private,
		/// The field is visible for everyone.
		Public,
	};
	
	/// A map for managing flexible fields.
	template<class TFieldBase, typename std::enable_if<std::is_unsigned<TFieldBase>::value>::type* = nullptr>
	class FieldMap
	{
	public:
		/// Type of a field index. Also determines the maximum amount of fields.
		typedef uint8 FieldIndexType;

		static constexpr size_t MaxFieldCount = 1 << std::numeric_limits<FieldIndexType>::digits;
		
	public:
		/// Initializes the field map with a maximum number of fields, where 0 < numFields <= MaxFieldCount.
		/// Must not be called twice.
		void Initialize(size_t numFields)
		{
			// Should not have been initialized already
			ASSERT(m_data.size() == 0);

			// Field count must be valid
			ASSERT(numFields > 0);
			ASSERT(numFields <= MaxFieldCount);
			
			m_changes.reset();
			m_data.resize(numFields, 0);
		}
	
	public:
		/// Gets the value of a specific field.
		/// @param index The field index to get.
		template<class T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
		[[nodiscard]] T GetFieldValue(FieldIndexType index) const
		{
			ASSERT(index <= m_data.size());
			ASSERT(index * sizeof(TFieldBase) + sizeof(T) <= m_data.size() * sizeof(TFieldBase));
			
			return *reinterpret_cast<const T*>(&m_data[index]);
		}

		/// Sets the value of a specific field. Marks all modified fields as changed.
		/// @param index The field index to set.
		/// @param value The new value to set.
		template<class T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
		void SetFieldValue(FieldIndexType index, T value)
		{
			ASSERT(index <= m_data.size());
			ASSERT(index * sizeof(TFieldBase) + sizeof(T) <= m_data.size() * sizeof(TFieldBase));

			T* data = reinterpret_cast<T*>(&m_data[index]);
			*data = value;
						
			// Mark fields as changed
			for (uint32 i = 0; i < sizeof(T) / sizeof(uint32); ++i)
			{
				MarkAsChanged(index + i);
			}
		}

		/// Determines whether the given field is marked as changed.
		[[nodiscard]] bool IsFieldMarkedAsChanged(FieldIndexType index) const { return m_changes[index]; }
		
		/// Marks all fields as changed.
		void MarkAllAsChanged() { m_changes.set(); }

		/// Marks a specific field as changed.
		void MarkAsChanged(FieldIndexType index) { m_changes.set(index); }
		
		/// Marks all fields as unchanged.
		void MarkAsUnchanged() { m_changes.reset(); }

	public:
		/// Serializes the whole field map, regardless of change flags.
		io::Writer& SerializeComplete(io::Writer& w) const
		{
			return w
				<< io::write_range(m_data);
		}

		/// Serializes only fields that have been changed.
		io::Writer& SerializeChanges(io::Writer& w) const
		{
			for (size_t i = 0; i < m_data.size(); i += 8)
			{
				uint8 flag = 0;

				for (size_t j = 0; j < 8; ++j)
				{
					if (i + j < m_data.size())
					{
						if (m_changes[i + j]) flag |= 1 << j;
					}
				}

				io::write<uint8>(flag);
			}

			for (size_t i = 0; i < m_data.size(); ++i)
			{
				if (m_changes[i])
				{
					io::write<TFieldBase>(m_data[i]);
				}
			}
			
			return w;
		}

		/// 
		io::Reader& DeserializeComplete(io::Reader& r) const
		{
			return r
				>> io::read_range(m_data);
		}

		/// 
		io::Reader& DeserializeChanges(io::Reader& r) const
		{
			return r
				>> io::read_range(m_data);
		}
	
	private:
		std::bitset<MaxFieldCount> m_changes;
		std::vector<TFieldBase> m_data;
	};

	typedef FieldMap<uint32> ObjectFieldMap;
	
	// Enumerates available object fields
	namespace object_fields
	{
		enum ObjectFields
		{
			Guid,
			Type,
			Entry,
			Scale,

			ObjectFieldCount,
		};

		enum UnitFields
		{
			Level = ObjectFieldCount,
			MaxHealth,
			Health,
			TargetUnit,

			UnitFieldCount,
		};

		enum PlayerFields
		{
			Placeholder = UnitFieldCount,
			
			PlayerFieldCount
		};
	}
	
	/// This is the base class of server side object, spawned on the world server.
	class GameObject : public std::enable_shared_from_this<GameObject>
	{
	public:
		explicit GameObject(uint64 guid);
		virtual ~GameObject();

	protected:
		virtual void PrepareFieldMap()
		{
			m_fields.Initialize(object_fields::ObjectFieldCount);
		}
		
	public:
		/// Gets the objects globally unique identifier value.
		uint64 GetGuid() const { return m_fields.GetFieldValue<uint64>(object_fields::Guid); }
	
	protected:
		ObjectFieldMap m_fields;
		Vector3 m_position;
		Angle m_rotation;
	};
}

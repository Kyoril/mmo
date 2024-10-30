// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <bitset>
#include <type_traits>
#include <vector>

#include "base/macros.h"
#include "base/typedefs.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	/// A map for managing flexible fields.
	template<class TFieldBase, std::enable_if_t<std::is_unsigned_v<TFieldBase>>* = nullptr>
	class FieldMap final
	{
	public:
		/// Type of a field index. Also determines the maximum amount of fields.
		typedef uint16 FieldIndexType;

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
		template<class T>
		[[nodiscard]] T GetFieldValue(FieldIndexType index) const
		{
			ASSERT(index <= m_data.size());
			ASSERT(index * sizeof(TFieldBase) + sizeof(T) <= m_data.size() * sizeof(TFieldBase));
			
			return *reinterpret_cast<const T*>(&m_data[index]);
		}

		/// Sets the value of a specific field. Marks all modified fields as changed.
		/// @param index The field index to set.
		/// @param value The new value to set.
		template<class T>
		bool SetFieldValue(FieldIndexType index, T value)
		{
			ASSERT(index <= m_data.size());
			ASSERT(index * sizeof(TFieldBase) + sizeof(T) <= m_data.size() * sizeof(TFieldBase));

			T* data = reinterpret_cast<T*>(&m_data[index]);

			// Don't need to change anything if value did not change
			if (*data == value)
			{
				return false;
			}

			*data = value;
						
			// Mark fields as changed
			for (uint32 i = 0; i < sizeof(T) / sizeof(TFieldBase); ++i)
			{
				MarkAsChanged(index + i);
			}

			return true;
		}

		/// Determines whether the given field is marked as changed.
		[[nodiscard]] bool IsFieldMarkedAsChanged(const FieldIndexType index) const { return m_changes[index]; }

		int32 GetFirstChangedField() const
		{
			for (uint32 i = 0; i < m_data.size(); ++i)
			{
				if (m_changes[i])
				{
					return i;
				}
			}

			return -1;
		}

		int32 GetLastChangedField() const
		{
			for (int32 i = m_data.size() - 1; i >= 0; --i)
			{
				if (m_changes[i])
				{
					return i;
				}
			}

			return -1;
		}

		/// Marks all fields as changed.
		void MarkAllAsChanged() { m_changes.set(); }

		/// Marks all fields as changed.
		void MarkAllAsUnchanged() { m_changes.reset(); }

		/// Marks a specific field as changed.
		void MarkAsChanged(const FieldIndexType index) { m_changes.set(index); }
		
		/// Marks all fields as unchanged.
		void MarkAsUnchanged() { m_changes.reset(); }

		bool HasChanges() const { return m_changes.any(); }

	public:
		/// Serializes the whole field map, regardless of change flags.
		io::Writer& SerializeComplete(io::Writer& w) const
		{
			return w
				<< io::write_range(m_data);
		}

		/// @brief Serializes only fields that have been changed.
		/// @param w The writer to use.
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

				w << io::write<uint8>(flag);
			}

			for (size_t i = 0; i < m_data.size(); ++i)
			{
				if (m_changes[i])
				{
					w << io::write<TFieldBase>(m_data[i]);
				}
			}
			
			return w;
		}

		/// @brief Deserializes the whole field map, expecting every single field value.
		///	@param r The reader to use.
		io::Reader& DeserializeComplete(io::Reader& r)
		{
			m_changes.reset();
			return r
				>> io::read_range(m_data);
		}

		/// @brief Deserializes the field map while expecting only changed field values.
		///	@param r The reader to use.
		io::Reader& DeserializeChanges(io::Reader& r)
		{
			m_changes.reset();

			for (size_t i = 0; i < m_data.size(); i += 8)
			{
				uint8 flag;
				if (!(r >> io::read<uint8>(flag)))
				{
					return r;
				}

				for (size_t j = 0; j < 8; ++j)
				{
					if (flag & (1 << j))
					{
						m_changes.set(j + i);
					}
				}
			}
			
			for (size_t i = 0; i < m_data.size(); ++i)
			{
				if (IsFieldMarkedAsChanged(i))
				{
					if (!(r >> io::read<TFieldBase>(m_data[i])))
					{
						break;
					}
				}
			}
			
			return r;
		}
	
	private:
		std::bitset<MaxFieldCount> m_changes{};
		std::vector<TFieldBase> m_data{};
	};

}

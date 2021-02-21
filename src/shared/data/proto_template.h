
#pragma once

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"

namespace mmo
{
	/// Represents a manager class for templates which are loaded using Googles ProtoBuf library.
	/// It offers the following features: Load List, Save List, Add Entry, Get Entry By Id, Remove Entry
	template<class T1, class T2>
	struct TemplateManager
	{
	public:

		typedef T2 EntryType;

	public:

		String hashString;

	private:

		T1 m_data;
		std::map<uint32, T2 *> m_templatesById;

	public:

		/// Called when this list should be loaded.
		/// @param stream The stream to load data from.
		bool load(std::istream &stream)
		{
			// Set byte limit to 128MB
			const int byteLimit = 1024 * 1024 * 128;

			google::protobuf::io::IstreamInputStream zeroCopyStream(&stream);
			google::protobuf::io::CodedInputStream decoder(&zeroCopyStream);
			decoder.SetTotalBytesLimit(byteLimit);

			if (!(m_data.ParseFromCodedStream(&decoder) && decoder.ConsumedEntireMessage()))
			{
				return false;
			}

			// Iterate through all data entries and store ids for quick id lookup
			for (int i = 0; i < m_data.entry_size(); ++i)
			{
				T2 *entry = m_data.mutable_entry(i);
				m_templatesById[entry->base().id()] = entry;
			}

			return true;
		}

		/// Called when this list should be saved.
		/// @param stream The stream to write data to.
		bool save(std::ostream &stream) const
		{
			if (!m_data.SerializeToOstream(&stream))
			{
				return false;
			}

			return true;
		}

		void clear()
		{
			m_templatesById.clear();
			m_data.clear_entry();
		}

		/// Gets a list of all template entries in this list.
		const T1 &getTemplates() const
		{
			return m_data;
		}

		T1 &getTemplates()
		{
			return m_data;
		}

		/// Adds a new entry using the specified id.
		T2 *add(uint32 id)
		{
			// Check id for existance
			if (getById(id) != nullptr) {
				return nullptr;
			}

			// Add new entry
			auto *added = m_data.add_entry();
			added->set_id(id);

			// Store in array and return
			m_templatesById[id] = added;
			return added;
		}

		/// Removes an existing entry from the data set.
		void remove(uint32 id)
		{
			// Remove entry from id list
			auto it = m_templatesById.find(id);
			if (it != m_templatesById.end())
			{
				m_templatesById.erase(it);
			}

			// Remove entry from m_data
			for (int i = 0; i < m_data.entry_size();)
			{
				if (m_data.entry(i).id() == id)
				{
					m_data.mutable_entry()->erase(
						m_data.mutable_entry()->begin() + i);
				}
				else
				{
					++i;
				}
			}
		}

		/// Retrieves a pointer to an object by its id.
		const T2 *getById(uint32 id) const
		{
			const auto it = m_templatesById.find(id);
			if (it == m_templatesById.end()) {
				return nullptr;
			}

			return it->second;
		}

		T2 *getById(uint32 id)
		{
			const auto it = m_templatesById.find(id);
			if (it == m_templatesById.end()) {
				return nullptr;
			}

			return it->second;
		}
	};
}

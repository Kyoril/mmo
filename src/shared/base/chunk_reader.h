// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "binary_io/reader.h"

#include <functional>
#include <map>
#include <set>


namespace mmo
{
	/// @brief A default chunk handler which will simply skip the entire chunk content.
	struct SkipChunkHandler final
	{
		bool operator()(io::Reader& reader, const uint32 chunkHeader, const uint32 size) const
		{
			// Nothing to do here, just skip the chunk
			return reader >> io::skip(size);
		}
	};

	/// @brief Base class for reading a chunked file format.
	class ChunkReader : public NonCopyable
	{
	public:
		/// @brief Callback to execute whenever a chunk has been found.
		typedef std::function<bool(io::Reader& reader, uint32 chunkHeader, uint32 size)> ChunkReadCallback;

	public:
		/// @brief Default constructor initializes the chunk reader.
		explicit ChunkReader(bool ignoreUnhandledChunks = false);

		/// @brief Virtual destructor
		virtual ~ChunkReader() override = default;

	public:
		/// @brief Reads all chunks from a given reader.
		/// @param reader The reader to read from. It is expected to have the right state, for example the beginning of a file.
		/// @return true on success, false if an error occurred.
		bool Read(io::Reader& reader);

		/// @brief Determines whether unhandled chunks will be ignored or make the Read call fail.
		[[nodiscard]] bool DoesIgnoreUnhandledChunks() const noexcept { return m_ignoreUnhandledChunks; }

	protected:
		/// @brief Implement for custom IsValid check after deserialization has been done.
		/// @return true if deserialization has been valid, false otherwise.
		virtual bool IsValid() const noexcept { return m_requiredChunkHandlers.empty(); }

		virtual bool OnReadFinished() noexcept { return true; }

	public:
		template<typename TInstance, typename TClass, typename... TArgs>
		void AddChunkHandler(const uint32 chunkHeader, const bool required, TInstance& instance, bool(TClass::*callback)(TArgs... args))
		{
			m_chunkHandlers[chunkHeader] = [&instance, callback](TArgs... args) {
				return (instance.*callback)(TArgs(args)...);
			};
			
			if (required)
			{
				m_requiredChunkHandlers.insert(chunkHeader);
			}
			else
			{
				m_requiredChunkHandlers.erase(chunkHeader);
			}
		}

		/// @brief Adds a new chunk handler callback for a given chunk.
		/// @param chunkHeader Header identifier of the chunk to skip.
		/// @param required If true, this chunk handler has to be called.
		/// @param callback Callback to execute.
		void AddChunkHandler(const uint32 chunkHeader, const bool required, ChunkReadCallback callback)
		{
			m_chunkHandlers[chunkHeader] = std::move(callback);

			if (required)
			{
				m_requiredChunkHandlers.insert(chunkHeader);
			}
			else
			{
				m_requiredChunkHandlers.erase(chunkHeader);
			}
		}

		void RemoveChunkHandler(const uint32 chunkHeader)
		{
			if (m_chunkHandlers.contains(chunkHeader))
			{
				m_chunkHandlers.erase(chunkHeader);
			}
		}

	protected:
		/// @brief Whether unhandled chunks should be ignored or make the Read call fail.
		bool m_ignoreUnhandledChunks { false };
		/// @brief List of registered chunk handlers.
		std::map<uint32, ChunkReadCallback> m_chunkHandlers;

		std::set<uint32> m_requiredChunkHandlers;
	};
}

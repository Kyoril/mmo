// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <string>
#include <vector>

namespace mmo
{
	namespace game
	{
		/// What a bug report is about. Goes on the wire, only ever append.
		namespace bug_report_subject
		{
			enum Type : uint8
			{
				Generic = 0,
				Item,
				Spell,
				Creature,
				Quest,
				Aura,
				Object,

				Count_
			};
		}

		typedef bug_report_subject::Type BugReportSubject;

		/// Lower case name of a subject type, as used by the client script API and the bug API.
		inline const char* GetBugReportSubjectName(const uint8 subject)
		{
			switch (subject)
			{
			case bug_report_subject::Item: return "item";
			case bug_report_subject::Spell: return "spell";
			case bug_report_subject::Creature: return "creature";
			case bug_report_subject::Quest: return "quest";
			case bug_report_subject::Aura: return "aura";
			case bug_report_subject::Object: return "object";
			default: return "generic";
			}
		}

		/// Answer to a bug report. Goes on the wire, only ever append.
		namespace bug_report_result
		{
			enum Type : uint8
			{
				/// The report was queued for upload.
				Accepted = 0,
				/// The character filed too many reports recently.
				RateLimited,
				/// The packet or its decompressed client data exceeded the size limits.
				TooLarge,
				/// Bug reporting is unavailable (disabled, unhealthy, or not in a world).
				Disabled,
				/// The report could not be parsed.
				Invalid,

				Count_
			};
		}

		typedef bug_report_result::Type BugReportResult;

		/// Limits shared by client, realm and world.
		namespace bug_report_limits
		{
			/// Maximum comment length in bytes (the UI limits to 1000 characters; UTF-8 needs more).
			constexpr uint32 MaxCommentBytes = 4000;
			/// Maximum subject name length in bytes.
			constexpr uint32 MaxSubjectNameBytes = 255;
			/// Maximum size of the compressed client JSON.
			constexpr uint32 MaxCompressedBytes = 64 * 1024;
			/// Maximum size of the client JSON after decompression.
			constexpr uint32 MaxUncompressedBytes = 256 * 1024;
		}

		/// The part of a bug report the client sends. The realm forwards it unchanged to the world
		/// node, behind the identity it vouches for.
		struct BugReportPayload
		{
			uint8 subjectType = bug_report_subject::Generic;
			uint32 subjectId = 0;
			uint64 subjectGuid = 0;
			std::string subjectName;
			std::string comment;
			/// Size of clientData once decompressed, as claimed by the client (verified on inflate).
			uint32 uncompressedSize = 0;
			/// zlib compressed client JSON (metadata and log tail).
			std::vector<char> clientData;
		};

		inline io::Writer& operator<<(io::Writer& writer, const BugReportPayload& payload)
		{
			return writer
				<< io::write<uint8>(payload.subjectType)
				<< io::write<uint32>(payload.subjectId)
				<< io::write<uint64>(payload.subjectGuid)
				<< io::write_dynamic_range<uint8>(payload.subjectName)
				<< io::write_dynamic_range<uint16>(payload.comment)
				<< io::write<uint32>(payload.uncompressedSize)
				<< io::write_dynamic_range<uint32>(payload.clientData);
		}

		/// Reads a payload and enforces the size limits: an oversized field fails the read
		/// rather than being silently truncated.
		inline io::Reader& operator>>(io::Reader& reader, BugReportPayload& payload)
		{
			uint8 nameLength = 0;
			uint16 commentLength = 0;
			uint32 dataLength = 0;

			if (!(reader
				>> io::read<uint8>(payload.subjectType)
				>> io::read<uint32>(payload.subjectId)
				>> io::read<uint64>(payload.subjectGuid)
				>> io::read<uint8>(nameLength)))
			{
				return reader;
			}

			payload.subjectName.resize(nameLength);
			if (nameLength > 0 && !(reader >> io::read_range(payload.subjectName.begin(), payload.subjectName.end())))
			{
				return reader;
			}

			if (!(reader >> io::read<uint16>(commentLength)))
			{
				return reader;
			}
			if (commentLength > bug_report_limits::MaxCommentBytes)
			{
				reader.setFailure();
				return reader;
			}

			payload.comment.resize(commentLength);
			if (commentLength > 0 && !(reader >> io::read_range(payload.comment.begin(), payload.comment.end())))
			{
				return reader;
			}

			if (!(reader >> io::read<uint32>(payload.uncompressedSize) >> io::read<uint32>(dataLength)))
			{
				return reader;
			}
			if (dataLength > bug_report_limits::MaxCompressedBytes || payload.uncompressedSize > bug_report_limits::MaxUncompressedBytes)
			{
				reader.setFailure();
				return reader;
			}

			payload.clientData.resize(dataLength);
			if (dataLength > 0)
			{
				reader >> io::read_range(payload.clientData.begin(), payload.clientData.end());
			}

			return reader;
		}
	}
}

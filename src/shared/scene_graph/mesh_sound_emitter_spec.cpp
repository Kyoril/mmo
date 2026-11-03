// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_sound_emitter_spec.h"

#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace mmo
{
	namespace
	{
		String Trim(const String& text)
		{
			const size_t first = text.find_first_not_of(" \t\r\n");
			if (first == String::npos)
			{
				return {};
			}

			const size_t last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

		std::vector<String> Split(const String& text, const char separator)
		{
			std::vector<String> parts;
			size_t start = 0;
			while (true)
			{
				const size_t end = text.find(separator, start);
				parts.push_back(Trim(text.substr(start, end == String::npos ? String::npos : end - start)));
				if (end == String::npos)
				{
					break;
				}
				start = end + 1;
			}
			return parts;
		}

		bool ParseFloat(const String& text, float& out)
		{
			const char* begin = text.data();
			const char* end = begin + text.size();
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
			const auto [ptr, ec] = std::from_chars(begin, end, out);
			return ec == std::errc() && ptr == end && std::isfinite(out);
#else
			// Apple's libc++ has no floating-point from_chars; strtof needs a terminated string,
			// which String::c_str() provides.
			if (begin == end)
			{
				return false;
			}

			char* parsedEnd = nullptr;
			errno = 0;
			out = std::strtof(text.c_str(), &parsedEnd);
			return errno == 0 && parsedEnd == text.c_str() + text.size() && std::isfinite(out);
#endif
		}

		bool ParseUInt(const String& text, uint32& out)
		{
			const char* begin = text.data();
			const char* end = begin + text.size();
			const auto [ptr, ec] = std::from_chars(begin, end, out);
			return ec == std::errc() && ptr == end;
		}
	}

	bool ParseMeshSoundEmitterSpec(const String& spec, std::vector<MeshSoundEmitter>& out_emitters, String& out_error)
	{
		std::vector<MeshSoundEmitter> emitters;
		if (Trim(spec).empty())
		{
			out_emitters.clear();
			return true;
		}

		for (const String& entry : Split(spec, ';'))
		{
			if (entry.empty())
			{
				continue;
			}

			const std::vector<String> fields = Split(entry, ':');
			if (fields.size() < 2 || fields.size() > 4)
			{
				out_error = "'" + entry + "': expected <id>:<x>,<y>,<z>[:<volume>[:<maxDistance>]]";
				return false;
			}

			MeshSoundEmitter emitter;
			if (!ParseUInt(fields[0], emitter.soundEntryId) || emitter.soundEntryId == 0)
			{
				out_error = "'" + entry + "': sound entry id must be a positive integer";
				return false;
			}

			const std::vector<String> coordinates = Split(fields[1], ',');
			if (coordinates.size() != 3 || !ParseFloat(coordinates[0], emitter.offset.x) || !ParseFloat(coordinates[1], emitter.offset.y) || !ParseFloat(coordinates[2], emitter.offset.z))
			{
				out_error = "'" + entry + "': offset must be three numbers <x>,<y>,<z>";
				return false;
			}

			if (fields.size() >= 3 && (!ParseFloat(fields[2], emitter.volume) || emitter.volume < 0.0f))
			{
				out_error = "'" + entry + "': volume must be a number >= 0";
				return false;
			}

			if (fields.size() == 4 && (!ParseFloat(fields[3], emitter.maxDistance) || emitter.maxDistance < 0.0f))
			{
				out_error = "'" + entry + "': max distance must be a number >= 0";
				return false;
			}

			emitters.push_back(emitter);
		}

		out_emitters = std::move(emitters);
		return true;
	}

	String FormatMeshSoundEmitterSpec(const std::vector<MeshSoundEmitter>& emitters)
	{
		std::ostringstream stream;
		for (size_t i = 0; i < emitters.size(); ++i)
		{
			const MeshSoundEmitter& emitter = emitters[i];
			if (i > 0)
			{
				stream << ';';
			}

			stream << emitter.soundEntryId << ':' << emitter.offset.x << ',' << emitter.offset.y << ',' << emitter.offset.z
				<< ':' << emitter.volume << ':' << emitter.maxDistance;
		}
		return stream.str();
	}
}

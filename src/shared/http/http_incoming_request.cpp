// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "http_incoming_request.h"

#include "base/constants.h"
#include "base64/base64.h"
#include "log/default_log_levels.h"
#include "base/macros.h"
#include "base/utilities.h"

namespace mmo
{
	namespace net
	{
		namespace http
		{
			IncomingRequest::IncomingRequest()
				: m_type(Unknown_)
			{
			}

			IncomingRequest::Type IncomingRequest::getType() const
			{
				return m_type;
			}

			const String &IncomingRequest::getPath() const
			{
				return m_path;
			}

			const String &IncomingRequest::getHost() const
			{
				return m_host;
			}

			const String &IncomingRequest::getPostData() const
			{
				return m_postData;
			}

			const IncomingRequest::Headers &IncomingRequest::getHeaders() const
			{
				return m_headers;
			}


			static bool isWhitespace(char c)
			{
				return (c <= ' ');
			}

			static void skipWhitespace(const char *&pos, const char *end)
			{
				while (
				    pos != end &&
				    isWhitespace(*pos))
				{
					++pos;
				}
			}

			static void parseThing(const char *&pos, const char *end, String &dest)
			{
				ASSERT(dest.empty());

				while (pos != end &&
				        !isWhitespace(*pos))
				{
					dest += *pos;
					++pos;
				}
			}

			static bool skipCharacter(const char *&pos, const char *end, char c)
			{
				if (pos != end &&
				        *pos == c)
				{
					++pos;
					return true;
				}

				return false;
			}

			static bool skipEndOfLine(const char *&pos, const char *end)
			{
				const char *savePos = pos;

				if (
				    skipCharacter(savePos, end, '\r') &&
				    skipCharacter(savePos, end, '\n'))
				{
					pos = savePos;
					return true;
				}

				return false;
			}

			static void readUntil(const char *&pos, const char *end, String &value, char until)
			{
				ASSERT(value.empty());

				while (pos != end &&
				        *pos != until)
				{
					value += *pos;
					++pos;
				}
			}

			ReceiveState IncomingRequest::Start(IncomingRequest &packet, io::MemorySource &source)
			{
				const char *const begin = source.getBegin();
				const char *const end = source.getEnd();
				const char *pos = begin;

				//request line
				{
					String method;
					parseThing(pos, end, method);

					if (method.empty())
					{
						return receive_state::Incomplete;
					}
					else if (method == "HEAD")
					{
						packet.m_type = Head;
					}
					else if (method == "GET")
					{
						packet.m_type = Get;
					}
					else if (method == "POST")
					{
						packet.m_type = Post;
					}
					else if (method == "OPTIONS")
					{
						packet.m_type = Options;
					}
					else
					{
						return receive_state::Malformed;
					}
				}

				skipWhitespace(pos, end);
				parseThing(pos, end, packet.m_path);
				if (packet.m_path.empty())
				{
					return receive_state::Incomplete;
				}

				// Parse path arguments
				auto argIt = packet.m_path.find('?');
				if (argIt != packet.m_path.npos)
				{
					// Split arguments from the path
					String argumentStrings = packet.m_path.substr(argIt + 1);

					// Split arguments
					std::string::size_type lastPos = 0;
					std::vector<String> arguments;
					for (std::string::size_type i = argumentStrings.find(' ', lastPos); i != std::string::npos; i = argumentStrings.find(' ', lastPos))
					{
						arguments.push_back(argumentStrings.substr(lastPos, i));
						lastPos = i + 1;
					}

					// Fill in argument map
					for (const auto& arg : arguments)
					{
						auto delimiterPos = arg.find('=');
						if (delimiterPos != arg.npos)
						{
							const String argName = arg.substr(0, delimiterPos);
							const String argValue = arg.substr(delimiterPos + 1);
							packet.m_pathArguments[argName] = argValue;
						}
					}

					// Fix path by removing the arguments
					packet.m_path.erase(argIt);
				}

				skipWhitespace(pos, end);

				{
					String version;
					parseThing(pos, end, version);
					if (version.empty())
					{
						return receive_state::Incomplete;
					}
				}

				if (!skipEndOfLine(pos, end))
				{
					return receive_state::Malformed;
				}

				// We need at least another end of line
				if (pos == end)
				{
					return receive_state::Incomplete;
				}

				//headers
				packet.m_headers.clear();
				size_t estimatedContentLength = 0;
				while (!skipEndOfLine(pos, end))
				{
					String name;

					readUntil(pos, end, name, ':');
					if (name.empty())
					{
						break;
					}

					if (!skipCharacter(pos, end, ':'))
					{
						WLOG("Malformed for header " << name << ": " << pos);
						return receive_state::Malformed;
					}

					skipWhitespace(pos, end);

					String value;
					readUntil(pos, end, value, '\r');

					if (!skipEndOfLine(pos, end))
					{
						WLOG("Malformed for header " << name << ": " << value << " - " << pos);
						return receive_state::Malformed;
					}

					if (_stricmp(name.c_str(), "Content-Length") == 0)
					{
						estimatedContentLength = atoi(value.c_str());
					}

					packet.m_headers.insert(std::make_pair(name, value));
				}

				if (estimatedContentLength > 0)
				{
					if (end <= pos ||
					        size_t(end - pos) < estimatedContentLength)
					{
						return receive_state::Incomplete;
					}
				}
				else if (estimatedContentLength == 0)
				{
					source.skip(pos - begin);
					return receive_state::Complete;
				}

				parseThing(pos, end, packet.m_postData);
				source.skip(pos - begin);

				packet.m_postFormArguments.clear();

				std::istringstream stream(packet.m_postData);
				std::string arg;
				while (std::getline(stream, arg, '&'))
				{
					auto delimiterPos = arg.find('=');
					if (delimiterPos == arg.npos)
					{
						packet.m_postFormArguments.clear();
						break;
					}

					String argName = arg.substr(0, delimiterPos);
					String argValue = arg.substr(delimiterPos + 1);
					packet.m_postFormArguments[argName] = UrlDecode(argValue);
				}

				return receive_state::Complete;
			}

			namespace
			{
				std::pair<std::string, std::string> parseHTTPAuthorization(
				    const std::string &encoded)
				{
					const std::pair<std::string, std::string> errorResult;

					//the beginning is expected to be "Basic "
					static const std::string Begin = "Basic ";
					if (encoded.size() < Begin.size())
					{
						return errorResult;
					}

					if (!std::equal(Begin.begin(), Begin.end(), encoded.begin()))
					{
						return errorResult;
					}

					const auto base64Begin = encoded.begin() + Begin.size();
					const auto base64End = encoded.end();

					//TODO: save memory allocation
					const auto concatenated = base64_decode(std::string(base64Begin, base64End));
					const auto colon = std::find(begin(concatenated), end(concatenated), ':');
					const auto afterColon = (colon == end(concatenated) ? colon : (colon + 1));

					return std::make_pair(
					           std::string(begin(concatenated), colon),
					           std::string(afterColon, end(concatenated)));
				}
			}

			bool authorize(
			    const IncomingRequest &request,
			    const std::function<bool (const std::string &, const std::string &)> &checkCredentials)
			{
				const auto &headers = request.getHeaders();
				const auto authorization = headers.find("Authorization");
				if (authorization == headers.end())
				{
					return false;
				}

				const auto &encodedCredentials = authorization->second;
				const auto credentials = parseHTTPAuthorization(encodedCredentials);

				const auto &name = credentials.first;
				const auto &password = credentials.second;

				return checkCredentials(name, password);
			}
		}
	}
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "http_outgoing_answer.h"
#include "base/constants.h"

#include <sstream>

namespace mmo
{
	namespace net
	{
		namespace http
		{
			OutgoingAnswer::OutgoingAnswer(io::ISink &dest)
				: m_dest(dest)
				, m_status(OK)
			{
			}

			void OutgoingAnswer::setStatus(Status status)
			{
				m_status = status;
			}

			void OutgoingAnswer::addHeader(const String &name, const String &value)
			{
				m_additionalHeaders += name;
				m_additionalHeaders += ": ";
				m_additionalHeaders += value;
				m_additionalHeaders += "\r\n";
			}

			void OutgoingAnswer::finish()
			{
				writeHeaders();
				m_dest.Write("\r\n", 2);
			}

			void OutgoingAnswer::finishWithContent(const String &type, const char *content, std::size_t size)
			{
				writeHeaders();

				std::ostringstream sstr;

				sstr << "Content-Type: " << type << "\r\n";
				sstr << "Content-Length: " << size << "\r\n";
				sstr << "\r\n";

				sstr.write(content, static_cast<std::streamsize>(size));

				const std::string str = sstr.str();
				m_dest.Write(str.data(), str.size());
			}


			void OutgoingAnswer::makeAnswer(io::ISink &dest, const String &type, const char *content, std::size_t size)
			{
				OutgoingAnswer packet(dest);
				packet.setStatus(OK);
				packet.finishWithContent(type, content, size);
			}

			void OutgoingAnswer::makeHtmlAnswer(io::ISink &dest, const char *content, std::size_t size)
			{
				makeAnswer(dest, "text/html", content, size);
			}


			void OutgoingAnswer::writeHeaders()
			{
				std::ostringstream sstr;

				static const char *const StatusStrings[StatusCount_] =
				{
					"200 OK",

					"400 Bad Request",
					"401 Unauthorized",
					"403 Forbidden",
					"404 Not Found",
					"409 Conflict",

					"500 Internal Server Error",
					"503 Service Unavailable"
				};

				sstr << "HTTP/1.1 " << StatusStrings[m_status] << "\r\n";
				sstr << m_additionalHeaders;

				const std::string str = sstr.str();
				m_dest.Write(str.data(), str.size());
			}


			void respondUnauthorized(OutgoingAnswer &response,
			                         const std::string &realmName)
			{
				response.setStatus(OutgoingAnswer::Unauthorized);
				response.addHeader("WWW-Authenticate", "Basic realm=\"" + realmName + "\"");
				response.finishWithContent("text/html", "", 0);
			}
		}
	}
}

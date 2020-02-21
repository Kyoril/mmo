// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "auth_incoming_packet.h"
#include "auth_outgoing_packet.h"

namespace mmo
{
	namespace auth
	{
		struct Protocol
		{
			typedef auth::IncomingPacket IncomingPacket;
			typedef auth::OutgoingPacket OutgoingPacket;
		};


		/// Enumerates all OP codes sent by the client.
		namespace client_packet
		{
			enum
			{
				/// Sent by the client right after the connection was established.
				LogonChallenge		= 0x00,
				/// Sent by the client after auth_result::Success was received from the server.
				LogonProof			= 0x01,
				/// <Not sent at the moment>
				ReconnectChallenge	= 0x02,
				/// <Not sent at the moment>
				ReconnectProof		= 0x03,
				/// Sent by the client after receive of successful LogonProof from the server, or if
				/// the player steps back to the realm list.
				RealmList			= 0x10,
			};
		}

		/// Enumerates possible OP codes which the server can send to the client.
		namespace server_packet
		{
			enum Type
			{
				LogonChallenge		= 0x00,
				LogonProof			= 0x01,
				ReconnectChallenge	= 0x02,
				ReconnectProof		= 0x03,
				RealmList			= 0x10,
				XferInitiated		= 0x30,
				XferData			= 0x31,
			};
		}

		/// Enumerates possible authentification result codes.
		namespace auth_result
		{
			enum Type
			{
				/// Success.
				Success,
				/// This account has been closed and is no longer available for use. Please go to <site>/banned.html for further information.
				FailBanned,
				/// The information you have entered is not valid. Please check the spelling of the account name and password. If you need help in retrieving a lost or stolen password, see <site> for more information.
				FailWrongCredentials,
				/// This account is already logged in. Please check the spelling and try again.
				FailAlreadyOnline,
				/// You have used up your prepaid time for this account. Please purchase more to continue playing.
				FailNoTime,
				/// Could not log in at this time. Please try again later.
				FailDbBusy,
				/// Unable to validate game version. This may be caused by file corruption or interference of another program. Please visit <site> for more information and possible solutions to this issue.
				FailVersionInvalid,
				/// Downloading...
				FailVersionUpdate,
				/// Unable to connect.
				FailInvalidServer,
				/// This account has been temporarily suspended. Please go to <site>/banned.html for further information.
				FailSuspended,
				/// Unable to connect.
				FailNoAccess,
				/// Access to this account has been blocked by parental controls. Your settings may be changed in your account preferences at <site>.
				FailParentControl,
				/// You have applied a lock to your account. You can change your locked status by calling your account lock phone number.
				FailLockedEnforced,
				/// Your trial subscription has expired. Please visit <site> to upgrade your account.
				FailTrialEnded,
				/// Internal error.
				FailInternalError,

				Count_
			};
		}

		typedef auth_result::Type AuthResult;

		/// Enumerates possible client platform architectures.
		enum class AuthPlatform
		{
			/// 32 bit platform.
			x86 = 0x00,
			/// 64 bit platform.
			x64 = 0x01
		};

		// Read and convert AuthPlatform value operator
		inline io::Reader& operator>>(io::Reader& reader, AuthPlatform& out_platform)
		{
			uint32 val = 0;
			if ((reader >> io::read<uint32>(val)))
			{
				switch (val)
				{
				case 0x00783836:
					out_platform = AuthPlatform::x86;
					break;
				default:
					reader.setFailure();
					break;
				}
			}

			return reader;
		}

		/// Enumerates possible operating systems a client can run on.
		enum class AuthSystem
		{
			Windows,
			MacOS
		};

		inline io::Reader& operator>>(io::Reader& reader, AuthSystem& out_system)
		{
			uint32 val = 0;
			if ((reader >> io::read<uint32>(val)))
			{
				switch (val)
				{
				case 0x0057696e:
					out_system = AuthSystem::Windows;
					break;
				case 0x004f5358:
					out_system = AuthSystem::MacOS;
					break;
				default:
					reader.setFailure();
					break;
				}
			}

			return reader;
		}

		/// Enumerates possible client localizations.
		enum class AuthLocale
		{
			/// Unused
			Default = 0x00,
			/// French (France)
			frFR = 0x01,
			/// German (Germany)
			deDE = 0x02,
			/// English (Great Britain)
			enGB = 0x03,
			/// English (USA)
			enUS = 0x04,
			/// Italian (Italy)
			itIT = 0x05,
			/// Korean (Korea)
			koKR = 0x06,
			/// Simplified Chinese (China)
			zhCN = 0x07,
			/// Traditional Chinese (Taiwan)
			zhTW = 0x08,
			/// Russian (Russia)
			ruRU = 0x09,
			/// Spanish (Spain)
			esES = 0x0A,
			/// Spanish (Mexico)
			esMX = 0x0B,
			/// Portuguese (Brazil)
			ptBR = 0x0C
		};

		inline io::Reader& operator>>(io::Reader& reader, AuthLocale& out_locale)
		{
			uint32 val = 0;
			if ((reader >> io::read<uint32>(val)))
			{
				switch (val)
				{
				case 0x66724652: out_locale = AuthLocale::frFR; break;
				case 0x64654445: out_locale = AuthLocale::deDE; break;
				case 0x656e4742: out_locale = AuthLocale::enGB; break;
				case 0x656e5553: out_locale = AuthLocale::enUS; break;
				case 0x69744954: out_locale = AuthLocale::itIT; break;
				case 0x6b6f4b52: out_locale = AuthLocale::koKR; break;
				case 0x7a68434e: out_locale = AuthLocale::zhCN; break;
				case 0x7a685457: out_locale = AuthLocale::zhTW; break;
				case 0x72755255: out_locale = AuthLocale::ruRU; break;
				case 0x65734553: out_locale = AuthLocale::esES; break;
				case 0x65734d58: out_locale = AuthLocale::esMX; break;
				case 0x70744252: out_locale = AuthLocale::ptBR; break;
				default:
					reader.setFailure();
					break;
				}
			}

			return reader;
		}

		/// Enumerate possible account flags.
		namespace account_flags
		{
			enum Type
			{
				/// Player account has game master rights.
				GameMaster	= 0x000001,
				/// Account has administrative rights.
				Admin		= 0x000002,
				/// Account has developer rights.
				Developer	= 0x000004,
				/// Player account is a trial account.
				Trial		= 0x000008,
			};
		}

		typedef account_flags::Type AccountFlags;

		namespace realm_flags
		{
			enum Type
			{
				None			= 0x00,
				Invalid			= 0x01,
				Offline			= 0x02,
				SpecifyBuild	= 0x04,		// Client will show realm version in realm list screen: "Realmname (major.minor.patch.build)"
				NewPlayers		= 0x20,
				Recommended		= 0x40,
				Full			= 0x80
			};
		}

		typedef realm_flags::Type RealmFlags;

		struct RealmEntry
		{
			std::string name;
			std::string address;
			uint16 port;
			uint32 icon;
			RealmFlags flags;

			explicit RealmEntry()
				: name("UNNAMED")
				, address("127.0.0.1")
				, port(8127)
				, icon(0)
				, flags(realm_flags::None)
			{
			}

			RealmEntry(const RealmEntry &Other)
				: name(Other.name)
				, address(Other.address)
				, port(Other.port)
				, icon(Other.icon)
				, flags(Other.flags)
			{
			}

			RealmEntry &operator=(const RealmEntry &Other)
			{
				name = Other.name;
				address = Other.address;
				port = Other.port;
				icon = Other.icon;
				flags = Other.flags;
				return *this;
			}
		};
	}
}

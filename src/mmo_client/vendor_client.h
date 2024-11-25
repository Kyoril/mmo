#pragma once

#include "base/non_copyable.h"
#include "net/realm_connector.h"

namespace mmo
{
	class VendorClient final : public NonCopyable
	{
	public:
		VendorClient(RealmConnector& connector);
		~VendorClient();

	public:
		void Initialize();

		void Shutdown();

	private:
		uint32 GetNumVendorItems() const;

	private:
		PacketParseResult OnListInventory(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerConnections;
	};
}

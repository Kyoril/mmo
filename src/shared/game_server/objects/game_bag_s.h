#pragma once

#include "game_item_s.h"

namespace mmo
{
	class GameBagS final : public GameItemS
	{
		friend io::Writer& operator << (io::Writer& w, GameBagS const& object);
		friend io::Reader& operator >> (io::Reader& r, GameBagS& object);

	public:

		explicit GameBagS(const proto::Project& project, const proto::ItemEntry& entry);
		~GameBagS() override = default;

	public:
		void Initialize() override;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Container; }

		uint32 GetSlotCount() const { return Get<uint32>(object_fields::NumSlots); }

		bool IsEmpty() const;

	protected:
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::BagFieldCount);
		}
	};


	io::Writer& operator << (io::Writer& w, GameBagS const& object);
	io::Reader& operator >> (io::Reader& r, GameBagS& object);
}
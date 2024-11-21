
#include "item.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const ItemInfo& itemInfo)
	{
		writer
			<< io::write<uint64>(itemInfo.id)
			<< io::write_dynamic_range<uint8>(itemInfo.name)
			<< io::write_dynamic_range<uint8>(itemInfo.description)
			<< io::write<uint32>(itemInfo.itemClass)
			<< io::write<uint32>(itemInfo.itemSubclass)
			<< io::write<uint32>(itemInfo.inventoryType)
			<< io::write<uint32>(itemInfo.displayId)
			<< io::write<uint32>(itemInfo.quality)
			<< io::write<uint32>(itemInfo.flags)
			<< io::write<uint32>(itemInfo.buyCount)
			<< io::write<uint32>(itemInfo.buyPrice)
			<< io::write<uint32>(itemInfo.sellPrice)
			<< io::write<int32>(itemInfo.allowedClasses)
			<< io::write<int32>(itemInfo.allowedRaces)
			<< io::write<uint32>(itemInfo.itemlevel)
			<< io::write<uint32>(itemInfo.requiredlevel)
			<< io::write<uint32>(itemInfo.requiredskill)
			<< io::write<uint32>(itemInfo.requiredskillrank)
			<< io::write<uint32>(itemInfo.requiredspell)
			<< io::write<uint32>(itemInfo.requiredhonorrank)
			<< io::write<uint32>(itemInfo.requiredcityrank)
			<< io::write<uint32>(itemInfo.requiredrep)
			<< io::write<uint32>(itemInfo.requiredreprank)
			<< io::write<uint32>(itemInfo.maxcount)
			<< io::write<uint32>(itemInfo.maxstack)
			<< io::write<uint32>(itemInfo.containerslots);
		for (auto stat : itemInfo.stats)
		{
			writer.WritePOD(stat);
		}
		writer.WritePOD(itemInfo.damage);

		writer
			<< io::write<uint32>(itemInfo.armor);
		for (const auto& resistance : itemInfo.resistance)
		{
			writer << io::write<uint32>(resistance);
		}

		writer
			<< io::write<uint32>(itemInfo.ammotype);
		for (const auto& spell : itemInfo.spells)
		{
			writer.WritePOD(spell);
		}
		writer
			<< io::write<uint32>(itemInfo.bonding)
			<< io::write<uint32>(itemInfo.lockid)
			<< io::write<uint32>(itemInfo.sheath)
			<< io::write<uint32>(itemInfo.randomproperty)
			<< io::write<uint32>(itemInfo.randomsuffix)
			<< io::write<uint32>(itemInfo.block)
			<< io::write<uint32>(itemInfo.itemset)
			<< io::write<uint32>(itemInfo.maxdurability)
			<< io::write<uint32>(itemInfo.area)
			<< io::write<int32>(itemInfo.map)
			<< io::write<int32>(itemInfo.bagfamily)
			<< io::write<int32>(itemInfo.material)
			<< io::write<int32>(itemInfo.totemcategory);
		for (const auto& socket : itemInfo.sockets)
		{
			writer.WritePOD(socket);
		}
		writer
			<< io::write<uint32>(itemInfo.socketbonus)
			<< io::write<uint32>(itemInfo.gemproperties)
			<< io::write<uint32>(itemInfo.disenchantskillval)
			<< io::write<uint32>(itemInfo.foodtype)
			<< io::write<uint32>(itemInfo.duration)
			<< io::write<uint32>(itemInfo.extraflags)
			<< io::write<uint32>(itemInfo.startquestid)
			<< io::write<float>(itemInfo.rangedrangepercent)
			<< io::write<uint32>(itemInfo.skill)
			<< io::write_dynamic_range<uint8>(itemInfo.icon);

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, ItemInfo& itemInfo)
	{
		reader
			>> io::read<uint64>(itemInfo.id)
			>> io::read_container<uint8>(itemInfo.name)
			>> io::read_container<uint8>(itemInfo.description)
			>> io::read<uint32>(itemInfo.itemClass)
			>> io::read<uint32>(itemInfo.itemSubclass)
			>> io::read<uint32>(itemInfo.inventoryType)
			>> io::read<uint32>(itemInfo.displayId)
			>> io::read<uint32>(itemInfo.quality)
			>> io::read<uint32>(itemInfo.flags)
			>> io::read<uint32>(itemInfo.buyCount)
			>> io::read<uint32>(itemInfo.buyPrice)
			>> io::read<uint32>(itemInfo.sellPrice)
			>> io::read<int32>(itemInfo.allowedClasses)
			>> io::read<int32>(itemInfo.allowedRaces)
			>> io::read<uint32>(itemInfo.itemlevel)
			>> io::read<uint32>(itemInfo.requiredlevel)
			>> io::read<uint32>(itemInfo.requiredskill)
			>> io::read<uint32>(itemInfo.requiredskillrank)
			>> io::read<uint32>(itemInfo.requiredspell)
			>> io::read<uint32>(itemInfo.requiredhonorrank)
			>> io::read<uint32>(itemInfo.requiredcityrank)
			>> io::read<uint32>(itemInfo.requiredrep)
			>> io::read<uint32>(itemInfo.requiredreprank)
			>> io::read<uint32>(itemInfo.maxcount)
			>> io::read<uint32>(itemInfo.maxstack)
			>> io::read<uint32>(itemInfo.containerslots);
		for (auto& stat : itemInfo.stats)
		{
			reader.readPOD(stat);
		}
		reader.readPOD(itemInfo.damage);

		reader
			>> io::read<uint32>(itemInfo.armor);
		for (auto& resistance : itemInfo.resistance)
		{
			reader >> io::read<uint32>(resistance);
		}

		reader
			>> io::read<uint32>(itemInfo.ammotype);
		for (auto& spell : itemInfo.spells)
		{
			reader.readPOD(spell);
		}
		reader
			>> io::read<uint32>(itemInfo.bonding)
			>> io::read<uint32>(itemInfo.lockid)
			>> io::read<uint32>(itemInfo.sheath)
			>> io::read<uint32>(itemInfo.randomproperty)
			>> io::read<uint32>(itemInfo.randomsuffix)
			>> io::read<uint32>(itemInfo.block)
			>> io::read<uint32>(itemInfo.itemset)
			>> io::read<uint32>(itemInfo.maxdurability)
			>> io::read<uint32>(itemInfo.area)
			>> io::read<int32>(itemInfo.map)
			>> io::read<int32>(itemInfo.bagfamily)
			>> io::read<int32>(itemInfo.material)
			>> io::read<int32>(itemInfo.totemcategory);
		for (auto& socket : itemInfo.sockets)
		{
			reader.readPOD(socket);
		}
		reader
			>> io::read<uint32>(itemInfo.socketbonus)
			>> io::read<uint32>(itemInfo.gemproperties)
			>> io::read<uint32>(itemInfo.disenchantskillval)
			>> io::read<uint32>(itemInfo.foodtype)
			>> io::read<uint32>(itemInfo.duration)
			>> io::read<uint32>(itemInfo.extraflags)
			>> io::read<uint32>(itemInfo.startquestid)
			>> io::read<float>(itemInfo.rangedrangepercent)
			>> io::read<uint32>(itemInfo.skill)
			>> io::read_container<uint8>(itemInfo.icon);

		return reader;
	}
}

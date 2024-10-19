#pragma once

namespace mmo
{
	namespace item_quality
	{
		enum Type
		{
			Poor,
			Common,
			Uncommon,
			Rare,
			Epic,
			Legendary,

			Count_
		};
	}

	typedef item_quality::Type ItemQuality;

	namespace item_class
	{
		enum Type
		{
			Consumable,
			Container,
			Weapon,
			Gem,
			Armor,
			Reagent,
			Projectile,
			TradeGoods,
			Generic,
			Recipe,
			Money,
			Quiver,
			Quest,
			Key,
			Permanent,
			Junk,

			Count_
		};
	}

	typedef item_class::Type ItemClass;

	namespace item_subclass_consumable
	{
		enum Type
		{
			Consumable,
			Potion,
			Elixir,
			Flask,
			Scroll,
			Food,
			ItemEnhancement,
			Bandage,

			Count_
		};
	}

	typedef item_subclass_consumable::Type ItemSubclassConsumable;

	namespace item_subclass_container
	{
		enum Type
		{
			Container,

			Count_
		};
	}

	typedef item_subclass_container::Type ItemSubclassContainer;

	namespace item_subclass_weapon
	{
		enum Type
		{
			OneHandedAxe,
			TwoHandedAxe,
			Bow,
			Gun,
			OneHandedMace,
			TwoHandedMace,
			Polearm,
			OneHandedSword,
			TwoHandedSword,
			Staff,
			Fist,
			Dagger,
			Thrown,
			Spear,
			CrossBow,
			Wand,
			FishingPole
		};
	}

	typedef item_subclass_weapon::Type ItemSubclassWeapon;

	namespace item_subclass_gem
	{
		enum Type
		{
			Red,
			Blue,
			Yellow,
			Purple,
			Green,
			Orange,
			Prismatic,

			Count_
		};
	}

	typedef item_subclass_gem::Type ItemSubclassGem;

	namespace item_subclass_armor
	{
		enum Type
		{
			Misc,
			Cloth,
			Leather,
			Mail,
			Plate,
			Buckler,
			Shield,
			Libram,
			Idol,
			Totem,

			Count_
		};
	}

	typedef item_subclass_armor::Type ItemSubclassArmor;

	namespace item_subclass_projectile
	{
		enum Type
		{
			Wand,
			Bolt,
			Arrow,
			Bullet,
			Thrown,

			Count_
		};
	}

	typedef item_subclass_projectile::Type ItemSubclassProjectile;

	namespace item_subclass_trade_goods
	{
		enum Type
		{
			TradeGoods,
			Parts,
			Eplosives,
			Devices,
			Jewelcrafting,
			Cloth,
			Leather,
			MetalStone,
			Meat,
			Herb,
			Elemental,
			TradeGoodsOther,
			Enchanting,
			Material,

			Count_
		};
	}

	typedef item_subclass_trade_goods::Type ItemSubclassTradeGoods;

	namespace weapon_prof
	{
		enum Type
		{
			None = 0x00000,
			OneHandAxe = 0x00001,
			TwoHandAxe = 0x00002,
			Bow = 0x00004,
			Gun = 0x00008,
			OneHandMace = 0x00010,
			TwoHandMace = 0x00020,
			Polearm = 0x00040,
			OneHandSword = 0x00080,
			TwoHandSword = 0x00100,
			Staff = 0x00400,
			Fist = 0x02000,
			Dagger = 0x08000,
			Throw = 0x10000,
			Crossbow = 0x40000,
			Wand = 0x80000
		};
	}

	typedef weapon_prof::Type WeaponProf;

	namespace armor_prof
	{
		enum Type
		{
			None = 0x00000,
			Common = 0x00001,
			Cloth = 0x00002,
			Leather = 0x00004,
			Mail = 0x00008,
			Plate = 0x00010,
			Shield = 0x00040,
			Libram = 0x00080,
			Fetish = 0x00100,
			Totem = 0x00200
		};
	}

	typedef armor_prof::Type ArmorProf;

	namespace inventory_type
	{
		enum Type
		{
			NonEquip,

			Head,

			Neck,

			Shoulders,
			Body,
			Chest,
			Waist,
			Legs,
			Feet,
			Wrists,
			Hands,
			Finger,
			Trinket,
			Weapon,
			Shield,
			Ranged,
			Cloak,
			TwoHandedWeapon,
			Bag,
			Tabard,
			Robe,
			MainHandWeapon,
			OffHandWeapon,
			Holdable,
			Ammo,
			Thrown,
			RangedRight,
			Quiver,
			Relic
		};
	}

	typedef inventory_type::Type InventoryType;
}
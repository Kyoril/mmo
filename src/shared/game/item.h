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


	namespace inventory_change_failure
	{
		enum Enum
		{
			Okay = 0,
			CantEquipLevel = 1,
			CantEquipSkill = 2,
			ItemDoesNotGoToSlot = 3,
			BagFull = 4,
			NonEmptyBagOverOtherBag = 5,
			CantTradeEquipBags = 6,
			OnlyAmmoCanGoHere = 7,
			NoRequiredProficiency = 8,
			NoEquipmentSlotAvailable = 9,
			YouCanNeverUseThatItem = 10,
			YouCanNeverUseThatItem2 = 11,
			NoEquipmentSlotAvailable2 = 12,
			CantEquipWithTwoHanded = 13,
			CantDualWield = 14,
			ItemDoesntGoIntoBag = 15,
			ItemDoesntGoIntoBag2 = 16,
			CantCarryMoreOfThis = 17,
			NoEquipmentSlotAvailable3 = 18,
			ItemCantStack = 19,
			ItemCantBeEquipped = 20,
			ItemsCantBeSwapped = 21,
			SlotIsEmpty = 22,
			ItemNotFound = 23,
			CantDropSoulbound = 24,
			OutOfRange = 25,
			TriedToSplitMoreThanCount = 26,
			CouldntSplitItems = 27,
			MissingReagent = 28,
			NotEnoughMoney = 29,
			NotABag = 30,
			CanOnlyDoWithEmptyBags = 31,
			DontOwnThatItem = 32,
			CanEquipOnlyOneQuiver = 33,
			MustPurchaseThatBagSlot = 34,
			TooFarAwayFromBank = 35,
			ItemLocked = 36,
			YouAreStunned = 37,
			YouAreDead = 38,
			CantDoRightNow = 39,
			InternalBagError = 40,
			CanEquipOnlyOneQuiver2 = 41,
			CanEquipOnlyOneAmmoPouch = 42,
			StackableCantBeWrapped = 43,
			EquippedCantBeWrapped = 44,
			WrappedCantBeWrapped = 45,
			BoundCantBeWrapped = 46,
			UniqueCantBeWrapped = 47,
			BagsCantBeWrapped = 48,
			AlreadyLooted = 49,
			InventoryFull = 50,
			BankFull = 51,
			ItemIsCurrentlySoldOut = 52,
			BagFull3 = 53,
			ItemNotFound2 = 54,
			ItemCantStack2 = 55,
			BagFull4 = 56,
			ItemSoldOut = 57,
			ObjectIsBusy = 58,
			None = 59,
			NotInCombat = 60,
			NotWhileDisarmed = 61,
			BagFull6 = 62,
			CantEquipBank = 63,
			CantEquipReputation = 64,
			TooManySpecialBags = 65,
			CantLootThatNow = 66,
			ItemUniqueEquippable = 67,
			VendorMissingTurnIns = 68,
			NotEnoughHonorPoints = 69,
			NotEnoughArenaPoints = 70,
			ItemMaxCountSocketed = 71,
			MailBoundItem = 72,
			NoSplitWhileProspecting = 73,
			ItemMaxCountEquippedSocketed = 75,
			ItemUniqueEquippableSocketed = 76,
			TooMuchGold = 77,
			NotDuringArenaMatch = 78,
			CannotTradeThat = 79,
			PersonalArenaRatingTooLow = 80
		};
	}

	typedef inventory_change_failure::Enum InventoryChangeFailure;


	namespace weapon_attack
	{
		enum Type
		{
			BaseAttack = 0,
			OffhandAttack = 1,
			RangedAttack = 2
		};
	}

	typedef weapon_attack::Type WeaponAttack;


	namespace player_item_slots
	{
		enum Enum
		{
			Start = 0,
			End = 118,

			Count_ = (End - Start)
		};
	};

	typedef player_item_slots::Enum PlayerItemSlots;

	namespace player_equipment_slots
	{
		enum Enum
		{
			Start = 0,

			Head = 0,
			Neck = 1,
			Shoulders = 2,
			Body = 3,
			Chest = 4,
			Waist = 5,
			Legs = 6,
			Feet = 7,
			Wrists = 8,
			Hands = 9,
			Finger1 = 10,
			Finger2 = 11,
			Trinket1 = 12,
			Trinket2 = 13,
			Back = 14,
			Mainhand = 15,
			Offhand = 16,
			Ranged = 17,
			Tabard = 18,

			End = 19,
			Count_ = (End - Start)
		};
	}

	typedef player_equipment_slots::Enum PlayerEquipmentSlots;

	namespace player_inventory_slots
	{
		enum Enum
		{
			Bag_0 = 255,
			Start = 19,
			End = 23,

			Count_ = (End - Start)
		};
	}

	typedef player_inventory_slots::Enum PlayerInventorySlots;

	namespace player_inventory_pack_slots
	{
		enum Enum
		{
			Start = 23,
			End = 39,

			Count_ = (End - Start)
		};
	}

	typedef player_inventory_pack_slots::Enum PlayerInventoryPackSlots;

	namespace player_bank_item_slots
	{
		enum Enum
		{
			Start = 39,
			End = 67,

			Count_ = (End - Start)
		};
	}

	typedef player_bank_item_slots::Enum PlayerBankItemSlots;

	namespace player_bank_bag_slots
	{
		enum Enum
		{
			Start = 67,
			End = 74,

			Count_ = (End - Start)
		};
	}

	typedef player_bank_bag_slots::Enum PlayerBankBagSlots;

	namespace player_buy_back_slots
	{
		enum Enum
		{
			Start = 74,
			End = 86,

			Count_ = (End - Start)
		};
	}

	typedef player_buy_back_slots::Enum PlayerBuyBackSlots;

	namespace item_binding
	{
		enum Type
		{
			/// No binding at all
			NoBinding,

			/// Bound when picked up
			BindWhenPickedUp,

			/// Bound when equipped
			BindWhenEquipped,

			/// Bound when used
			BindWhenUsed
		};
	}

	typedef item_binding::Type ItemBinding;

	namespace item_flags
	{
		enum Type
		{
			/// The item is bound to a player character.
			Bound = 0x00000001,

			/// This is a conjured item, which means that it will be destroyed after being logged
			/// out more than 15 minutes.
			Conjured = 0x00000002,

			/// This item contains items (not a bag!)
			Openable = 0x00000004,

			/// The item is wrapped
			Wrapped = 0x00000008,

			/// ?
			Wrapper = 0x00000200,

			/// Indicates that this item is lootable by the whole party (one drop for every valid member)
			PartyLoot = 0x00000800,

			/// Item can only be equipped once (weapons / rings)
			UniqueEquipped = 0x00001000,
		};
	}

	typedef item_flags::Type ItemFlags;

}
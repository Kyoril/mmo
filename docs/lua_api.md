# MMO Client Lua API Documentation

This document provides a comprehensive reference of all Lua functions available to the UI system in the MMO client. These functions can be used in UI scripts to interact with the game's systems.

## Table of Contents

- [Global Functions](#global-functions)
- [Character Functions](#character-functions)
- [Movement Functions](#movement-functions)
- [Targeting Functions](#targeting-functions)
- [UI Functions](#ui-functions)
- [Combat Functions](#combat-functions)
- [Quest Functions](#quest-functions)
- [Inventory Functions](#inventory-functions)
- [Social Functions](#social-functions)
- [Guild Functions](#guild-functions)
- [Vendor & Trainer Functions](#vendor--trainer-functions)
- [Character Creation Functions](#character-creation-functions)
- [Character Selection Functions](#character-selection-functions)
- [Loot Functions](#loot-functions)
- [Utility Functions](#utility-functions)
- [Global Objects](#global-objects)

---

## Global Functions

### RunConsoleCommand
**Parameters:** `cmdLine` (string) - The console command to execute  
**Returns:** None  
**Description:** Executes a console command with the specified command line.

### GetCVar
**Parameters:** `name` (string) - The name of the console variable to retrieve  
**Returns:** string - The value of the console variable, or nil if not found  
**Description:** Returns the value of the specified console variable.

### EnterWorld
**Parameters:** None  
**Returns:** None  
**Description:** Enters the world from the character selection screen.

### print
**Parameters:** `text` (string) - The text to print to the console  
**Returns:** None  
**Description:** Prints text to the console/log.

### IsShiftKeyDown
**Parameters:** None  
**Returns:** boolean - True if shift key is pressed, false otherwise  
**Description:** Checks if the shift key is currently being pressed.

### GetTime
**Parameters:** None  
**Returns:** number - The current time in milliseconds  
**Description:** Returns the current time in milliseconds.

### PlaySound
**Parameters:** `sound` (string) - The path to the sound file to play  
**Returns:** None  
**Description:** Plays a sound specified by the path.

### Quit
**Parameters:** None  
**Returns:** None  
**Description:** Exits the game client.

### Logout
**Parameters:** None  
**Returns:** None  
**Description:** Logs the player out from the world.

## Character Functions

### UnitExists
**Parameters:** `unitName` (string) - The name of the unit to check ("player", "target", etc.)  
**Returns:** boolean - True if the unit exists, false otherwise  
**Description:** Checks if the specified unit exists.

### UnitAttributeCost
**Parameters:** `unitName` (string) - The name of the unit to check, `attribute` (number) - The attribute ID to check  
**Returns:** number - The cost of the attribute  
**Description:** Returns the cost of increasing the specified attribute for the unit.

### UnitStat
**Parameters:** `unitName` (string) - The name of the unit, `statId` (number) - The ID of the stat  
**Returns:** number, number - Base value and modifier value  
**Description:** Returns the base value and modifier value of the specified stat for the unit.

### UnitArmor
**Parameters:** `unitName` (string) - The name of the unit  
**Returns:** number, number - Base armor and modifier value  
**Description:** Returns the base armor and modifier value of the specified unit.

### UnitMoney
**Parameters:** `unitName` (string) - The name of the unit  
**Returns:** number - The amount of money the unit has  
**Description:** Returns the amount of money the unit has.

### UnitDisplayId
**Parameters:** `unitName` (string) - The name of the unit  
**Returns:** number - The display ID of the unit, or -1 if not found  
**Description:** Returns the display ID of the specified unit.

### UnitAura
**Parameters:** `unitName` (string) - The name of the unit, `id` (number) - The aura ID  
**Returns:** Spell, number - The spell entry and duration  
**Description:** Returns the spell entry and duration of an aura on a unit.

### PlayerXp
**Parameters:** None  
**Returns:** number - The current XP of the player  
**Description:** Returns the current XP of the player.

### PlayerNextLevelXp
**Parameters:** None  
**Returns:** number - The XP needed for the player's next level, 0 if at max level  
**Description:** Returns the XP needed for the player's next level.

### GetUnit
**Parameters:** `name` (string) - The name of the unit  
**Returns:** UnitHandle - A handle to the unit, or nil if not found  
**Description:** Returns a handle to the specified unit.

### AddAttributePoint
**Parameters:** `attribute` (number) - The attribute ID to add a point to  
**Returns:** None  
**Description:** Adds an attribute point to the specified attribute.

## Movement Functions

### MoveForwardStart
**Parameters:** None  
**Returns:** None  
**Description:** Starts moving the player character forward.

### MoveForwardStop
**Parameters:** None  
**Returns:** None  
**Description:** Stops moving the player character forward.

### MoveBackwardStart
**Parameters:** None  
**Returns:** None  
**Description:** Starts moving the player character backward.

### MoveBackwardStop
**Parameters:** None  
**Returns:** None  
**Description:** Stops moving the player character backward.

### TurnLeftStart
**Parameters:** None  
**Returns:** None  
**Description:** Starts turning the player character left.

### TurnLeftStop
**Parameters:** None  
**Returns:** None  
**Description:** Stops turning the player character left.

### TurnRightStart
**Parameters:** None  
**Returns:** None  
**Description:** Starts turning the player character right.

### TurnRightStop
**Parameters:** None  
**Returns:** None  
**Description:** Stops turning the player character right.

### StrafeLeftStart
**Parameters:** None  
**Returns:** None  
**Description:** Starts strafing the player character left.

### StrafeLeftStop
**Parameters:** None  
**Returns:** None  
**Description:** Stops strafing the player character left.

### StrafeRightStart
**Parameters:** None  
**Returns:** None  
**Description:** Starts strafing the player character right.

### StrafeRightStop
**Parameters:** None  
**Returns:** None  
**Description:** Stops strafing the player character right.

### ToggleAutoRun
**Parameters:** None  
**Returns:** None  
**Description:** Toggles auto-run mode for the player character.

### Jump
**Parameters:** None  
**Returns:** None  
**Description:** Makes the player character jump.

## Targeting Functions

### TargetUnit
**Parameters:** `name` (string) - The name of the unit to target  
**Returns:** None  
**Description:** Targets the specified unit.

### ClearTarget
**Parameters:** None  
**Returns:** boolean - True if a target was cleared, false if there was no target  
**Description:** Clears the player's current target.

### TargetNearestEnemy
**Parameters:** None  
**Returns:** None  
**Description:** Targets the nearest enemy unit.

## UI Functions

### GetZoneText
**Parameters:** None  
**Returns:** string - The name of the current zone  
**Description:** Returns the name of the current zone.

### GetSpellDescription
**Parameters:** `spell` (Spell) - The spell entry to get the description for  
**Returns:** string - The formatted spell description  
**Description:** Returns the formatted description text for a spell.

### GetSpellAuraText
**Parameters:** `spell` (Spell) - The spell entry to get the aura text for  
**Returns:** string - The formatted aura text  
**Description:** Returns the formatted aura text for a spell.

### IsPassiveSpell
**Parameters:** `spell` (Spell) - The spell entry to check  
**Returns:** boolean - True if the spell is passive, false otherwise  
**Description:** Checks if the specified spell is passive.

## Combat Functions

### GetSpell
**Parameters:** `index` (number) - The index of the spell in the spellbook  
**Returns:** Spell - The spell entry, or nil if not found  
**Description:** Returns the spell entry for the spell at the specified index in the player's spellbook.

### CastSpell
**Parameters:** `spellIndex` (number) - The index of the spell to cast  
**Returns:** None  
**Description:** Casts the spell at the specified index in the player's spellbook.

### SpellStopCasting
**Parameters:** None  
**Returns:** boolean - True if casting was stopped, false otherwise  
**Description:** Stops casting the current spell.

### ReviveMe
**Parameters:** None  
**Returns:** None  
**Description:** Requests to revive the player character.

### RandomRoll
**Parameters:** `min` (number) - The minimum roll value, `max` (number) - The maximum roll value  
**Returns:** None  
**Description:** Performs a random roll between min and max values.

## Quest Functions

### GetGreetingText
**Parameters:** None  
**Returns:** string - The greeting text from an NPC  
**Description:** Returns the greeting text from the currently interacted NPC.

### GetNumAvailableQuests
**Parameters:** None  
**Returns:** number - The number of available quests  
**Description:** Returns the number of quests available from the currently interacted NPC.

### GetAvailableQuest
**Parameters:** `index` (number) - The index of the quest to retrieve  
**Returns:** QuestListEntry - The quest entry, or nil if not found  
**Description:** Returns the quest entry for the quest at the specified index.

### QueryQuestDetails
**Parameters:** `questId` (number) - The ID of the quest  
**Returns:** None  
**Description:** Queries for details about the specified quest.

### GetQuestDetails
**Parameters:** None  
**Returns:** QuestDetails - The details of the queried quest, or nil if not available  
**Description:** Returns the details of the previously queried quest.

### AcceptQuest
**Parameters:** `questId` (number) - The ID of the quest  
**Returns:** None  
**Description:** Accepts the specified quest.

### GetNumQuestLogEntries
**Parameters:** None  
**Returns:** number - The number of quests in the quest log  
**Description:** Returns the number of quests in the player's quest log.

### GetQuestLogEntry
**Parameters:** `index` (number) - The index of the quest to retrieve  
**Returns:** QuestLogEntry - The quest log entry, or nil if not found  
**Description:** Returns the quest log entry at the specified index.

### GetNumGossipActions
**Parameters:** None  
**Returns:** number - The number of gossip actions  
**Description:** Returns the number of gossip actions available from the currently interacted NPC.

### GetGossipAction
**Parameters:** `index` (number) - The index of the gossip action  
**Returns:** GossipMenuAction - The gossip action, or nil if not found  
**Description:** Returns the gossip action at the specified index.

### AbandonQuest
**Parameters:** `questId` (number) - The ID of the quest  
**Returns:** None  
**Description:** Abandons the specified quest.

### GetQuestReward
**Parameters:** `rewardChoice` (number) - The index of the reward choice  
**Returns:** None  
**Description:** Selects the specified reward for the current quest.

### QuestLogSelectQuest
**Parameters:** `questId` (number) - The ID of the quest  
**Returns:** None  
**Description:** Selects the specified quest in the quest log.

### GetQuestLogSelection
**Parameters:** None  
**Returns:** number - The ID of the selected quest, or 0 if none selected  
**Description:** Returns the ID of the currently selected quest in the quest log.

### GetQuestObjectiveCount
**Parameters:** None  
**Returns:** number - The number of objectives for the selected quest  
**Description:** Returns the number of objectives for the currently selected quest.

### GetQuestObjectiveText
**Parameters:** `index` (number) - The index of the objective  
**Returns:** string - The objective text  
**Description:** Returns the text of the objective at the specified index.

### GossipAction
**Parameters:** `index` (number) - The index of the gossip action  
**Returns:** None  
**Description:** Executes the gossip action at the specified index.

### GetQuestDetailsText
**Parameters:** `quest` (Quest) - The quest info  
**Returns:** string - The formatted quest details text  
**Description:** Returns the formatted details text for the specified quest.

### GetQuestObjectivesText
**Parameters:** `quest` (Quest) - The quest info  
**Returns:** string - The formatted quest objectives text  
**Description:** Returns the formatted objectives text for the specified quest.

## Inventory Functions

### GetBackpackSlot
**Parameters:** `slotId` (number) - The slot ID within the backpack  
**Returns:** number - The combined slot ID, or -1 if invalid  
**Description:** Returns the combined slot ID for the specified backpack slot.

### IsBackpackSlot
**Parameters:** `slotId` (number) - The combined slot ID to check  
**Returns:** boolean - True if the slot is a backpack slot, false otherwise  
**Description:** Checks if the specified slot is a backpack slot.

### GetBagSlot
**Parameters:** `bagIndex` (number) - The bag index (1-4), `slotId` (number) - The slot ID within the bag  
**Returns:** number - The combined slot ID, or -1 if invalid  
**Description:** Returns the combined slot ID for the specified bag and slot.

### GetInventorySlotItem
**Parameters:** `unitName` (string) - The name of the unit, `slotId` (number) - The combined slot ID  
**Returns:** ItemHandle - A handle to the item, or nil if not found  
**Description:** Returns a handle to the item in the specified inventory slot.

### PickupContainerItem
**Parameters:** `slot` (number) - The combined slot ID  
**Returns:** None  
**Description:** Picks up the item in the specified container slot.

### UseContainerItem
**Parameters:** `slot` (number) - The combined slot ID  
**Returns:** None  
**Description:** Uses the item in the specified container slot.

### GetContainerNumSlots
**Parameters:** `container` (number) - The container index  
**Returns:** number - The number of slots in the container  
**Description:** Returns the number of slots in the specified container.

### GetItemCount
**Parameters:** `id` (number) - The item ID  
**Returns:** number - The count of the item in the player's inventory  
**Description:** Returns the count of the specified item in the player's inventory.

### PickupSpell
**Parameters:** `spell` (number) - The spell ID  
**Returns:** None  
**Description:** Picks up the specified spell to be placed on the action bar.

### GetItemSpellTriggerType
**Parameters:** `item` (Item) - The item info, `index` (number) - The spell index  
**Returns:** string - The trigger type name, or nil if not found  
**Description:** Returns the name of the trigger type for the item's spell at the specified index.

### GetItemSpell
**Parameters:** `item` (Item) - The item info, `index` (number) - The spell index  
**Returns:** Spell - The spell entry, or nil if not found  
**Description:** Returns the spell entry for the item's spell at the specified index.

## Social Functions

### HasPartyMember
**Parameters:** `index` (number) - The index of the party member  
**Returns:** boolean - True if there is a party member at the specified index, false otherwise  
**Description:** Checks if there is a party member at the specified index.

### GetPartySize
**Parameters:** None  
**Returns:** number - The number of members in the party  
**Description:** Returns the number of members in the party.

### GetPartyLeaderIndex
**Parameters:** None  
**Returns:** number - The index of the party leader  
**Description:** Returns the index of the party leader.

### IsPartyLeader
**Parameters:** None  
**Returns:** boolean - True if the player is the party leader, false otherwise  
**Description:** Checks if the player is the party leader.

### AcceptGroup
**Parameters:** None  
**Returns:** None  
**Description:** Accepts a group invitation.

### DeclineGroup
**Parameters:** None  
**Returns:** None  
**Description:** Declines a group invitation.

### InviteByName
**Parameters:** `playerName` (string) - The name of the player to invite  
**Returns:** None  
**Description:** Invites the specified player to the group.

### UninviteByName
**Parameters:** `playerName` (string) - The name of the player to remove  
**Returns:** None  
**Description:** Removes the specified player from the group.

### SendChatMessage
**Parameters:** `message` (string) - The message to send, `type` (string) - The type of chat message, `target` (string, optional) - The target for whisper or channel messages  
**Returns:** None  
**Description:** Sends a chat message of the specified type to the specified target.

## Guild Functions

### GuildInviteByName
**Parameters:** `name` (string) - The name of the player to invite  
**Returns:** None  
**Description:** Invites the specified player to the guild.

### GuildUninviteByName
**Parameters:** `name` (string) - The name of the player to remove  
**Returns:** None  
**Description:** Removes the specified player from the guild.

### GuildPromoteByName
**Parameters:** `name` (string) - The name of the player to promote  
**Returns:** None  
**Description:** Promotes the specified player in the guild.

### GuildDemoteByName
**Parameters:** `name` (string) - The name of the player to demote  
**Returns:** None  
**Description:** Demotes the specified player in the guild.

### GuildSetLeaderByName
**Parameters:** `name` (string) - The name of the player to make leader  
**Returns:** None  
**Description:** Makes the specified player the guild leader.

### GuildSetMOTD
**Parameters:** `motd` (string) - The new message of the day  
**Returns:** None  
**Description:** Sets the guild message of the day.

### GuildLeave
**Parameters:** None  
**Returns:** None  
**Description:** Leaves the guild.

### GuildDisband
**Parameters:** None  
**Returns:** None  
**Description:** Disbands the guild.

### AcceptGuild
**Parameters:** None  
**Returns:** None  
**Description:** Accepts a guild invitation.

### DeclineGuild
**Parameters:** None  
**Returns:** None  
**Description:** Declines a guild invitation.

## Vendor & Trainer Functions

### GetVendorNumItems
**Parameters:** None  
**Returns:** number - The number of items available from the vendor  
**Description:** Returns the number of items available from the currently interacted vendor.

### GetVendorItemInfo
**Parameters:** `slot` (number) - The slot index of the vendor item  
**Returns:** Item, string, number, number, number, boolean - The item, icon, price, quantity, available quantity, and usability  
**Description:** Returns information about the vendor item at the specified slot.

### BuyVendorItem
**Parameters:** `slot` (number) - The slot index of the vendor item  
**Returns:** None  
**Description:** Buys the vendor item at the specified slot.

### CloseVendor
**Parameters:** None  
**Returns:** None  
**Description:** Closes the vendor window.

### GetNumTrainerSpells
**Parameters:** None  
**Returns:** number - The number of spells available from the trainer  
**Description:** Returns the number of spells available from the currently interacted trainer.

### GetTrainerSpellInfo
**Parameters:** `slot` (number) - The slot index of the trainer spell  
**Returns:** number, string, string, number, boolean - The spell ID, name, icon, price, and known status  
**Description:** Returns information about the trainer spell at the specified slot.

### BuyTrainerSpell
**Parameters:** `slot` (number) - The slot index of the trainer spell  
**Returns:** None  
**Description:** Buys the trainer spell at the specified slot.

### CloseTrainer
**Parameters:** None  
**Returns:** None  
**Description:** Closes the trainer window.

## Character Creation Functions

### CreateCharacter
**Parameters:** `name` (string) - The name of the character to create  
**Returns:** None  
**Description:** Creates a character with the specified name and current customization.

### SetCharCustomizeFrame
**Parameters:** `frame` (Frame) - The frame to use for character customization  
**Returns:** None  
**Description:** Sets the frame to use for character customization.

### SetCharacterClass
**Parameters:** `classId` (number) - The class ID  
**Returns:** None  
**Description:** Sets the class of the character being created.

### SetCharacterGender
**Parameters:** `genderId` (number) - The gender ID  
**Returns:** None  
**Description:** Sets the gender of the character being created.

### SetCharacterRace
**Parameters:** `raceId` (number) - The race ID  
**Returns:** None  
**Description:** Sets the race of the character being created.

### GetCharacterRace
**Parameters:** None  
**Returns:** number - The race ID of the character being created  
**Description:** Returns the race ID of the character being created.

### GetCharacterGender
**Parameters:** None  
**Returns:** number - The gender ID of the character being created  
**Description:** Returns the gender ID of the character being created.

### GetCharacterClass
**Parameters:** None  
**Returns:** number - The class ID of the character being created  
**Description:** Returns the class ID of the character being created.

### ResetCharCustomize
**Parameters:** None  
**Returns:** None  
**Description:** Resets the character customization.

### GetCustomizationValue
**Parameters:** `name` (string) - The name of the customization property  
**Returns:** string - The value of the customization property  
**Description:** Returns the value of the specified customization property.

### CycleCustomizationProperty
**Parameters:** `property` (string) - The name of the customization property, `forward` (boolean) - True to cycle forward, false to cycle backward  
**Returns:** None  
**Description:** Cycles the specified customization property in the specified direction.

### GetNumCustomizationProperties
**Parameters:** None  
**Returns:** number - The number of customization properties  
**Description:** Returns the number of customization properties available.

### GetCustomizationProperty
**Parameters:** `index` (number) - The index of the customization property  
**Returns:** string - The name of the customization property, or nil if not found  
**Description:** Returns the name of the customization property at the specified index.

## Character Selection Functions

### SetCharSelectModelFrame
**Parameters:** `frame` (Frame) - The frame to use for character selection models  
**Returns:** None  
**Description:** Sets the frame to use for character selection models.

### GetNumCharacters
**Parameters:** None  
**Returns:** number - The number of characters on the account  
**Description:** Returns the number of characters on the account.

### GetCharacterInfo
**Parameters:** `index` (number) - The index of the character  
**Returns:** CharacterView - The character view, or nil if not found  
**Description:** Returns the character view for the character at the specified index.

### SelectCharacter
**Parameters:** `index` (number) - The index of the character to select  
**Returns:** None  
**Description:** Selects the character at the specified index.

## Loot Functions

### GetNumLootItems
**Parameters:** None  
**Returns:** number - The number of items in the loot window  
**Description:** Returns the number of items in the loot window.

### LootSlot
**Parameters:** `slot` (number) - The slot index of the loot item, `force` (boolean) - Whether to force looting  
**Returns:** None  
**Description:** Loots the item at the specified slot.

### LootSlotIsCoin
**Parameters:** `slot` (number) - The slot index to check  
**Returns:** boolean - True if the slot contains coins, false otherwise  
**Description:** Checks if the specified loot slot contains coins.

### LootSlotIsItem
**Parameters:** `slot` (number) - The slot index to check  
**Returns:** boolean - True if the slot contains an item, false otherwise  
**Description:** Checks if the specified loot slot contains an item.

### GetLootSlotItem
**Parameters:** `slot` (number) - The slot index of the loot item  
**Returns:** Item - The item, or nil if not found  
**Description:** Returns the item at the specified loot slot.

### CloseLoot
**Parameters:** None  
**Returns:** None  
**Description:** Closes the loot window.

### GetLootSlotInfo
**Parameters:** `slot` (number) - The slot index of the loot item  
**Returns:** string, string, number - The icon, text, and count  
**Description:** Returns information about the loot item at the specified slot.

## Utility Functions

### UseActionButton
**Parameters:** `slot` (number) - The action bar slot index  
**Returns:** None  
**Description:** Uses the action button at the specified slot.

### PickupActionButton
**Parameters:** `slot` (number) - The action bar slot index  
**Returns:** None  
**Description:** Picks up the action button at the specified slot.

### IsActionButtonUsable
**Parameters:** `slot` (number) - The action bar slot index  
**Returns:** boolean - True if the action button is usable, false otherwise  
**Description:** Checks if the action button at the specified slot is usable.

### IsActionButtonItem
**Parameters:** `slot` (number) - The action bar slot index  
**Returns:** boolean - True if the action button is an item, false otherwise  
**Description:** Checks if the action button at the specified slot is an item.

### IsActionButtonSpell
**Parameters:** `slot` (number) - The action bar slot index  
**Returns:** boolean - True if the action button is a spell, false otherwise  
**Description:** Checks if the action button at the specified slot is a spell.

### GetActionButtonSpell
**Parameters:** `slot` (number) - The action bar slot index  
**Returns:** Spell - The spell entry, or nil if not found  
**Description:** Returns the spell entry for the spell at the specified action button slot.

### GetActionButtonItem
**Parameters:** `slot` (number) - The action bar slot index  
**Returns:** Item - The item, or nil if not found  
**Description:** Returns the item at the specified action button slot.

## Global Objects

Several global objects are available for use in Lua scripts:

### loginConnector
**Type:** LoginConnector  
**Description:** Access to login server connectivity functions.

### realmConnector
**Type:** RealmConnector  
**Description:** Access to realm server connectivity functions.

### loginState
**Type:** LoginState  
**Description:** Access to login state functions.

### gameData
**Type:** Project  
**Description:** Access to game data such as spells and models.

## Object Classes

### RealmData
- **id** (number): The realm ID
- **name** (string): The realm name

### CharacterView
- **guid** (number): The character's GUID
- **name** (string): The character's name
- **level** (number): The character's level
- **dead** (boolean): Whether the character is dead
- **raceId** (number): The character's race ID
- **classId** (number): The character's class ID
- **map** (number): The map ID where the character is located

### LoginConnector
- **GetRealms()**: Returns an iterator over the available realms
- **IsConnected()**: Returns whether the client is connected to the login server

### Project
- **spells** (SpellManager): The spell manager
- **models** (ModelManager): The model manager

### SpellManager
- **GetById(id)**: Returns the spell entry with the specified ID

### RealmConnector
- **ConnectToRealm(realmId)**: Connects to the specified realm
- **IsConnected()**: Returns whether the client is connected to the realm server
- **GetRealmName()**: Returns the name of the connected realm
- **DeleteCharacter(guid)**: Deletes the character with the specified GUID

### LoginState
- **EnterWorld()**: Enters the world with the selected character

### Item
- **id** (number): The item ID
- **name** (string): The item name
- **description** (string): The item description
- **quality** (number): The item quality
- **armor** (number): The item's armor value
- **block** (number): The item's block value
- **minDamage** (number): The item's minimum damage
- **maxDamage** (number): The item's maximum damage
- **dps** (number): The item's damage per second
- **attackTime** (number): The item's attack time
- **bagSlots** (number): The number of slots if the item is a bag
- **maxDurability** (number): The item's maximum durability
- **class** (string): The item's class name
- **subClass** (string): The item's subclass name
- **inventoryType** (string): The item's inventory type name
- **GetIcon()**: Returns the item's icon path
- **sellPrice** (number): The item's sell price
- **attackSpeed** (number): The item's attack speed
- **GetStatType(index)**: Returns the type of the stat at the specified index
- **GetStatValue(index)**: Returns the value of the stat at the specified index
- **GetSpellId(index)**: Returns the ID of the spell at the specified index
- **GetSpellTriggerType(index)**: Returns the trigger type of the spell at the specified index

### QuestListEntry
- **id** (number): The quest ID
- **title** (string): The quest title
- **icon** (string): The quest icon
- **isActive** (boolean): Whether the quest is active

### Quest
- **id** (number): The quest ID
- **title** (string): The quest title
- **rewardMoney** (number): The quest's money reward

### QuestLogEntry
- **id** (number): The quest ID
- **quest** (Quest): The quest info
- **status** (number): The quest status

### GossipMenuAction
- **id** (number): The action ID
- **text** (string): The action text
- **icon** (string): The action icon

### QuestDetails
- **id** (number): The quest ID
- **title** (string): The quest title
- **details** (string): The quest details
- **objectives** (string): The quest objectives
- **offerReward** (string): The quest's offer reward text
- **requestItems** (string): The quest's request items text
- **rewardedXp** (number): The quest's XP reward
- **rewardedMoney** (number): The quest's money reward

### UnitHandle
- **GetHealth()**: Returns the unit's current health
- **GetMaxHealth()**: Returns the unit's maximum health
- **GetPower()**: Returns the unit's current power
- **GetMaxPower()**: Returns the unit's maximum power
- **GetLevel()**: Returns the unit's level
- **GetAuraCount()**: Returns the number of auras on the unit
- **GetAura(index)**: Returns the aura at the specified index
- **GetName()**: Returns the unit's name
- **GetPowerType()**: Returns the unit's power type
- **GetMinDamage()**: Returns the unit's minimum damage
- **GetMaxDamage()**: Returns the unit's maximum damage
- **GetAttackTime()**: Returns the unit's attack time
- **GetAttackPower()**: Returns the unit's attack power
- **GetArmor()**: Returns the unit's armor
- **GetAvailableAttributePoints()**: Returns the number of attribute points available
- **GetArmorReductionFactor()**: Returns the unit's armor reduction factor
- **GetStat(index)**: Returns the value of the specified stat
- **GetPosStat(index)**: Returns the positive modifier of the specified stat
- **GetNegStat(index)**: Returns the negative modifier of the specified stat
- **IsAlive()**: Returns whether the unit is alive
- **GetType()**: Returns the unit's type
- **IsFriendly()**: Returns whether the unit is friendly
- **IsHostile()**: Returns whether the unit is hostile

### AuraHandle
- **IsExpired()**: Returns whether the aura has expired
- **CanExpire()**: Returns whether the aura can expire
- **GetDuration()**: Returns the aura's duration
- **GetSpell()**: Returns the aura's spell entry

### ItemHandle
- **GetName()**: Returns the item's name
- **GetDescription()**: Returns the item's description
- **GetClass()**: Returns the item's class
- **GetInventoryType()**: Returns the item's inventory type
- **GetSubClass()**: Returns the item's subclass
- **GetStackCount()**: Returns the item's stack count
- **GetBagSlots()**: Returns the number of slots if the item is a bag
- **GetMinDamage()**: Returns the item's minimum damage
- **GetMaxDamage()**: Returns the item's maximum damage
- **GetAttackSpeed()**: Returns the item's attack speed
- **GetDps()**: Returns the item's damage per second
- **GetQuality()**: Returns the item's quality
- **GetArmor()**: Returns the item's armor value
- **GetBlock()**: Returns the item's block value
- **GetDurability()**: Returns the item's current durability
- **GetMaxDurability()**: Returns the item's maximum durability
- **GetSellPrice()**: Returns the item's sell price
- **GetIcon()**: Returns the item's icon path
- **GetEntry()**: Returns the item's entry ID
- **GetSpell(index)**: Returns the spell at the specified index
- **GetSpellTriggerType(index)**: Returns the trigger type of the spell at the specified index
- **GetStatType(index)**: Returns the type of the stat at the specified index
- **GetStatValue(index)**: Returns the value of the stat at the specified index

### Spell
- **id** (number): The spell ID
- **name** (string): The spell name
- **rank** (string): The spell rank
- **cost** (number): The spell cost
- **cooldown** (number): The spell cooldown
- **powertype** (number): The spell's power type
- **level** (number): The spell level
- **casttime** (number): The spell cast time
- **icon** (string): The spell icon

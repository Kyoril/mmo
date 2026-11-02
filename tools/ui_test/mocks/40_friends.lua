-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- Friends list (FriendClient). GetFriendInfo is 0-based like the client and returns a table with
-- the FriendInfo fields. Change UiTest.data.friends and fire FRIEND_LIST_UPDATE to refresh the UI.

UiTest.data.friends = {
	{ name = "Brynja", level = 14, className = "Cleric", raceName = "Human", online = true },
	{ name = "Corwin", level = 9, className = "Scout", raceName = "Human", online = true },
	{ name = "Elowen", level = 20, className = "Mage", raceName = "Human", online = false },
	{ name = "Thorgar", level = 11, className = "Warrior", raceName = "Human", online = false },
	{ name = "Isolde", level = 17, className = "Acolyte", raceName = "Human", online = true },
}

UiTest.data.pendingFriendInviter = ""

function GetNumFriends()
	return #UiTest.data.friends
end

function GetFriendInfo(index)
	return UiTest.data.friends[index + 1]
end

function IsFriend(name)
	for _, friend in ipairs(UiTest.data.friends) do
		if friend.name == name then
			return true
		end
	end
	return false
end

function GetPendingInviter()
	return UiTest.data.pendingFriendInviter
end

function RequestFriendList()
	UiTest.FireEvent("FRIEND_LIST_UPDATE")
end

function FriendInviteByName(name)
	print("FriendInviteByName(" .. tostring(name) .. ")")
end

function RemoveFriendByName(name)
	for index, friend in ipairs(UiTest.data.friends) do
		if friend.name == name then
			table.remove(UiTest.data.friends, index)
			UiTest.FireEvent("FRIEND_LIST_UPDATE")
			return
		end
	end
end

function AcceptFriend()
	UiTest.data.pendingFriendInviter = ""
end

function DeclineFriend()
	UiTest.data.pendingFriendInviter = ""
end

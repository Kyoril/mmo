-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- Party (PartyInfo). No party by default; a scenario fills UiTest.data.party, puts the members
-- into UiTest.data.units.party1..4 and fires PARTY_MEMBERS_CHANGED. Member indices are 1-based,
-- like the unit names; leader is the member index of the leader, or 0 when the player leads.

UiTest.data.party = { size = 0, leader = 0 }

function GetPartySize()
	return UiTest.data.party.size
end

function HasPartyMember(index)
	return index >= 1 and index <= UiTest.data.party.size
end

function GetPartyLeaderIndex()
	return UiTest.data.party.leader
end

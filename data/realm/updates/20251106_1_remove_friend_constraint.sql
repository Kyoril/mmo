-- Remove bidirectional friendship constraint to support one-sided friendships
-- In one-sided friendship system, character_id adds friend_id without automatic reciprocal relationship
-- The CHECK constraint enforcing character_id < friend_id must be removed

ALTER TABLE `friend_list` DROP CHECK `friend_list_chk_1`;

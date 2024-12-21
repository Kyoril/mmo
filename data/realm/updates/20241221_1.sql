/* Remove all character actions first. */
DELETE FROM character_actions;

/* For each spell a character knows, add this spell to the character action bar. */
INSERT INTO character_actions (character_id, button, action, type)
SELECT 
    character_spells.character AS character_id,
    ROW_NUMBER() OVER (PARTITION BY character_spells.character ORDER BY character_spells.spell) - 1 AS button,
    character_spells.spell AS action,
    1 AS type
FROM 
    character_spells;
    
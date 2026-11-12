-- Bind Shadow (spell 301) is no longer learned: Malrik Duskwright casts it on the player at the end
-- of the acolyte's shadow binding quest line. Characters who bought it from the acolyte trainer
-- keep a spell that is useless after one cast; remove it. Their bound shade, Summon Shadow and
-- Dismiss Shadow stay. A rolled back build only offers the spell at the trainer again.
DELETE FROM `character_spells` WHERE `spell` = 301;

-- Multi-Class System cleanup: drop the legacy character-wide attribute columns.
--
-- Per-class attribute spending now lives entirely in `character_classes` (see
-- 20260623_1_multi_class.sql, which backfilled it from these columns). The realm no longer reads or
-- writes `characters.attr_0..attr_4`, so they can be removed.
--
-- Must run after 20260623_1_multi_class.sql (which still reads these columns for the backfill);
-- migration order is by filename, so the `_3_` ordinal guarantees this.

ALTER TABLE `characters`
  DROP COLUMN `attr_0`,
  DROP COLUMN `attr_1`,
  DROP COLUMN `attr_2`,
  DROP COLUMN `attr_3`,
  DROP COLUMN `attr_4`;

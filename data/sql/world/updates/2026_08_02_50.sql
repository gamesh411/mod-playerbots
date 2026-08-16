-- DEC-032: the Glyph of Eternal Water elemental (37994) is missing Freeze (33395) in
-- creature_template_spell; the unglyphed temporary elemental (510) carries it at Index 1.
-- On retail the glyphed permanent pet keeps the same pet-bar kit, so mirror the row.
-- Without it no pet path (PetSpellMap, template spells, CreatureSpellData, levelup map)
-- can ever resolve Freeze for glyphed mages - bots and players alike.
DELETE FROM `creature_template_spell` WHERE `CreatureID` = 37994 AND `Index` = 1;
INSERT INTO `creature_template_spell` (`CreatureID`, `Index`, `Spell`, `VerifiedBuild`) VALUES (37994, 1, 33395, 12340);

"""Port of HeuristicScores::FillActionFlags / CombatDecisionUtil (DEC-017 flags)."""

from __future__ import annotations


def _has_any(n: str, needles: tuple[str, ...]) -> bool:
    return any(s in n for s in needles)


def is_meta_action(name: str) -> bool:
    n = name.lower()
    return _has_any(
        n,
        (
            "set facing", "reach melee", "reach spell", "check mount", "check objective", "reset objective",
            "move to objective", "move to start", "move to", "xp gain", "drop target", "dps assist",
            "apply oil", "apply stone", "auto release", "self resurrect", "follow", "food", "drink",
            "duel_start", "unstealth", "set behind", "set pet", "toggle pet", "cast greater blessing assignment",
            "select new target", "update strategy", "chat", "emote", "rpg ", "travel", "grind", "loot",
            "add all loot", "equip", "use stone", "use oil", "wait for", "guard", "stay", "follow master",
            "ml duel bracket", "accept duel", "activate primary spec", "activate secondary spec",
        ),
    )


def is_interrupt_action(name: str) -> bool:
    n = name.lower()
    return _has_any(
        n,
        (
            "kick", "pummel", "counterspell", "mind freeze", "wind shear", "spell lock", "shield bash",
            "strangulate", "silencing shot", "arcane torrent", "deadly throw", "gouge", "silence", "bash",
        ),
    )


def is_enemy_healer_action(name: str) -> bool:
    n = name.lower()
    return _has_any(n, ("on enemy healer", "enemy healer"))


def is_defensive_action(name: str) -> bool:
    n = name.lower()
    return _has_any(
        n,
        (
            "ice block", "divine shield", "divine protection", "barkskin", "survival instincts",
            "shield wall", "last stand", "cloak of shadows", "dispersion", "pain suppression",
            "hand of protection", "blessing of protection", "deterrence", "die by the sword",
            "anti-magic shell", "icebound fortitude", "shield block", "feign death", "vanish",
            "fade", "hand of sacrifice", "blessing of sacrifice", "guardian spirit",
        ),
    )


def is_crowd_control_action(name: str) -> bool:
    n = name.lower()
    if "death coil" in n:
        return False
    return _has_any(
        n,
        (
            "polymorph", "fear", "hammer of justice", "repentance", "blind", "hex", "cyclone", "sap",
            "freezing trap", "wyvern sting", "scatter shot", "banish", "seduction", "hibernate",
            "shackle", "turn evil", "scare beast", "psychic scream", "howl of terror", "cheap shot",
            "kidney shot", "deep freeze", "frost nova", "entangling roots", "nature's grasp",
        ),
    )


def is_heal_action(name: str) -> bool:
    n = name.lower()
    return _has_any(
        n,
        (
            "heal", "flash", "renew", "rejuvenation", "regrowth", "nourish", "holy light",
            "flash of light", "lay on hands", "chain heal", "riptide", "healing wave",
            "lesser healing wave", "penance", "circle of healing", "prayer of mending",
            "prayer of healing", "binding heal", "wild growth", "lifebloom", "holy shock",
            "gift of the naaru", "bandage",
        ),
    )


def is_damage_action(name: str) -> bool:
    if is_heal_action(name) or is_defensive_action(name) or is_crowd_control_action(name) or is_meta_action(name):
        return False
    n = name.lower()
    return _has_any(
        n,
        (
            "attack", "strike", "shot", "bolt", "fireball", "frostbolt", "shadow bolt", "smite",
            "wrath", "starfire", "lava", "chaos", "arcane blast", "arcane missiles", "mind blast",
            "mind flay", "corruption", "immolate", "incinerate", "conflagrate", "haunt", "unstable affliction",
            "serpent sting", "steady shot", "aimed shot", "multi-shot", "chimera", "explosive shot",
            "mortal strike", "heroic strike", "slam", "execute", "bloodthirst", "whirlwind",
            "sinister", "eviscerate", "envenom", "mutilate", "backstab", "hemorrhage",
            "crusader", "judgement", "consecration", "exorcism", "hammer of wrath",
            "lightning bolt", "earth shock", "flame shock", "lava burst", "stormstrike",
            "icy touch", "plague strike", "death coil", "death strike", "heart strike", "scourge strike",
            "obliterate", "frost strike", "mangle", "shred", "rip", "rake", "ferocious bite",
            "swipe", "claw", "melee", "auto shot", "moonfire", "insect swarm", "holy fire",
            "shadow word", "devouring plague", "vampiric touch",
        ),
    )


def is_focus_player_action(name: str) -> bool:
    n = name.lower()
    return _has_any(
        n,
        ("attack enemy player", "attack enemy flag carrier", "on enemy player", "enemy flag carrier"),
    )


def is_instant_preferred_action(name: str) -> bool:
    return (
        is_interrupt_action(name)
        or is_defensive_action(name)
        or is_crowd_control_action(name)
        or _has_any(name.lower(), ("trinket", "vanish", "shadowstep", "blink", "disengage", "gift of the naaru"))
    )


def fill_action_flags(name: str) -> list[float]:
    return [
        1.0 if is_interrupt_action(name) else 0.0,
        1.0 if is_enemy_healer_action(name) else 0.0,
        1.0 if is_defensive_action(name) else 0.0,
        1.0 if is_crowd_control_action(name) else 0.0,
        1.0 if is_heal_action(name) else 0.0,
        1.0 if is_instant_preferred_action(name) else 0.0,
        1.0 if is_damage_action(name) else 0.0,
        1.0 if is_focus_player_action(name) else 0.0,
    ]

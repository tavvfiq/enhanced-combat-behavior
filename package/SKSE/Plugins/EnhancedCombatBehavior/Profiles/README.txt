Enhanced Combat Behavior - personality profiles

Every .json file in this folder is read (in file-name order), so other mods can ship their own. Profiles set
how groups of NPCs fight, replacing the risk, caution and patience an NPC gets from its Confidence, Aggression
and combat style. Needs [DecisionMatrix] EnablePersonality = true in EnhancedCombatBehavior.ini.

A file is a plain JSON list (no comments: JSON has none) of entries:

  { "match": "<what>", "risk": 0-1, "caution": 0-1, "patience": 0-1, "note": "<your label>" }

  risk      forward dodges, sprint attacks, trading blows
  caution   backing off, parry / timed block, close dodges
  patience  feints
  note      optional text for yourself; ignored

Leave a trait out to keep the NPC's own. "match" is a keyword EditorID (vanilla, or one added with KID / SPID),
or a form as "0xFormID~Plugin.esp": a keyword, a faction, a race or an NPC. Factions, races and NPCs by EditorID
only work with EditorIDs kept in memory (powerofthree's Tweaks "Load EditorIDs"). Where several entries match an
NPC, the later one wins per trait. PersonalityJitter (INI) is still added, so NPCs of one group differ a little.
The log lists every entry it used and warns about the ones it can't. The in-game menu's "Reload from INI" reads
these files again.

Example (save as e.g. MyProfiles.json in this folder):

[
    { "match": "0x1BCC0~Skyrim.esm", "risk": 0.85, "caution": 0.2, "note": "bandits (BanditFaction): reckless" },
    { "match": "0x39F26~Skyrim.esm", "risk": 0.4, "caution": 0.8, "patience": 0.9, "note": "Thalmor (ThalmorFaction)" },
    { "match": "ActorTypeUndead", "patience": 0.9, "note": "draugr and other undead" }
]

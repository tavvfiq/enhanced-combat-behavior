# Decisions

Design choices that are not obvious from the code, with the reason.

## Dodge stays an animation event, not a tree leaf (2026-10-25)
The brain triggers the installed dodge mod's graph event (TK Dodge `TKDodge*`) directly from the executor, as now. No custom
tree leaf. A leaf would send the same event later (tree thread, queued to the main thread) on the most
reaction-critical action, and would lose the synchronous accepted/rejected result that feeds cooldowns and
stamina. Its only gain, holding the tree so vanilla movement or attacks can't start mid-dodge, has not been
seen in any run. Revisit if a log or video shows movement or an attack fighting a dodge (TREE_DESIGN §6).

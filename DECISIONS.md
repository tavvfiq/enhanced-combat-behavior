# Decisions

Design choices that are not obvious from the code, with the reason.

## Dodge stays an animation event, not a tree leaf (2026-10-25)
The brain triggers the installed dodge mod's graph event (TK Dodge `TKDodge*`) directly from the executor, as now. No custom
tree leaf. A leaf would send the same event later (tree thread, queued to the main thread) on the most
reaction-critical action, and would lose the synchronous accepted/rejected result that feeds cooldowns and
stamina. Its only gain, holding the tree so vanilla movement or attacks can't start mid-dodge, has not been
seen in any run. Revisit if a log or video shows movement or an attack fighting a dodge (TREE_DESIGN §6).

## Blocking without the Block tree goes through a custom tree node, not the executor (2026-10-25)
NPCs with a weapon and a spell, torch or second weapon never run vanilla's Block tree, but their graph blocks on
ActorState wantBlocking + blockStart (Simple Dual Wield Block's method; run 34 probe). The block is a DualBlock node
(an Idle node with a copied vtable, TREE_DESIGN 6) placed first in the Melee and Magic trees, not events sent from the
executor. While the node runs it holds that hand's tree, so vanilla can't cast or swing with it mid-block, and it
releases in its own Exit. The executor route was smaller but left vanilla's attack branch and left-hand casting free
to break the block. Dodge stays an executor event (see above): it is reaction-critical and needs the synchronous result.

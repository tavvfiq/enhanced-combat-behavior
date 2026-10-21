# Lesson candidates (not promoted)

## NATIVE: CombatBehaviorTreeNode::AddChildren adds the top ancestor, not the node (2026-10-22)
- Evidence: SE 46305 +0x7F..+0x9A disassembly (`while (n->parent) n = n->parent;` then stores n, sets n->parent = this).
- Symptom when ignored: passing an attached node (vanilla Movement Selector under Repeat > Context) made the
  tree root a child of its own descendant: a cycle, and the game hung as soon as a hostile NPC entered combat.
- Rule: detach (parent = nullptr) before AddChildren / initChildrensParents. The RandomNode factory relies on this
  behaviour (it returns the leaf and AddChildren climbs to the wrapper).
- Status: assistant-claimed root cause from static evidence; runtime confirmation pending (0.2.1 retest).
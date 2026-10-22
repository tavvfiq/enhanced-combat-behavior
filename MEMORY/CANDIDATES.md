# Lesson candidates (not promoted)

## NATIVE: CombatBehaviorTreeNode::AddChildren adds the top ancestor, not the node (2026-10-22)
- Evidence: SE 46305 +0x7F..+0x9A disassembly (`while (n->parent) n = n->parent;` then stores n, sets n->parent = this).
- Symptom when ignored: passing an attached node (vanilla Movement Selector under Repeat > Context) made the
  tree root a child of its own descendant: a cycle, and the game hung as soon as a hostile NPC entered combat.
- Rule: detach (parent = nullptr) before AddChildren / initChildrensParents. The RandomNode factory relies on this
  behaviour (it returns the leaf and AddChildren climbs to the wrapper).
- Status: runtime-evidenced - 0.2.1 (detached) ran 2.5 min against camp bandits with no hang.
## Combat tree node slots: trust Thread::Update, not CPR names (tool-validated, 2026-10-22)
CombatBehaviorThread::Update 46228/47483 calls 2 = Enter (descend), 4 = Update each running tick, 5 = Abort (failState 2), 3 = Exit on the node that just ascended. CPR's names (pop/on_childfailed/on_interrupted) misled an earlier correction. To stop a running leaf early: call thread->Ascend() from its slot 4. Movement leaves' own Abort does exactly that, and CPR's AdvanceInterruptHook does the same.

## Hook thunks must mirror every argument the callee reads (2026-10-22, evidence: 0.8.4 CTD)
- A call-site thunk that declares fewer args than the callee reads can work for years by luck (the register still holds the caller's value), then crash the day any code runs before the forwarded call. CircleAngle(self) vs get_CircleAngle(self, target): latent 0.2.4, CTD in 0.8.4.
- Before writing a thunk, read the callee head for argument registers read before written (rcx/rdx/r8/r9, xmm0-3) and check the call site's setup; record the signature in a comment.
- Status: candidate (runtime-evidenced crash + static proof).

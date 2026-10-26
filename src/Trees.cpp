#include "Trees.h"
#include "Config.h"
#include "Intents.h"
#include "Logger.h"
#include "TreeNodes.h"

#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace CombatAI::Trees
{
    namespace
    {
        using namespace TreeNodes;

        // Vanilla tree builders we replace (they also confirm the IDs: our emulation of the SE
        // trees and the 1.7.104 bodies match Fenix-NG's).
        constexpr REL::RelocationID kMeleeTree{48151, 49182};
        constexpr REL::RelocationID kBlockTree{46640, 47843};
        // The vanilla CloseMovement builder is kept; we hook its final SetRoot call and splice our
        // movement branches in above its Movement Selector (one E8 call in each build).
        constexpr REL::RelocationID kCloseMovementTree{46731, 47928};
        constexpr std::size_t kCloseMovementSetRootSE = 0x512;
        constexpr std::size_t kCloseMovementSetRootAE = 0xE9C;
        // Circle node's call to its angle getter (one of two callers; the other is CircleDistant).
        // The angle's sign picks the side (Fenix-NG: positive = left).
        constexpr REL::RelocationID kCircleUpdate{46720, 47916};
        constexpr std::size_t kCircleAngleCallSE = 0x299;
        constexpr std::size_t kCircleAngleCallAE = 0x22B;
        constexpr REL::RelocationID kCircleAngle{49721, 50648};
        // RangedMovement (archers and mages: the Movement selector picks it when the NPC's max range > 1024) is kept;
        // its SetRoot call is hooked to add our movement branches like CloseMovement's (SE 48476+0x495,
        // AE 49468+0x957 in 1.6.1170 and 1.7.104, RE 2026-10-26)
        constexpr REL::RelocationID kRangedMovementTree{48476, 49468};
        constexpr std::size_t kRangedMovementSetRootSE = 0x495;
        constexpr std::size_t kRangedMovementSetRootAE = 0x957;
        // The vanilla Ranged builder (SE 32398 / AE 33149) stores the ranged attack chance function (SE 49772 / AE
        // 50700) in its RandomNode with a `lea rax, [rip+x]` (SE +0xA3, AE +0x111 in 1.6.1170 and 1.7.104). The lea
        // is pointed at our RangedShotChance before the tree is built, so the roll is vanilla's times the brain's
        // factor (RE 2026-10-26).
        constexpr REL::RelocationID kRangedTree{32398, 33149};
        constexpr std::size_t kRangedChanceLeaSE = 0xA3;
        constexpr std::size_t kRangedChanceLeaAE = 0x111;
        using ChanceFn = float (*)(RE::Actor *, RE::Actor *);
        ChanceFn g_vanillaRangedChance = nullptr;

        float RangedShotChance(RE::Actor *a_me, RE::Actor *a_target)
        {
            const float vanilla = g_vanillaRangedChance(a_me, a_target);
            return a_me ? std::clamp(vanilla * Intents::RangedShotFactor(a_me), 0.0f, 1.0f) : vanilla;
        }

        void InstallRangedShotChance(SKSE::Trampoline &a_trampoline)
        {
            const auto lea = kRangedTree.address() + (REL::Module::IsAE() ? kRangedChanceLeaAE : kRangedChanceLeaSE);
            const auto *b = reinterpret_cast<const std::uint8_t *>(lea);
            if (b[0] != 0x48 || b[1] != 0x8D || b[2] != 0x05) {
                LOG_ERROR("Tree: Ranged tree chance site does not match - archers keep vanilla shot timing");
                return;
            }
            const auto next = lea + 7;
            g_vanillaRangedChance = reinterpret_cast<ChanceFn>(next + *reinterpret_cast<const std::int32_t *>(lea + 3));
            // Our function may be out of rel32 reach: jump through a stub in the trampoline (near the game)
            auto *stub = static_cast<std::uint8_t *>(a_trampoline.allocate(14));
            const std::uint8_t jmp[6] = {0xFF, 0x25, 0, 0, 0, 0}; // jmp [rip+0]
            std::memcpy(stub, jmp, 6);
            const auto target = reinterpret_cast<std::uintptr_t>(&RangedShotChance);
            std::memcpy(stub + 6, &target, 8);
            const auto disp = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(stub)) - static_cast<std::int64_t>(next);
            if (disp < INT32_MIN || disp > INT32_MAX) {
                LOG_ERROR("Tree: Ranged chance stub out of reach - archers keep vanilla shot timing");
                return;
            }
            REL::safe_write(lea + 3, static_cast<std::int32_t>(disp));
            LOG_INFO("Tree: Ranged attack chance redirected (the brain times archers' shots)");
        }

        // The vanilla Magic builder is kept; its SetRoot call is hooked to add the DualBlock branch (SE 48065+0x4AC,
        // AE 49112+0x7AC in 1.6.1170 and 1.7.104, RE 2026-10-25).
        constexpr REL::RelocationID kMagicTree{48065, 49112};
        constexpr std::size_t kMagicSetRootSE = 0x4AC;
        constexpr std::size_t kMagicSetRootAE = 0x7AC;

        // True if the instruction at a_site is a rel32 call to a_target (checked before patching).
        bool CallsTarget(std::uintptr_t a_site, std::uintptr_t a_target)
        {
            const auto *bytes = reinterpret_cast<const std::uint8_t *>(a_site);
            return bytes[0] == 0xE8 && a_site + 5 + *reinterpret_cast<const std::int32_t *>(a_site + 1) == a_target;
        }

        // Vanilla chance functions the stock RandomNodes roll (SE->AE pairs from the two tree
        // builders; every pair is +927, like the attack-chance pair 49748/50675).
        constexpr REL::RelocationID kAttackChance{49747, 50674};       // f(attacker, target)
        constexpr REL::RelocationID kSpecialAttackChance{49749, 50676}; // f(attacker)
        constexpr REL::RelocationID kBlockAttackChance{49750, 50677};  // f(attacker)
        constexpr REL::RelocationID kBlockChance{49751, 50678};        // f(attacker)
        constexpr REL::RelocationID kBashChance{49755, 50682};         // f(attacker, target)
        constexpr REL::RelocationID kOffensiveBashChance{49756, 50683}; // f(attacker, target)

        // Idle hold. Vanilla holds 0.25 s, which is also how often the tree re-reads anything,
        // so a timed block could start up to 0.25 s late. We re-read every 0.05 s and rescale
        // vanilla's per-roll chances so the per-second rate without intents stays the same.
        constexpr float kVanillaHold = 0.25f;
        constexpr float kHold = 0.05f;

        float Rescaled(float a_chance)
        {
            const float p = std::clamp(a_chance, 0.0f, 1.0f);
            return 1.0f - std::pow(1.0f - p, kHold / kVanillaHold);
        }

        float Vanilla2(const REL::RelocationID &a_id, RE::Actor *a_me, RE::Actor *a_target)
        {
            REL::Relocation<float (*)(RE::Actor *, RE::Actor *)> f{a_id};
            return f(a_me, a_target);
        }

        float Vanilla1(const REL::RelocationID &a_id, RE::Actor *a_me)
        {
            REL::Relocation<float (*)(RE::Actor *)> f{a_id};
            return f(a_me);
        }

        // ---- RandomNode probabilities: float(attacker, target), run on the tree thread ----

        float IntentAttack(RE::Actor *a_me, RE::Actor *)
        {
            return Intents::TakeAttack(a_me) != Intents::Attack::None ? 1.0f : 0.0f;
        }

        // Vanilla branches stay out of the way while the brain has something pending.
        float VanillaAttack(RE::Actor *a_me, RE::Actor *a_target)
        {
            return !a_me || Intents::HasPending(a_me) ? 0.0f : Rescaled(Vanilla2(kAttackChance, a_me, a_target));
        }

        float VanillaSpecialAttack(RE::Actor *a_me, RE::Actor *)
        {
            return !a_me || Intents::HasPending(a_me) ? 0.0f : Rescaled(Vanilla1(kSpecialAttackChance, a_me));
        }

        float IntentBash(RE::Actor *a_me, RE::Actor *)
        {
            return Intents::TakeDefend(a_me, Intents::Defend::Bash) ? 1.0f : 0.0f;
        }

        float IntentBlock(RE::Actor *a_me, RE::Actor *)
        {
            return Intents::TakeDefend(a_me, Intents::Defend::Block) ? 1.0f : 0.0f;
        }

        float VanillaBlockAttack(RE::Actor *a_me, RE::Actor *)
        {
            return !a_me || Intents::HasPending(a_me) ? 0.0f : Rescaled(Vanilla1(kBlockAttackChance, a_me));
        }

        float VanillaOffensiveBash(RE::Actor *a_me, RE::Actor *a_target)
        {
            return !a_me || Intents::HasPending(a_me) ? 0.0f
                                                      : Rescaled(Vanilla2(kOffensiveBashChance, a_me, a_target));
        }

        float VanillaBlock(RE::Actor *a_me, RE::Actor *)
        {
            return !a_me || Intents::HasPending(a_me) ? 0.0f : Rescaled(Vanilla1(kBlockChance, a_me));
        }

        // Post-block counter bash: one roll when a block ends, as in vanilla, so no rescale. A bash
        // the brain asked for while the Block node was holding fires here, as the block ends.
        float VanillaCounterBash(RE::Actor *a_me, RE::Actor *a_target)
        {
            if (!a_me) {
                return 0.0f;
            }
            return Intents::TakeDefend(a_me, Intents::Defend::Bash) ? 1.0f : Vanilla2(kBashChance, a_me, a_target);
        }

        Node *Checked(Node *a_node, const char *a_what)
        {
            if (!a_node) {
                SKSE::stl::report_and_fail(std::format("EnhancedCombatBehavior: could not build tree node '{}'", a_what));
            }
            return a_node;
        }

        // Block (or BlockAttack) followed by vanilla's post-block counter-bash roll.
        Node *BlockThenBash(Node *a_block, const char *a_blockName)
        {
            auto *counter = Checked(RandomNode(Named(Bash(), "Bash"), VanillaCounterBash, "RandomNode - Bash"),
                                    "counter bash");
            return AddChild(Checked(Named(a_block, a_blockName), a_blockName), counter);
        }

        // ---- DualBlock: an Idle node with a copied vtable (TREE_DESIGN 6). Enter starts a direct block (ActorState
        // wantBlocking + blockStart, Simple Dual Wield Block's method) and Exit ends it once the brain's hold is over.
        // Idle's own Enter/Update/Exit keep the frame and the 0.05 s hold, so the selector re-reads like elsewhere.
        // It sits first in the Melee and Magic trees, so while it runs that hand can't swing or cast.
        using NodeFn = void (*)(Node *, RE::CombatBehaviorThread *);
        std::array<void *, 11> g_dualBlockVtable{}; // [0] RTTI, [1..10] = Idle's slots 0-9 (SE and AE: 10 slots)
        NodeFn g_idleEnter = nullptr;
        NodeFn g_idleExit = nullptr;

        void SetWantBlocking(RE::Actor *a_actor, bool a_on)
        {
            if (auto *state = a_actor ? a_actor->AsActorState() : nullptr) {
                state->actorState2.wantBlocking = a_on ? 1 : 0;
            }
        }

        void DualBlockEnter(Node *a_node, RE::CombatBehaviorThread *a_thread)
        {
            auto *actor = RE::CombatBehaviorController::GetCurrentAttacker();
            if (actor && Intents::StartDualBlock(actor)) {
                SetWantBlocking(actor, true);
                const bool started = actor->NotifyAnimationGraph("blockStart");
                if (Config::GetInstance().IsDebugTarget(actor->GetFormID())) {
                    LOG_DEBUG("DualBlock 0x{:08X}: blockStart {}", actor->GetFormID(), started ? "accepted" : "rejected");
                }
            }
            g_idleEnter(a_node, a_thread);
        }

        void DualBlockExit(Node *a_node, RE::CombatBehaviorThread *a_thread)
        {
            g_idleExit(a_node, a_thread);
            if (auto *actor = RE::CombatBehaviorController::GetCurrentAttacker()) {
                ReleaseDualBlock(actor, false);
            }
        }

        Node *DualBlockNode()
        {
            auto *n = Idle(kHold);
            if (n) {
                n->vtable = &g_dualBlockVtable[1];
            }
            return Named(n, "Dual Block");
        }

        float WantDualBlock(RE::Actor *a_me, RE::Actor *) { return Intents::WantsDualBlock(a_me) ? 1.0f : 0.0f; }

        // Hold a cast while the brain says the shot is off (an ally in the line of fire): an Idle branch in front of
        // vanilla's cast selector, re-read every 0.05 s like the others
        float WantHoldCast(RE::Actor *a_me, RE::Actor *) { return a_me && Intents::RangedShotFactor(a_me) <= 0.0f ? 1.0f : 0.0f; }

        Node *HoldCastBranch()
        {
            return Checked(RandomNode(Named(Idle(kHold), "Hold Cast"), WantHoldCast, "Intent - Hold Cast"), "intent hold cast");
        }

        Node *DualBlockBranch()
        {
            return Checked(RandomNode(DualBlockNode(), WantDualBlock, "Intent - Dual Block"), "intent dual block");
        }

        std::atomic<std::uint32_t> g_meleeBuilt{0}, g_blockBuilt{0}, g_magicBuilt{0};

        // Vanilla Melee: Context > Repeat > Selector{ RandomNode-Special Attack, RandomNode-Attack, Idle }.
        // Ours puts the brain's attack first.
        void MeleeTree(void *a_tree)
        {
            auto *selector = Checked(Named(NextSelector(), "Attack Selector"), "Attack Selector");
            SetChildren(selector,
                        {DualBlockBranch(),
                         Checked(RandomNode(Named(Attack(), "attack"), IntentAttack, "Intent - Attack"), "intent attack"),
                         Checked(RandomNode(Named(SpecialAttack(), "Special Attack"), VanillaSpecialAttack,
                                            "RandomNode - Special Attack"),
                                 "special attack"),
                         Checked(RandomNode(Named(Attack(), "attack"), VanillaAttack, "RandomNode - Attack"), "attack"),
                         Checked(Named(Idle(kHold), "Attack Idle"), "Attack Idle")});
            auto *repeat = AddChild(Checked(Named(Repeat(), "Attack Repeat"), "Attack Repeat"), selector);
            auto *context = AddChild(Checked(Named(ContextMelee(), "Melee Context"), "Melee Context"), repeat);
            SetRoot(a_tree, context);
            if (g_meleeBuilt.fetch_add(1) == 0) {
                LOG_INFO("Tree: Melee tree built (intent attack + vanilla branches, re-read every {}s)", kHold);
            }
        }

        // Vanilla Block: Context > Repeat > Selector{ BlockAttack>Bash, Offensive Bash, Block>Bash, Idle }.
        // Ours puts the brain's bash and block first.
        void BlockTree(void *a_tree)
        {
            auto *selector = Checked(Named(NextSelector(), "Block Selector"), "Block Selector");
            SetChildren(
                selector,
                {Checked(RandomNode(Named(Bash(), "Bash"), IntentBash, "Intent - Bash"), "intent bash"),
                 Checked(RandomNode(BlockThenBash(Block(), "Block"), IntentBlock, "Intent - Block"), "intent block"),
                 Checked(RandomNode(BlockThenBash(BlockAttack(), "Block Attack"), VanillaBlockAttack,
                                    "RandomNode - Block Attack"),
                         "block attack"),
                 Checked(RandomNode(Named(Bash(), "Offensive Bash"), VanillaOffensiveBash, "RandomNode - Offensive Bash"),
                         "offensive bash"),
                 Checked(RandomNode(BlockThenBash(Block(), "Block"), VanillaBlock, "RandomNode - Block"), "block"),
                 Checked(Named(Idle(kHold), "Block Idle"), "Block Idle")});
            auto *repeat = AddChild(Checked(Named(Repeat(), "Block Repeat"), "Block Repeat"), selector);
            auto *context = AddChild(Checked(Named(ContextBlock(), "Block Context"), "Block Context"), repeat);
            SetRoot(a_tree, context);
            if (g_blockBuilt.fetch_add(1) == 0) {
                LOG_INFO("Tree: Block tree built (intent bash/block + vanilla branches, re-read every {}s)", kHold);
            }
        }
        float WantAdvance(RE::Actor *a_me, RE::Actor *) { return Intents::WantsMove(a_me, Intents::Move::Advance) ? 1.0f : 0.0f; }
        float WantCircle(RE::Actor *a_me, RE::Actor *) { return Intents::WantsMove(a_me, Intents::Move::Circle) ? 1.0f : 0.0f; }
        float WantFallback(RE::Actor *a_me, RE::Actor *) { return Intents::WantsMove(a_me, Intents::Move::Fallback) ? 1.0f : 0.0f; }
        float WantBackoff(RE::Actor *a_me, RE::Actor *) { return Intents::WantsMove(a_me, Intents::Move::Backoff) ? 1.0f : 0.0f; }

        std::atomic<std::uint32_t> g_closeBuilt{0}, g_rangedBuilt{0};

        // Circle start audit (selected NPC): what each of the Circle start's checks (46720/47916) answered.
        // 1 = passed, 0 = refused, -1 = not reached; the first 0, or the stage after the last 1, is why it failed.
        struct CircleChecks
        {
            std::int8_t animDriven = -1, facing = -1, angle = -1, viewCone = -1, straightPath = -1, pathCheck = -1;
        };
        thread_local bool t_circleAudit = false;
        thread_local CircleChecks t_circle;
        // Fallback start (46712/47908): after the facing / leaving checks (passed for ours) it refuses when the move is
        // animation-driven, then when the fallback spot is too close (a compare, no call), then on CheckStraightPath.
        // animation-driven 1 and straight path -1 = the distance compare refused (run 45: 23 of 29 failed at start).
        struct FallbackChecks
        {
            std::int8_t animDriven = -1, straightPath = -1;
        };
        thread_local bool t_fallbackAudit = false;
        thread_local FallbackChecks t_fallback;

        bool NoteCheck(std::int8_t &a_slot, bool a_passed)
        {
            if (t_circleAudit) {
                a_slot = a_passed ? 1 : 0;
            }
            return a_passed;
        }

        // Circle on the brain's side: keep vanilla's angle size, set its sign.
        struct CircleAngle
        {
            // get_CircleAngle(self, target) 49721/50648 reads both (rcx, rdx). Until 0.8.4 this thunk took only
            // self and worked by luck (rdx untouched before the call); 0.8.4's audit write clobbered rdx: CTD.
            static float thunk(RE::Actor *a_me, RE::Actor *a_target)
            {
                NoteCheck(t_circle.angle, true); // reached: the facing and distance checks passed
                const float angle = func(a_me, a_target);
                const float side = Intents::CircleSide(a_me);
                const float out = side > 0.0f ? std::fabs(angle) : (side < 0.0f ? -std::fabs(angle) : angle);
                if (a_me && Config::GetInstance().IsDebugTarget(a_me->GetFormID())) {
                    LOG_DEBUG("Circle 0x{:08X}: vanilla angle {:.3f} -> {:.3f} (brain side {:+.0f})",
                              static_cast<std::uint32_t>(a_me->GetFormID()), angle, out, side);
                }
                return out;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // Movement preemption. CombatBehaviorThread::Update (46228/47483) calls a running node's
        // slot 4 every tick (slot 2 on entry, slot 5 instead of 4 while the thread aborts, slot 3 of
        // the node that just ascended to free its stack frame). The four movement classes all abort
        // with a bare Ascend (slot 5 = Ascend on the current thread; checked on SE, 1.6.1170 and
        // 1.7.104). So stopping one early is that same Ascend from its slot 4: the engine then frees
        // the node's frame, and our selector re-reads the intents on the next tick.
        std::array<std::atomic<Node *>, 8> g_moveSelectors{}; // our "Intent Movement Selector" instances (close + ranged)
        constexpr auto kMinRun = std::chrono::milliseconds(300); // brain flip-flops must not stutter the feet

        // Our selector above a_node, or nullptr when the node is in some other tree. Direct child of one
        // of our RandomNodes = our branch's node (a_ours).
        Node *MoveSelectorOf(const Node *a_node, bool &a_ours)
        {
            int depth = 0;
            for (const Node *n = a_node ? a_node->parent : nullptr; n && depth < 12; n = n->parent, ++depth) {
                for (auto &s : g_moveSelectors) {
                    if (s.load(std::memory_order_relaxed) == n) {
                        a_ours = depth == 1;
                        return const_cast<Node *>(n);
                    }
                }
            }
            return nullptr;
        }

        // Stop only when the brain changed its mind since the node started. Ours: it wants anything but
        // our kind. Vanilla's: it now wants a different move. A vanilla node that started while the brain
        // already wanted that move means our branch for it just failed (e.g. Circle out of range), and
        // stopping it would only loop vanilla -> stop -> vanilla every frame.
        constexpr bool BrainChangedMind(Intents::Move a_atStart, Intents::Move a_now, bool a_ours)
        {
            return a_now != a_atStart && (a_ours || a_now != Intents::Move::None);
        }
        static_assert(BrainChangedMind(Intents::Move::Circle, Intents::Move::None, true));
        static_assert(!BrainChangedMind(Intents::Move::Circle, Intents::Move::Circle, true));
        static_assert(!BrainChangedMind(Intents::Move::Circle, Intents::Move::Circle, false));
        static_assert(!BrainChangedMind(Intents::Move::None, Intents::Move::None, false));
        static_assert(BrainChangedMind(Intents::Move::None, Intents::Move::Circle, false));
        static_assert(BrainChangedMind(Intents::Move::Circle, Intents::Move::Advance, false));
        static_assert(!BrainChangedMind(Intents::Move::Circle, Intents::Move::None, false));

        // Per thread: the node it updates, since when, and what the brain wanted then.
        // shortcut: a node re-entered right after it finished naturally keeps its old entry (only a stop
        // resets it); hook slot 2 if that ever matters.
        bool ShouldStop(const void *a_thread, const Node *a_node, Intents::Move a_atStart, Intents::Move a_now, bool a_ours)
        {
            struct Run
            {
                const Node *node = nullptr;
                std::chrono::steady_clock::time_point start;
                Intents::Move want = Intents::Move::None;
            };
            static std::mutex lock;
            static std::unordered_map<const void *, Run> runs;
            const auto now = std::chrono::steady_clock::now();
            std::scoped_lock guard(lock);
            if (runs.size() > 512) {
                runs.clear(); // threads come and go; entries are only hints
            }
            auto &run = runs[a_thread];
            if (run.node != a_node) {
                run = {a_node, now, a_atStart};
            }
            if (!BrainChangedMind(run.want, a_now, a_ours) || now - run.start < kMinRun) {
                return false;
            }
            run.node = nullptr; // whatever this thread runs next is a fresh start
            return true;
        }

        // Movement node audit (debug target only): one line per node run saying how it ended and what the
        // body did, so a log shows which brain moves the vanilla nodes really carry out. Enter (slot 2) and
        // Exit (slot 3) are called by CombatBehaviorThread::Update with (node, thread), like Update (slot 4).
        struct MoveRun
        {
            std::chrono::steady_clock::time_point start;
            RE::NiPoint3 pos;
            float dist = -1.0f;
            Intents::Move want = Intents::Move::None;
            bool stoppedByBrain = false;
            std::string checks; // Circle: what its start checks answered
        };
        std::mutex g_auditLock;
        std::unordered_map<const void *, MoveRun> g_audit; // per thread

        const char *MoveName(Intents::Move a_move)
        {
            constexpr const char *kNames[] = {"none", "Advance", "Circle", "Fallback", "Backoff"};
            return kNames[static_cast<int>(a_move) % 5];
        }

        float TargetDistance(RE::Actor *a_actor)
        {
            auto *controller = RE::CombatBehaviorController::GetCurrentCombatController();
            const auto target = controller ? controller->targetHandle.get() : RE::NiPointer<RE::Actor>();
            return target ? a_actor->GetPosition().GetDistance(target->GetPosition()) : -1.0f;
        }

        RE::Actor *AuditedActor(RE::CombatBehaviorThread *a_thread)
        {
            auto *actor = a_thread ? RE::CombatBehaviorController::GetCurrentAttacker() : nullptr;
            return actor && Config::GetInstance().IsDebugTarget(actor->GetFormID()) ? actor : nullptr;
        }

        // Our Backoff/Fallback start. The vanilla starts (Backoff 46724/47920, Fallback 46712/47908) walk the NPC
        // out to a ring of radius "weapon range + X" around the target, and refuse when it is already outside
        // that ring, isn't facing the target, the target isn't facing it (Fallback) or the target is moving
        // away (TREE_DESIGN §10; run 12: our Backoff started 2/30, Fallback 15/27). While our branch's node
        // starts, those facing/moving-away calls pass and X puts the ring a step behind where the NPC stands.
        // The path request, CheckStraightPath and IsMovementAnimationDriven stay vanilla.
        thread_local bool t_ourStart = false;

        float CurrentWeaponRange() // CombatInventory+0x1A0: the range both starts add X to (SE 46712+0x1E0, AE 47908+0x1A5)
        {
            const auto *inventory = RE::CombatBehaviorController::GetCurrentCombatInventory();
            return inventory ? *reinterpret_cast<const float *>(reinterpret_cast<std::uintptr_t>(inventory) + 0x1A0) : 0.0f;
        }

        std::uintptr_t CallTarget(std::uintptr_t a_site)
        {
            return *reinterpret_cast<const std::uint8_t *>(a_site) == 0xE8
                       ? a_site + 5 + *reinterpret_cast<const std::int32_t *>(a_site + 1)
                       : 0;
        }

        struct StartFacing // WithinHeadingAngle 36756/37772
        {
            static bool thunk(RE::Actor *a_actor, const RE::NiPoint3 *a_pos, float a_angle) { return t_ourStart || func(a_actor, a_pos, a_angle); }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        struct StartTargetLeaving // "target moving from this spot" 46062/47326 (523984 on 1.7)
        {
            static bool thunk(RE::Actor *a_target, const RE::NiPoint3 *a_from) { return !t_ourStart && func(a_target, a_from); }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        struct BackoffRing // set_two_radiuses 49716/50643: X = inner radius
        {
            static void thunk(RE::Actor *a_self, RE::Actor *a_target, float *a_inner, float *a_outer)
            {
                func(a_self, a_target, a_inner, a_outer);
                if (t_ourStart && a_self && a_target && a_inner) {
                    *a_inner = a_self->GetPosition().GetDistance(a_target->GetPosition()) - CurrentWeaponRange() +
                               Config::GetInstance().GetDecisionMatrix().backoffStepDistance;
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        struct FallbackRing // FallbackDistance 49718/50645: X = the vanilla fallback distance, from where it stands
        {
            static float thunk(RE::Actor *a_self)
            {
                const float distance = func(a_self);
                return t_ourStart && a_self ? TargetDistance(a_self) - CurrentWeaponRange() + distance : distance;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // Backoff Update (46725/47921) ends the node once 46063/47327 (523985 on 1.7) sees the target moving
        // toward the NPC (run 14: 24 of our Backoffs ended after ~110 ms, moved 6). Ours keeps backing off.
        struct BackoffClosing
        {
            static bool thunk(RE::Actor *a_actor, const RE::NiPoint3 *a_pos) { return !t_ourStart && func(a_actor, a_pos); }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // Circle start audit recorders (pass the vanilla answer through unchanged)
        struct CircleAnimDriven // IsMovementAnimationDriven 36487/37486: true refuses
        {
            static bool thunk(RE::Actor *a_actor)
            {
                const bool driven = func(a_actor);
                NoteCheck(t_circle.animDriven, !driven);
                return driven;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        struct CircleFacing // WithinHeadingAngle, target in front
        {
            static bool thunk(RE::Actor *a_actor, const RE::NiPoint3 *a_pos, float a_angle) { return NoteCheck(t_circle.facing, func(a_actor, a_pos, a_angle)); }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        struct CircleViewCone // WithinHeadingAngle with fCombatCircleViewConeAngle
        {
            static bool thunk(RE::Actor *a_actor, const RE::NiPoint3 *a_pos, float a_angle) { return NoteCheck(t_circle.viewCone, func(a_actor, a_pos, a_angle)); }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        struct CircleStraightPath // CombatController::CheckStraightPath 32513/33262
        {
            static bool thunk(void *a_controller, const RE::NiPoint3 *a_pos, float a_speed, float a_tolerance)
            {
                return NoteCheck(t_circle.straightPath, func(a_controller, a_pos, a_speed, a_tolerance));
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        struct CirclePathCheck // 32507/33256 (controller, point) -> bool, right after the straight-path check
        {
            static bool thunk(void *a_controller, const RE::NiPoint3 *a_pos) { return NoteCheck(t_circle.pathCheck, func(a_controller, a_pos)); }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        constexpr REL::RelocationID kBackoffUpdate{46725, 47921};
        constexpr std::size_t kBackoffClosingSE = 0x10B;
        constexpr std::size_t kBackoffClosingAE = 0x17A; // same on 1.7.104
        constexpr REL::RelocationID kAnimDriven{36487, 37486};
        constexpr REL::RelocationID kStraightPath{32513, 33262};

        struct FallbackAnimDriven
        {
            static bool thunk(RE::Actor *a_actor)
            {
                const bool driven = func(a_actor);
                if (t_fallbackAudit) {
                    t_fallback.animDriven = driven ? 0 : 1;
                }
                return driven;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        struct FallbackStraightPath
        {
            static bool thunk(void *a_controller, const RE::NiPoint3 *a_pos, float a_speed, float a_tolerance)
            {
                const bool ok = func(a_controller, a_pos, a_speed, a_tolerance);
                if (t_fallbackAudit) {
                    t_fallback.straightPath = ok ? 1 : 0;
                }
                return ok;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        // SE 46712 +0x15F / +0x27B, AE 47908 +0x12B / +0x239 (1.6.1170 and 1.7.104; RE 2026-10-26)
        void InstallFallbackAudit(SKSE::Trampoline &a_trampoline)
        {
            const auto f = REL::RelocationID(46712, 47908).address();
            const auto anim = f + (REL::Module::IsAE() ? 0x12B : 0x15F);
            const auto path = f + (REL::Module::IsAE() ? 0x239 : 0x27B);
            if (!CallsTarget(anim, kAnimDriven.address()) || !CallsTarget(path, kStraightPath.address())) {
                LOG_ERROR("Tree: Fallback start sites do not match - no Fallback start-check audit");
                return;
            }
            FallbackAnimDriven::func = a_trampoline.write_call<5>(anim, FallbackAnimDriven::thunk);
            FallbackStraightPath::func = a_trampoline.write_call<5>(path, FallbackStraightPath::thunk);
            LOG_INFO("Tree: Fallback start checks audited (MoveNode lines show which one refused)");
        }
        constexpr REL::RelocationID kCirclePathCheck{32507, 33256};
        struct CircleSites
        {
            std::size_t animDriven, facing, viewCone, straightPath, pathCheck;
        };
        constexpr CircleSites kCircleSitesSE{0x179, 0x198, 0x366, 0x3C8, 0x3E7};
        constexpr CircleSites kCircleSitesAE{0x149, 0x16A, 0x2F2, 0x351, 0x370}; // same on 1.7.104

        // The Backoff Update check: same idea as the start, but its callee (46063) has no stable ID on 1.7, so it
        // is accepted as a call into the function right after 46062 (0xB0 later on SE/AE, 0xE0 on 1.7.104).
        void InstallBackoffClosing(SKSE::Trampoline &a_trampoline, std::uintptr_t a_leaving)
        {
            const auto site = kBackoffUpdate.address() + (REL::Module::IsAE() ? kBackoffClosingAE : kBackoffClosingSE);
            const auto target = CallTarget(site);
            if (!target || target <= a_leaving || target - a_leaving > 0x100) {
                LOG_ERROR("Tree: Backoff update site does not match - our Backoff still stops when the target closes in");
                return;
            }
            BackoffClosing::func = a_trampoline.write_call<5>(site, BackoffClosing::thunk);
            LOG_INFO("Tree: Backoff update check hooked (our Backoff keeps going while the target closes in)");
        }

        void InstallCircleAudit(SKSE::Trampoline &a_trampoline)
        {
            const auto &s = REL::Module::IsAE() ? kCircleSitesAE : kCircleSitesSE;
            const auto c = kCircleUpdate.address();
            const auto heading = REL::RelocationID(36756, 37772).address();
            if (!CallsTarget(c + s.animDriven, kAnimDriven.address()) || !CallsTarget(c + s.facing, heading) ||
                !CallsTarget(c + s.viewCone, heading) || !CallsTarget(c + s.straightPath, kStraightPath.address()) ||
                !CallsTarget(c + s.pathCheck, kCirclePathCheck.address())) {
                LOG_ERROR("Tree: Circle start sites do not match - no Circle start-check audit");
                return;
            }
            CircleAnimDriven::func = a_trampoline.write_call<5>(c + s.animDriven, CircleAnimDriven::thunk);
            CircleFacing::func = a_trampoline.write_call<5>(c + s.facing, CircleFacing::thunk);
            CircleViewCone::func = a_trampoline.write_call<5>(c + s.viewCone, CircleViewCone::thunk);
            CircleStraightPath::func = a_trampoline.write_call<5>(c + s.straightPath, CircleStraightPath::thunk);
            CirclePathCheck::func = a_trampoline.write_call<5>(c + s.pathCheck, CirclePathCheck::thunk);
            LOG_INFO("Tree: Circle start checks audited (MoveNode lines show which one refused)");
        }

        // Our Advance: the start (46703/47898) sets its destination ring to "weapon range + inner radius"
        // (CombatTargetLocationSearch 45702 and the approach controller 49062 both get [inner] + range) and an
        // accept band [inner, outer] + range (+0x38F). While our branch's node starts, both are set to the
        // brain's stop distance: with vanilla's outer (256-512 + range) a bandit at 129 units counted as
        // already there and the node ended next tick (run 18: 41 times, moved 0).
        struct AdvanceRing // set_two_radiuses 49716/50643 (self, target, &inner, &outer)
        {
            static void thunk(RE::Actor *a_self, RE::Actor *a_target, float *a_inner, float *a_outer)
            {
                func(a_self, a_target, a_inner, a_outer);
                const float stop = t_ourStart && a_self ? Intents::AdvanceDistance(a_self) : 0.0f;
                if (stop > 0.0f && a_inner) {
                    *a_inner = stop - CurrentWeaponRange();
                    if (a_outer) {
                        *a_outer = *a_inner;
                    }
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        // Advance Update (46704/47899) ends the node when CheckShouldStop (46698/47893, (location, tolerance))
        // says the NPC is near its destination, within a tolerance of tens of units (run 17: a range-76 bandit
        // "arrived" 34 times at 80-99 units). Ours is in place only once inside the brain's stop distance.
        struct AdvanceShouldStop
        {
            static bool thunk(void *a_location, float a_tolerance)
            {
                if (t_ourStart) {
                    auto *actor = RE::CombatBehaviorController::GetCurrentAttacker();
                    const float stop = actor ? Intents::AdvanceDistance(actor) : 0.0f;
                    const float distance = actor ? TargetDistance(actor) : -1.0f;
                    if (stop > 0.0f && distance > stop) {
                        return false;
                    }
                }
                return func(a_location, a_tolerance);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
        constexpr REL::RelocationID kAdvanceUpdate{46704, 47899};
        constexpr std::size_t kAdvanceShouldStop = 0x121; // SE, AE and 1.7.104

        constexpr REL::RelocationID kAdvanceStart{46703, 47898};
        constexpr std::size_t kAdvanceRingSE = 0x16F;
        constexpr std::size_t kAdvanceRingAE = 0x127; // same on 1.7.104

        void InstallAdvanceRing(SKSE::Trampoline &a_trampoline)
        {
            const auto site = kAdvanceStart.address() + (REL::Module::IsAE() ? kAdvanceRingAE : kAdvanceRingSE);
            if (!CallsTarget(site, REL::RelocationID(49716, 50643).address())) {
                LOG_ERROR("Tree: Advance start site does not match - our Advance keeps vanilla's distance");
                return;
            }
            AdvanceRing::func = a_trampoline.write_call<5>(site, AdvanceRing::thunk);
            LOG_INFO("Tree: Advance start radius hooked (our Advance closes to the brain's attack range)");
            const auto stopSite = kAdvanceUpdate.address() + kAdvanceShouldStop;
            if (!CallsTarget(stopSite, REL::RelocationID(46698, 47893).address())) {
                LOG_ERROR("Tree: Advance update site does not match - our Advance may stop short of its range");
                return;
            }
            AdvanceShouldStop::func = a_trampoline.write_call<5>(stopSite, AdvanceShouldStop::thunk);
            LOG_INFO("Tree: Advance in-place check hooked (our Advance stops only inside the brain's range)");
        }

        constexpr REL::RelocationID kBackoffStart{46724, 47920};
        constexpr REL::RelocationID kFallbackStart{46712, 47908};
        constexpr REL::RelocationID kWithinHeadingAngle{36756, 37772};
        constexpr REL::RelocationID kInnerRadius{49716, 50643};
        constexpr REL::RelocationID kFallbackDistance{49718, 50645};
        struct StartSites
        {
            std::size_t backoffLeaving, backoffFacing, backoffRing, fallbackFacing1, fallbackLeaving, fallbackFacing2, fallbackRing;
        };
        // Same call order on SE 1.5.97, AE 1.6.1170 and 1.7.104 (dumped 2026-10-22); AE and 1.7 share offsets
        constexpr StartSites kStartSitesSE{0x142, 0x16E, 0x1B7, 0x13B, 0x14F, 0x17B, 0x1EB};
        constexpr StartSites kStartSitesAE{0x104, 0x130, 0x179, 0x107, 0x11B, 0x147, 0x1B0};

        void InstallOwnStarts(SKSE::Trampoline &a_trampoline)
        {
            const auto &s = REL::Module::IsAE() ? kStartSitesAE : kStartSitesSE;
            const auto b = kBackoffStart.address();
            const auto f = kFallbackStart.address();
            const auto heading = kWithinHeadingAngle.address();
            const auto leaving = CallTarget(b + s.backoffLeaving); // renumbered on 1.7: both starts must call the same one
            const bool ok = leaving && CallsTarget(f + s.fallbackLeaving, leaving) && CallsTarget(b + s.backoffFacing, heading) &&
                            CallsTarget(f + s.fallbackFacing1, heading) && CallsTarget(f + s.fallbackFacing2, heading) &&
                            CallsTarget(b + s.backoffRing, kInnerRadius.address()) &&
                            CallsTarget(f + s.fallbackRing, kFallbackDistance.address());
            if (!ok) {
                LOG_ERROR("Tree: Backoff/Fallback start sites do not match - the brain's Backoff/Fallback keep vanilla start rules");
                return;
            }
            StartFacing::func = a_trampoline.write_call<5>(b + s.backoffFacing, StartFacing::thunk);
            a_trampoline.write_call<5>(f + s.fallbackFacing1, StartFacing::thunk);
            a_trampoline.write_call<5>(f + s.fallbackFacing2, StartFacing::thunk);
            StartTargetLeaving::func = a_trampoline.write_call<5>(b + s.backoffLeaving, StartTargetLeaving::thunk);
            a_trampoline.write_call<5>(f + s.fallbackLeaving, StartTargetLeaving::thunk);
            BackoffRing::func = a_trampoline.write_call<5>(b + s.backoffRing, BackoffRing::thunk);
            FallbackRing::func = a_trampoline.write_call<5>(f + s.fallbackRing, FallbackRing::thunk);
            LOG_INFO("Tree: Backoff/Fallback start checks hooked (the brain's moves start where it asks)");
            InstallBackoffClosing(a_trampoline, leaving);
        }

        template <Intents::Move K>
        struct MoveEnter
        {
            static void thunk(Node *a_node, RE::CombatBehaviorThread *a_thread)
            {
                if constexpr (K == Intents::Move::Backoff || K == Intents::Move::Fallback || K == Intents::Move::Advance) {
                    bool ours = false;
                    const auto &matrix = Config::GetInstance().GetDecisionMatrix();
                    t_ourStart = MoveSelectorOf(a_node, ours) && ours &&
                                 (K == Intents::Move::Advance ? matrix.brainAdvanceDistance : matrix.brainBackoffFallback);
                }
                auto *audited = AuditedActor(a_thread);
                if (audited) {
                    std::scoped_lock guard(g_auditLock);
                    if (g_audit.size() > 64) {
                        g_audit.clear();
                    }
                    g_audit[a_thread] = {std::chrono::steady_clock::now(), audited->GetPosition(), TargetDistance(audited),
                                         Intents::WantedMove(audited), false, {}};
                }
                if constexpr (K == Intents::Move::Circle) {
                    t_circleAudit = audited != nullptr;
                    t_circle = {};
                }
                if constexpr (K == Intents::Move::Fallback) {
                    t_fallbackAudit = audited != nullptr;
                    t_fallback = {};
                }
                func(a_node, a_thread); // may fail at once: SetFailed + Ascend inside (e.g. Backoff out of range)
                t_ourStart = false;
                if constexpr (K == Intents::Move::Circle) {
                    if (t_circleAudit) {
                        const auto &c = t_circle;
                        std::scoped_lock guard(g_auditLock);
                        if (auto it = g_audit.find(a_thread); it != g_audit.end()) {
                            it->second.checks = std::format(
                                " [start checks: not animation-driven {}, facing {}, angle {}, view cone {}, straight path {}, path {}]",
                                c.animDriven, c.facing, c.angle, c.viewCone, c.straightPath, c.pathCheck);
                        }
                    }
                    t_circleAudit = false;
                }
                if constexpr (K == Intents::Move::Fallback) {
                    if (t_fallbackAudit) {
                        const auto &c = t_fallback;
                        std::scoped_lock guard(g_auditLock);
                        if (auto it = g_audit.find(a_thread); it != g_audit.end()) {
                            it->second.checks = std::format(" [start checks: not animation-driven {}, far enough {}, straight path {}]",
                                                            c.animDriven, c.animDriven == 1 ? (c.straightPath >= 0 ? 1 : 0) : -1,
                                                            c.straightPath);
                        }
                    }
                    t_fallbackAudit = false;
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        template <Intents::Move K>
        struct MoveExit
        {
            static void thunk(Node *a_node, RE::CombatBehaviorThread *a_thread)
            {
                if constexpr (K == Intents::Move::Circle) {
                    // Our Circle failed on the brain's side (path or view cone, run 48): the brain tries the other
                    bool ours = false;
                    if (a_thread && a_thread->failState.any(RE::CombatBehaviorThread::FailState::kFailed) &&
                        MoveSelectorOf(a_node, ours) && ours) {
                        Intents::NoteCircleRefused(RE::CombatBehaviorController::GetCurrentAttacker());
                    }
                }
                if (auto *actor = AuditedActor(a_thread)) {
                    std::optional<MoveRun> run;
                    {
                        std::scoped_lock guard(g_auditLock);
                        if (auto it = g_audit.find(a_thread); it != g_audit.end()) {
                            run = it->second;
                            g_audit.erase(it);
                        }
                    }
                    if (run) {
                        bool ours = false;
                        const bool underOurs = MoveSelectorOf(a_node, ours) != nullptr;
                        const auto pos = actor->GetPosition();
                        const float ms =
                            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - run->start).count();
                        const char *end = run->stoppedByBrain ? "stopped by brain"
                                          : a_thread->failState.any(RE::CombatBehaviorThread::FailState::kFailed) ? "failed"
                                          : a_thread->failState.any(RE::CombatBehaviorThread::FailState::kAborted) ? "aborted"
                                                                                                                    : "done";
                        LOG_DEBUG("MoveNode 0x{:08X}: {} {} ran {:.0f} ms, moved {:.0f}, dist {:.0f} -> {:.0f}, {} (brain wanted {}){}",
                                  static_cast<std::uint32_t>(actor->GetFormID()),
                                  underOurs ? (ours ? "our" : "vanilla") : "other-tree", MoveName(K), ms,
                                  std::hypot(pos.x - run->pos.x, pos.y - run->pos.y), run->dist, TargetDistance(actor), end,
                                  MoveName(run->want), run->checks);
                    }
                }
                func(a_node, a_thread);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // A running cast node stops (Ascend instead of Update, like the movement preemption) when the brain wants a
        // dual block: the engine then runs the node's Exit, which interrupts the cast, and the Magic tree's loop
        // re-reads our DualBlock branch. Otherwise the block would wait for the cast to end.
        template <int K>
        struct CastUpdate
        {
            static void thunk(Node *a_node, RE::CombatBehaviorThread *a_thread)
            {
                auto *actor = RE::CombatBehaviorController::GetCurrentAttacker();
                // Stop for a wanted dual block, or a stream (concentration) spell with an ally in the line of fire
                const bool holdStream = (K == 1 || K == 3) && actor && Intents::RangedShotFactor(actor) <= 0.0f;
                if (actor && a_thread && (Intents::WantsDualBlock(actor) || holdStream)) {
                    if (Config::GetInstance().IsDebugTarget(actor->GetFormID())) {
                        LOG_DEBUG("{} 0x{:08X}: {} cast stopped", holdStream ? "Ranged" : "DualBlock", actor->GetFormID(),
                                  K == 0 ? "immediate" : K == 1 ? "concentration" : K == 2 ? "dual immediate" : "dual concentration");
                    }
                    a_thread->Ascend();
                    return;
                }
                func(a_node, a_thread);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // Vanilla Magic (0.18.3 dump): Context > ValueNode "Casting Type Selector" > one ValueNode case per casting
        // type ("Cast Immediate Repeat", "Cast Concentration Repeat") > that type's spell selector. The loop is the
        // equipment selector above, which re-runs the tree from the top. Each case gets
        // case > NextSelector{ DualBlock, <its spell selector> }, so every pass reads our branch first.
        void CollectCastCases(Node *a_node, std::vector<Node *> &a_out, int a_depth)
        {
            if (!a_node || a_depth > 16) {
                return;
            }
            if (Is(a_node, Id::vtCastTypeCase) && a_node->childCount == 1 && a_node->children[0]) {
                a_out.push_back(a_node);
            }
            for (std::int32_t i = 0; i < a_node->childCount; ++i) {
                CollectCastCases(a_node->children[i], a_out, a_depth + 1);
            }
        }

        struct MagicSetRoot
        {
            static void thunk(void *a_tree, Node *a_root)
            {
                std::vector<Node *> cases;
                CollectCastCases(a_root, cases, 0);
                for (auto *castCase : cases) {
                    Node *vanilla = castCase->children[0];
                    vanilla->parent = nullptr; // detach, or SetChildren would add the whole tree's root
                    auto *selector = Checked(Named(NextSelector(), "Intent Magic Selector"), "Intent Magic Selector");
                    SetChildren(selector, {DualBlockBranch(), HoldCastBranch(), vanilla});
                    castCase->children[0] = selector; // the case keeps its one-slot child array
                    selector->parent = castCase;
                }
                func(a_tree, a_root);
                if (cases.empty()) {
                    LOG_ERROR("Tree: Magic tree has no casting-type case node - no dual block for spell hands");
                } else if (g_magicBuilt.fetch_add(1) == 0) {
                    LOG_INFO("Tree: Magic tree extended (dual block and cast hold in {} casting types)", cases.size());
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        void NoteStoppedByBrain(RE::CombatBehaviorThread *a_thread)
        {
            std::scoped_lock guard(g_auditLock);
            if (auto it = g_audit.find(a_thread); it != g_audit.end()) {
                it->second.stoppedByBrain = true;
            }
        }

        template <Intents::Move K>
        struct MoveUpdate
        {
            static void thunk(Node *a_node, RE::CombatBehaviorThread *a_thread)
            {
                bool ours = false;
                auto *actor = MoveSelectorOf(a_node, ours) ? RE::CombatBehaviorController::GetCurrentAttacker() : nullptr;
                if (actor && a_thread) {
                    const auto want = Intents::WantedMove(actor);
                    if constexpr (K == Intents::Move::Backoff) {
                        t_ourStart = ours && Config::GetInstance().GetDecisionMatrix().brainBackoffFallback;
                    } else if constexpr (K == Intents::Move::Advance) {
                        t_ourStart = ours && Config::GetInstance().GetDecisionMatrix().brainAdvanceDistance;
                    }
                    if (ShouldStop(a_thread, a_node, ours ? K : want, want, ours)) {
                        t_ourStart = false;
                        Intents::NotePreempted(!ours);
                        if (Config::GetInstance().IsDebugTarget(actor->GetFormID())) {
                            LOG_DEBUG("Move 0x{:08X}: stopped running {} node {} (brain wants move {})",
                                      static_cast<std::uint32_t>(actor->GetFormID()), ours ? "our" : "vanilla",
                                      static_cast<int>(K), static_cast<int>(want));
                        }
                        NoteStoppedByBrain(a_thread);
                        a_thread->Ascend();
                        return;
                    }
                }
                func(a_node, a_thread);
                t_ourStart = false;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        template <Intents::Move K>
        void HookMoveUpdate(const REL::RelocationID &a_vtable)
        {
            REL::Relocation<std::uintptr_t> vtable{a_vtable};
            MoveUpdate<K>::func = vtable.write_vfunc(4, MoveUpdate<K>::thunk);
            MoveEnter<K>::func = vtable.write_vfunc(2, MoveEnter<K>::thunk);
            MoveExit<K>::func = vtable.write_vfunc(3, MoveExit<K>::thunk);
        }

        // Vanilla CloseMovement: Context > Repeat > Movement Selector{ OutOfRange.., OutOfPosition.., InRange.. }.
        // Ours:    Context > Repeat > NextSelector{ brain Advance, Circle, Fallback, Backoff, <vanilla Movement Selector> }.
        // The brain's branches only run while it keeps asking (WantsMove); otherwise vanilla runs as before.
        // A running movement node is stopped early when the brain changes its mind (MoveUpdate above).
        // Context > Repeat > <vanilla selector> becomes Context > Repeat > NextSelector{ our moves, <vanilla> }.
        // False (tree left vanilla) if the shape differs.
        bool SpliceMovement(Node *a_root)
        {
            Node *repeat = a_root && a_root->childCount == 1 ? a_root->children[0] : nullptr;
            Node *vanilla = repeat && repeat->childCount == 1 ? repeat->children[0] : nullptr;
            if (!Is(repeat, Id::vtRepeat) || !vanilla) {
                return false;
            }
            vanilla->parent = nullptr; // detach, or SetChildren would add the whole tree's root
                auto *selector = Checked(Named(NextSelector(), "Intent Movement Selector"), "Intent Movement Selector");
            SetChildren(selector,
                        {Checked(RandomNode(Named(Advance(), "Advance"), WantAdvance, "Intent - Advance"), "intent advance"),
                         Checked(RandomNode(Named(Circle(), "Circle"), WantCircle, "Intent - Circle"), "intent circle"),
                         Checked(RandomNode(Named(Fallback(), "Fallback"), WantFallback, "Intent - Fallback"), "intent fallback"),
                         Checked(RandomNode(Named(Backoff(), "Backoff"), WantBackoff, "Intent - Backoff"), "intent backoff"),
                         vanilla}); // re-parents the vanilla selector under ours
            repeat->children[0] = selector; // Repeat keeps its one-slot child array
            selector->parent = repeat;
            for (auto &s : g_moveSelectors) {
                Node *expected = nullptr;
                if (s.compare_exchange_strong(expected, selector)) {
                    break; // a 9th tree build would just not be preempted
                }
            }
            return true;
        }

        struct CloseMovementSetRoot
        {
            static void thunk(void *a_tree, Node *a_root)
            {
                if (!Is(a_root, Id::vtContextCloseMovement) || !SpliceMovement(a_root)) {
                    LOG_ERROR("Tree: CloseMovement tree has an unexpected shape - left vanilla");
                } else if (g_closeBuilt.fetch_add(1) == 0) {
                    LOG_INFO("Tree: CloseMovement tree extended (intent Advance/Circle/Fallback/Backoff over vanilla)");
                }
                func(a_tree, a_root);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct RangedMovementSetRoot
        {
            static void thunk(void *a_tree, Node *a_root)
            {
                if (!SpliceMovement(a_root)) {
                    LOG_ERROR("Tree: RangedMovement tree has an unexpected shape - left vanilla");
                } else if (g_rangedBuilt.fetch_add(1) == 0) {
                    LOG_INFO("Tree: RangedMovement tree extended (archers and mages take the brain's moves)");
                }
                func(a_tree, a_root);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    } // namespace

    void ReleaseDualBlock(RE::Actor *a_actor, bool a_force)
    {
        if (a_actor && Intents::EndDualBlock(a_actor, a_force)) {
            SetWantBlocking(a_actor, false);
            const bool stopped = a_actor->NotifyAnimationGraph("blockStop");
            if (Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
                LOG_DEBUG("DualBlock 0x{:08X}: released{}, blockStop {}", a_actor->GetFormID(), a_force ? " (forced)" : "",
                          stopped ? "accepted" : "rejected");
            }
        }
    }

    void Install()
    {
        // DualBlock node vtable: Idle's, with our Enter (slot 2) and Exit (slot 3) around Idle's own
        {
            const auto *idle = reinterpret_cast<void *const *>(Id::vtIdle.address());
            std::copy_n(idle - 1, g_dualBlockVtable.size(), g_dualBlockVtable.begin());
            g_idleEnter = reinterpret_cast<NodeFn>(idle[2]);
            g_idleExit = reinterpret_cast<NodeFn>(idle[3]);
            g_dualBlockVtable[1 + 2] = reinterpret_cast<void *>(&DualBlockEnter);
            g_dualBlockVtable[1 + 3] = reinterpret_cast<void *>(&DualBlockExit);
        }

        // Full replacement: the vanilla builder never runs (same approach as Fenix-NG).
        auto &trampoline = SKSE::GetTrampoline();
        trampoline.write_branch<5>(kMeleeTree.address(), reinterpret_cast<std::uintptr_t>(&MeleeTree));
        trampoline.write_branch<5>(kBlockTree.address(), reinterpret_cast<std::uintptr_t>(&BlockTree));
        LOG_INFO("Tree: Melee and Block tree builders replaced");

        const auto site = kCloseMovementTree.address() +
                          (REL::Module::IsAE() ? kCloseMovementSetRootAE : kCloseMovementSetRootSE);
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(site);
        if (CallsTarget(site, Id::initNodes.address())) {
            CloseMovementSetRoot::func = trampoline.write_call<5>(site, CloseMovementSetRoot::thunk);
            LOG_INFO("Tree: CloseMovement SetRoot hooked");
            HookMoveUpdate<Intents::Move::Advance>(Id::vtAdvance);
            HookMoveUpdate<Intents::Move::Circle>(Id::vtCircle);
            HookMoveUpdate<Intents::Move::Fallback>(Id::vtFallback);
            HookMoveUpdate<Intents::Move::Backoff>(Id::vtBackoff);
            LOG_INFO("Tree: movement node Enter/Update/Exit hooked (preemption + MoveNode audit for the selected NPC)");
            InstallCircleAudit(trampoline);
            InstallFallbackAudit(trampoline);
            if (Config::GetInstance().GetDecisionMatrix().brainAdvanceDistance) {
                InstallAdvanceRing(trampoline);
            }
            if (Config::GetInstance().GetDecisionMatrix().brainBackoffFallback) {
                InstallOwnStarts(trampoline);
            } else {
                LOG_INFO("Tree: BrainBackoffFallback off at load - Backoff/Fallback start checks not hooked");
            }
        } else {
            LOG_ERROR("Tree: CloseMovement SetRoot site does not match (byte {:02X}) - movement intents disabled", bytes[0]);
        }

        InstallRangedShotChance(trampoline);

        const auto rangedSite =
            kRangedMovementTree.address() + (REL::Module::IsAE() ? kRangedMovementSetRootAE : kRangedMovementSetRootSE);
        if (CallsTarget(rangedSite, Id::initNodes.address())) {
            RangedMovementSetRoot::func = trampoline.write_call<5>(rangedSite, RangedMovementSetRoot::thunk);
            LOG_INFO("Tree: RangedMovement SetRoot hooked");
        } else {
            LOG_ERROR("Tree: RangedMovement SetRoot site does not match - archers and mages keep vanilla movement");
        }

        const auto magicSite = kMagicTree.address() + (REL::Module::IsAE() ? kMagicSetRootAE : kMagicSetRootSE);
        if (CallsTarget(magicSite, Id::initNodes.address())) {
            MagicSetRoot::func = trampoline.write_call<5>(magicSite, MagicSetRoot::thunk);
            REL::Relocation<std::uintptr_t> immediate{Id::vtCastImmediate}, concentration{Id::vtCastConcentration},
                dualImmediate{Id::vtDualCastImmediate}, dualConcentration{Id::vtDualCastConcentration};
            CastUpdate<0>::func = immediate.write_vfunc(4, CastUpdate<0>::thunk);
            CastUpdate<1>::func = concentration.write_vfunc(4, CastUpdate<1>::thunk);
            CastUpdate<2>::func = dualImmediate.write_vfunc(4, CastUpdate<2>::thunk);
            CastUpdate<3>::func = dualConcentration.write_vfunc(4, CastUpdate<3>::thunk);
            LOG_INFO("Tree: Magic SetRoot hooked (dual block for a weapon with a spell; a running cast stops for it)");
        } else {
            LOG_ERROR("Tree: Magic SetRoot site does not match - no dual block for spell hands");
        }

        const auto circleSite = kCircleUpdate.address() + (REL::Module::IsAE() ? kCircleAngleCallAE : kCircleAngleCallSE);
        if (CallsTarget(circleSite, kCircleAngle.address())) {
            CircleAngle::func = trampoline.write_call<5>(circleSite, CircleAngle::thunk);
            LOG_INFO("Tree: Circle angle call hooked (brain picks the side)");
        } else {
            LOG_ERROR("Tree: Circle angle call site does not match - vanilla picks the circle side");
        }
    }
} // namespace CombatAI::Trees

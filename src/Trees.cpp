#include "Trees.h"
#include "Config.h"
#include "Intents.h"
#include "Logger.h"
#include "TreeNodes.h"

#include <chrono>
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
                SKSE::stl::report_and_fail(std::format("CombatAITree: could not build tree node '{}'", a_what));
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

        std::atomic<std::uint32_t> g_meleeBuilt{0}, g_blockBuilt{0};

        // Vanilla Melee: Context > Repeat > Selector{ RandomNode-Special Attack, RandomNode-Attack, Idle }.
        // Ours puts the brain's attack first.
        void MeleeTree(void *a_tree)
        {
            auto *selector = Checked(Named(NextSelector(), "Attack Selector"), "Attack Selector");
            SetChildren(selector,
                        {Checked(RandomNode(Named(Attack(), "attack"), IntentAttack, "Intent - Attack"), "intent attack"),
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

        std::atomic<std::uint32_t> g_closeBuilt{0};

        // Circle on the brain's side: keep vanilla's angle size, set its sign.
        struct CircleAngle
        {
            static float thunk(RE::Actor *a_me)
            {
                const float angle = func(a_me);
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
        std::array<std::atomic<Node *>, 4> g_moveSelectors{}; // our "Intent Movement Selector" instances
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

        // How long a_node has been the node this thread updates (time since first seen).
        // shortcut: a node re-entered right after it finished keeps its old start, which only allows an
        // earlier stop; hook slot 2 if that ever matters.
        std::chrono::steady_clock::duration RunTime(const void *a_thread, const Node *a_node)
        {
            static std::mutex lock;
            static std::unordered_map<const void *, std::pair<const Node *, std::chrono::steady_clock::time_point>> seen;
            const auto now = std::chrono::steady_clock::now();
            std::scoped_lock guard(lock);
            if (seen.size() > 512) {
                seen.clear(); // threads come and go; entries are only hints
            }
            auto &entry = seen[a_thread];
            if (entry.first != a_node) {
                entry = {a_node, now};
            }
            return now - entry.second;
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
                    // Ours: stop once the brain wants anything else. Vanilla's (under our selector): stop
                    // only when the brain wants a move, so our branch can take over.
                    const bool stop = ours ? want != K : want != Intents::Move::None;
                    if (stop && RunTime(a_thread, a_node) >= kMinRun) {
                        Intents::NotePreempted(!ours);
                        if (Config::GetInstance().IsDebugTarget(actor->GetFormID())) {
                            LOG_DEBUG("Move 0x{:08X}: stopped running {} node {} (brain wants move {})",
                                      static_cast<std::uint32_t>(actor->GetFormID()), ours ? "our" : "vanilla",
                                      static_cast<int>(K), static_cast<int>(want));
                        }
                        a_thread->Ascend();
                        return;
                    }
                }
                func(a_node, a_thread);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        template <Intents::Move K>
        void HookMoveUpdate(const REL::RelocationID &a_vtable)
        {
            REL::Relocation<std::uintptr_t> vtable{a_vtable};
            MoveUpdate<K>::func = vtable.write_vfunc(4, MoveUpdate<K>::thunk);
        }

        // Vanilla CloseMovement: Context > Repeat > Movement Selector{ OutOfRange.., OutOfPosition.., InRange.. }.
        // Ours:    Context > Repeat > NextSelector{ brain Advance, Circle, Fallback, Backoff, <vanilla Movement Selector> }.
        // The brain's branches only run while it keeps asking (WantsMove); otherwise vanilla runs as before.
        // A running movement node is stopped early when the brain changes its mind (MoveUpdate above).
        struct CloseMovementSetRoot
        {
            static void thunk(void *a_tree, Node *a_root)
            {
                Node *repeat = a_root && a_root->childCount == 1 ? a_root->children[0] : nullptr;
                Node *vanilla = repeat && repeat->childCount == 1 ? repeat->children[0] : nullptr;
                if (!Is(a_root, Id::vtContextCloseMovement) || !Is(repeat, Id::vtRepeat) || !vanilla) {
                    LOG_ERROR("Tree: CloseMovement tree has an unexpected shape - left vanilla");
                    func(a_tree, a_root);
                    return;
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
                        break; // a 5th tree build would just not be preempted
                    }
                }
                func(a_tree, a_root);
                if (g_closeBuilt.fetch_add(1) == 0) {
                    LOG_INFO("Tree: CloseMovement tree extended (intent Advance/Circle/Fallback/Backoff over vanilla)");
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    } // namespace

    void Install()
    {
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
            LOG_INFO("Tree: movement node updates hooked (running moves stop when the brain changes its mind)");
        } else {
            LOG_ERROR("Tree: CloseMovement SetRoot site does not match (byte {:02X}) - movement intents disabled", bytes[0]);
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

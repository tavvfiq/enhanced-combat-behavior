#include "Trees.h"
#include "Intents.h"
#include "Logger.h"
#include "TreeNodes.h"

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

        // Vanilla CloseMovement: Context > Repeat > Movement Selector{ OutOfRange.., OutOfPosition.., InRange.. }.
        // Ours:    Context > Repeat > NextSelector{ brain Advance, Circle, Fallback, Backoff, <vanilla Movement Selector> }.
        // The brain's branches only run while it keeps asking (WantsMove); otherwise vanilla runs as before.
        // A running movement node finishes before the selector re-reads (no preemption yet).
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
                auto *selector = Checked(Named(NextSelector(), "Intent Movement Selector"), "Intent Movement Selector");
                SetChildren(selector,
                            {Checked(RandomNode(Named(Advance(), "Advance"), WantAdvance, "Intent - Advance"), "intent advance"),
                             Checked(RandomNode(Named(Circle(), "Circle"), WantCircle, "Intent - Circle"), "intent circle"),
                             Checked(RandomNode(Named(Fallback(), "Fallback"), WantFallback, "Intent - Fallback"), "intent fallback"),
                             Checked(RandomNode(Named(Backoff(), "Backoff"), WantBackoff, "Intent - Backoff"), "intent backoff"),
                             vanilla}); // re-parents the vanilla selector under ours
                repeat->children[0] = selector; // Repeat keeps its one-slot child array
                selector->parent = repeat;
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
        const REL::Relocation<std::uintptr_t> setRoot{Id::initNodes};
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(site);
        const auto target = site + 5 + *reinterpret_cast<const std::int32_t *>(site + 1);
        if (bytes[0] == 0xE8 && target == setRoot.address()) {
            CloseMovementSetRoot::func = trampoline.write_call<5>(site, CloseMovementSetRoot::thunk);
            LOG_INFO("Tree: CloseMovement SetRoot hooked");
        } else {
            LOG_ERROR("Tree: CloseMovement SetRoot site does not match (byte {:02X}) - movement intents disabled", bytes[0]);
        }
    }
} // namespace CombatAI::Trees

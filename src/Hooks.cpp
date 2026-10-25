#include "Hooks.h"
#include "ActorUtils.h"
#include "CombatDirector.h"
#include "Config.h"
#include "EngineProbes.h"
#include "Logger.h"
#include "VanillaAttackShaper.h"
#include "pch.h"

namespace CombatAI
{
    namespace Hooks
    {
        // Hook structure (inspired by ProjectGapClose)
        // Post-hook pattern: call original, then our logic
        struct Actor_Update
        {
            static void thunk(RE::Actor *a_actor, float a_delta)
            {
                // Call original function first
                func(a_actor, a_delta);

                CombatDirector::GetInstance().ProcessActor(a_actor, RE::GetSecondsSinceLastFrame());
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // Global systems must tick exactly once per frame. The player never reaches the
        // Character hook above: PlayerCharacter::Update (own vtable slot 0xAD, AE 40447)
        // calls Character::Update directly, not through the vtable. So tick from here.
        struct PlayerCharacter_Update
        {
            static void thunk(RE::PlayerCharacter *a_player, float a_delta)
            {
                func(a_player, a_delta);
                CombatDirector::GetInstance().Update(RE::GetSecondsSinceLastFrame());
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        // The vanilla melee attack node (CombatBehaviorAttack) asks this for each candidate
        // attack's chance: float(attacker, target, BGSAttackData*) = SE 49748 / AE 50675.
        // Call site found by RE (SE 48139+0x2AE, AE 49170+0x28D); both versions have the same
        // two callers in the same order.
        struct MeleeAttackChance
        {
            static float thunk(RE::Actor *a_attacker, RE::Actor *a_target, RE::BGSAttackData *a_data)
            {
                const float chance = func(a_attacker, a_target, a_data);
                return VanillaAttackShaper::GetInstance().Adjust(a_attacker, a_target, a_data, chance);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        namespace
        {
            std::atomic<bool> g_daggerBlock{false};

            // Vanilla's one-handed weapon block (CombatInventoryItemOneHandedBlock::CheckShouldEquip, SE 43837 /
            // AE 45056) masks the right-hand weapon's animationType with 0xFD, refusing fists (0) and daggers (2).
            // 0xFF refuses only fists. SE: `and cl, 0xFD` at +0x72 (imm +0x74); AE 1.6.1170 and 1.7.104:
            // `test byte ptr [rax+0x19D], 0xFD` at +0x6B (imm +0x71).
            void PatchDaggerBlock()
            {
                if (!Config::GetInstance().GetDecisionMatrix().allowDaggerBlock || REL::Module::IsVR()) {
                    return;
                }
                REL::Relocation<std::uintptr_t> check{RELOCATION_ID(43837, 45056)};
                const auto imm = check.address() + REL::Relocate(0x74, 0x71);
                if (*reinterpret_cast<const std::uint8_t *>(imm) != 0xFD) {
                    LOG_WARN("Dagger block: unexpected byte at the weapon-block check - not patched (another mod?)");
                    return;
                }
                REL::safe_write(imm, std::uint8_t{0xFF});
                g_daggerBlock = true;
                LOG_INFO("Dagger block: NPCs with a dagger can block and bash");
            }
        } // namespace

        bool DaggerBlockPatched() { return g_daggerBlock; }

        void Install()
        {
            LOG_INFO("Installing hooks...");
            PatchDaggerBlock();

            // Trampoline is allocated once in SKSEPlugin_Load (the tree hooks need it before kDataLoaded)
            auto &trampoline = SKSE::GetTrampoline();

            // Hook Character::Update at vtable index 0xAD (inspired by ProjectGapClose)
            // Using RE::Character instead of RE::Actor for more reliable hooking
            REL::Relocation<std::uintptr_t> CharacterVTable{RE::Character::VTABLE[0]};

            // Write our hook function to the vtable
            // write_vfunc returns the original function pointer
            Actor_Update::func = CharacterVTable.write_vfunc(0x0AD, Actor_Update::thunk);

            REL::Relocation<std::uintptr_t> PlayerVTable{RE::PlayerCharacter::VTABLE[0]};
            PlayerCharacter_Update::func = PlayerVTable.write_vfunc(0x0AD, PlayerCharacter_Update::thunk);

            // Call-site offsets are only mapped for SE and AE. write_call returns the current
            // target, so a mod already hooking this site (e.g. FenixCombatAI) stays chained.
            if (!REL::Module::IsVR()) {
                REL::Relocation<std::uintptr_t> site{RELOCATION_ID(48139, 49170), REL::Relocate(0x2AE, 0x28D)};
                if (*reinterpret_cast<const std::uint8_t *>(site.address()) == 0xE8) {
                    MeleeAttackChance::func = trampoline.write_call<5>(site.address(), MeleeAttackChance::thunk);
                    LOG_INFO("Vanilla attack chance hook installed");
                } else {
                    LOG_WARN("Vanilla attack chance site is not a call - shaping disabled");
                }
            }

            EngineProbes::Install();

            LOG_INFO("Hooks installed successfully");
        }
    } // namespace Hooks
} // namespace CombatAI

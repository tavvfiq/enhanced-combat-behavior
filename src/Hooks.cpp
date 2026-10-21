#include "Hooks.h"
#include "ActorUtils.h"
#include "CombatDirector.h"
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

        void Install()
        {
            LOG_INFO("Installing hooks...");

            // Get trampoline
            auto &trampoline = SKSE::GetTrampoline();
            trampoline.create(128);

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

            LOG_INFO("Hooks installed successfully");
        }
    } // namespace Hooks
} // namespace CombatAI

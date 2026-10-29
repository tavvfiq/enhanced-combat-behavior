#pragma once

#include "ActorUtils.h"
#include "Hooks.h"
#include "pch.h"

// Which equipment lets an NPC block, and how: through vanilla's Block tree, or through our direct block node.
namespace CombatAI::BlockRules
{
    // Two copies of one one-handed weapon: both hands report the same form, as a two-handed weapon does, but it is
    // dual wield (1.5.1 run: a bandit with two iron daggers was sent to the Block tree 7 times, never taken)
    inline bool SameOneHanderInBothHands(const RE::TESForm *a_left, const RE::TESForm *a_right)
    {
        const auto *weapon = a_left && a_left == a_right ? a_left->As<RE::TESObjectWEAP>() : nullptr;
        if (!weapon) {
            return false;
        }
        switch (weapon->GetWeaponType()) {
        case RE::WEAPON_TYPE::kOneHandSword:
        case RE::WEAPON_TYPE::kOneHandDagger:
        case RE::WEAPON_TYPE::kOneHandAxe:
        case RE::WEAPON_TYPE::kOneHandMace:
            return true;
        default:
            return false;
        }
    }

    // Whether vanilla runs the Block tree (bash, block, parry, timed block) for this NPC. Its left-hand slot links the
    // Block tree for an item of type Shield: a real shield, or CombatInventoryItemOneHandedBlock, which reports the
    // same type (shared GetType SE 43824). Every humanoid race gets that item (SE 43626 -> 24852: race can equip a
    // melee weapon), and it equips (CheckShouldEquip SE 43837) only when the right-hand combat item is melee and its
    // weapon is not a dagger or fists (animationType & ~2 != 0). So daggers, fists, bows, staffs and crossbows
    // can't block; swords, axes, maces and two-handed weapons can. A spell or torch in the left hand takes the slot.
    inline bool CanRunBlockTree(RE::Actor *a_actor)
    {
        RE::TESForm *leftHand = ActorUtils::SafeGetEquippedObject(a_actor, true);
        RE::TESForm *rightHand = ActorUtils::SafeGetEquippedObject(a_actor, false);
        if (leftHand && leftHand->IsArmor()) {
            return true;
        }
        if ((leftHand && leftHand != rightHand) || SameOneHanderInBothHands(leftHand, rightHand)) {
            return false; // a spell, torch or second weapon holds the left slot
        }
        const auto *weapon = rightHand ? rightHand->As<RE::TESObjectWEAP>() : nullptr;
        if (!weapon) {
            return false;
        }
        switch (weapon->GetWeaponType()) {
        case RE::WEAPON_TYPE::kOneHandSword:
        case RE::WEAPON_TYPE::kOneHandAxe:
        case RE::WEAPON_TYPE::kOneHandMace:
        case RE::WEAPON_TYPE::kTwoHandSword:
        case RE::WEAPON_TYPE::kTwoHandAxe:
            return true;
        case RE::WEAPON_TYPE::kOneHandDagger:
            return Hooks::DaggerBlockPatched(); // AllowDaggerBlock
        default:
            return false;
        }
    }

    // A melee weapon in the right hand while a spell, torch or second weapon holds the left slot. Vanilla never
    // runs the Block tree then, but the graph blocks on ActorState wantBlocking + blockStart (Simple Dual Wield
    // Block's method; run 34: both untroubled tries blocked the hit). Our DualBlock tree node does that.
    inline bool CanDualBlock(RE::Actor *a_actor)
    {
        RE::TESForm *leftHand = ActorUtils::SafeGetEquippedObject(a_actor, true);
        RE::TESForm *rightHand = ActorUtils::SafeGetEquippedObject(a_actor, false);
        if (!leftHand || leftHand->IsArmor() || (leftHand == rightHand && !SameOneHanderInBothHands(leftHand, rightHand)) ||
            CanRunBlockTree(a_actor)) {
            return false;
        }
        const auto *weapon = rightHand ? rightHand->As<RE::TESObjectWEAP>() : nullptr;
        if (!weapon) {
            return false; // a pure caster has nothing to block with (run 34: 47 tries, none blocked)
        }
        switch (weapon->GetWeaponType()) {
        case RE::WEAPON_TYPE::kOneHandSword:
        case RE::WEAPON_TYPE::kOneHandDagger:
        case RE::WEAPON_TYPE::kOneHandAxe:
        case RE::WEAPON_TYPE::kOneHandMace:
            return true;
        default:
            return false;
        }
    }
} // namespace CombatAI::BlockRules

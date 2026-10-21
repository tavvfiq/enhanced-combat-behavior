#include "CombatStyleEnhancer.h"
#include "ActorUtils.h"
#include "Config.h"
#include "pch.h"

// Undefine Windows macros that conflict with std functions
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef clamp
#undef clamp
#endif

namespace CombatAI
{
    // Helper function to clamp values (avoids Windows macro issues)
    template <typename T> constexpr T ClampValue(const T &value, const T &minVal, const T &maxVal)
    {
        return (value < minVal) ? minVal : ((value > maxVal) ? maxVal : value);
    }
    RE::TESCombatStyle *CombatStyleEnhancer::GetCombatStyle(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return nullptr;
        }

        // Get from combat controller first (runtime)
        RE::CombatController *controller = ActorUtils::SafeGetCombatController(a_actor);
            if (controller && controller->combatStyle) {
            return controller->combatStyle;
        }

        // Fallback to actor base
        RE::TESNPC *base = ActorUtils::SafeGetActorBase(a_actor);
        if (base) {
            return base->GetCombatStyle();
        }

        return nullptr;
    }

    DecisionResult CombatStyleEnhancer::EnhanceDecision(RE::Actor *a_actor, const DecisionResult &a_baseDecision,
                                                        const ActorStateData &a_state)
    {
        if (!a_actor || a_baseDecision.action == ActionType::None) {
            return a_baseDecision;
        }

        DecisionResult enhanced = a_baseDecision;
        ApplyArmorStyle(enhanced, a_state);

        RE::TESCombatStyle *style = GetCombatStyle(a_actor);
        if (!style) {
            return enhanced; // No combat style, armor adjustment only
        }

        // Apply style-specific enhancements based on flags (mutually exclusive styles)
        if (style->flags.any(RE::TESCombatStyle::FLAG::kDuelingStyle)) {
            enhanced = EnhanceForDuelingStyle(a_actor, enhanced, a_state, style);
        } else if (style->flags.any(RE::TESCombatStyle::FLAG::kFlankingStyle)) {
            enhanced = EnhanceForFlankingStyle(a_actor, enhanced, a_state, style);
        }

        // Apply multipliers based on combat style data (affects all actions)
        ApplyStyleMultipliers(style, enhanced, a_state);

        // Determine style type from multipliers and enhance accordingly
        // These can stack with flag-based styles
        if (style->generalData.offensiveMult > style->generalData.defensiveMult * 1.5f) {
            enhanced = EnhanceForAggressiveStyle(a_actor, enhanced, a_state, style);
        } else if (style->generalData.defensiveMult > style->generalData.offensiveMult * 1.5f) {
            enhanced = EnhanceForDefensiveStyle(a_actor, enhanced, a_state, style);
        }

        if (style->generalData.magicScoreMult > 0.5f) {
            enhanced = EnhanceForMagicStyle(a_actor, enhanced, a_state, style);
        }

        if (style->generalData.rangedScoreMult > 0.5f) {
            enhanced = EnhanceForRangedStyle(a_actor, enhanced, a_state, style);
        }

        return enhanced;
    }

    void CombatStyleEnhancer::ApplyArmorStyle(DecisionResult &a_decision, const ActorStateData &a_state)
    {
        const auto &cfg = Config::GetInstance().GetArmorStyle();
        if (!cfg.enableArmorStyle || a_state.self.armorClass == ArmorClass::None) {
            return;
        }

        const ActionType action = a_decision.action;
        const bool evasive = action == ActionType::Dodge || action == ActionType::Strafe ||
                             action == ActionType::Jump || action == ActionType::ForwardDodge;

        if (a_state.self.armorClass == ArmorClass::Light) {
            // Light armor: mobile skirmisher
            if (evasive || action == ActionType::Flanking) {
                a_decision.priority += cfg.lightEvasionBonus;
            }
        } else {
            // Heavy armor: stand ground, trade blows behind the guard
            if (evasive) {
                a_decision.priority = (std::max)(0.0f, a_decision.priority - cfg.heavyEvasionPenalty);
            } else if (action == ActionType::Bash || action == ActionType::TimedBlock) {
                a_decision.priority += cfg.heavyGuardBonus;
            }
        }
    }

    bool CombatStyleEnhancer::HasMeleeWeapon(RE::Actor *a_actor) const
    {
        if (!a_actor)
            return false;
        auto weapon = ActorUtils::SafeGetEquippedObject(a_actor, false);
        if (weapon && weapon->IsWeapon()) {
            auto weap = weapon->As<RE::TESObjectWEAP>();
            return weap && !weap->IsBow() && !weap->IsCrossbow();
        }
        return false;
    }

    bool CombatStyleEnhancer::HasRangedWeapon(RE::Actor *a_actor) const
    {
        if (!a_actor)
            return false;
        auto weapon = ActorUtils::SafeGetEquippedObject(a_actor, false);
        if (weapon && weapon->IsWeapon()) {
            auto weap = weapon->As<RE::TESObjectWEAP>();
            return weap && (weap->IsBow() || weap->IsCrossbow());
        }
        return false;
    }

    bool CombatStyleEnhancer::HasMagicEquipped(RE::Actor *a_actor) const
    {
        if (!a_actor)
            return false;
        // Check if casting or has spell equipped
        return ActorUtils::SafeWhoIsCasting(a_actor) != 0 ||
               ActorUtils::SafeGetEquippedObject(a_actor, true) != nullptr;
    }

    DecisionResult CombatStyleEnhancer::EnhanceForDuelingStyle([[maybe_unused]] RE::Actor *a_actor,
                                                               const DecisionResult &a_decision,
                                                               const ActorStateData &a_state,
                                                               RE::TESCombatStyle *a_style)
    {
        DecisionResult result = a_decision;

        // Dueling style: More focused on 1v1, prefers bash interrupts and precise attacks - subtle nudge
        if (result.action == ActionType::Bash) {
            result.priority += 0.15f; // Subtle boost
        }

        if (result.action == ActionType::Dodge) {
            result.priority += 0.15f; // Subtle boost
        }

        // Prefer power attacks when stamina allows (duelists are precise)
        if (result.action == ActionType::PowerAttack && HasMeleeWeapon(a_actor)) {
            result.priority += 0.15f; // Subtle boost
        }

        // Less likely to retreat, use combat style fallback threshold
        if (result.action == ActionType::Retreat) {
            float retreatThreshold = a_style ? a_style->closeRangeData.fallbackMult * 0.1f : 0.15f;
            if (a_state.self.healthPercent > retreatThreshold) {
                result.priority = (std::max)(0.0f, result.priority - 0.3f);
            }
        }

        // Dueling style: Less likely to backoff (prefer dodging/strafe for positioning)
        if (result.action == ActionType::Backoff) {
            result.priority = (std::max)(0.0f, result.priority - 0.2f); // Prefer dodging over backing off
        }

        // Don't use sprint attacks (duelists prefer precision)
        if (result.action == ActionType::SprintAttack) {
            result.priority = (std::max)(0.0f, result.priority - 0.3f);
        }

        return result;
    }

    DecisionResult CombatStyleEnhancer::EnhanceForFlankingStyle([[maybe_unused]] RE::Actor *a_actor,
                                                                const DecisionResult &a_decision,
                                                                const ActorStateData &a_state,
                                                                RE::TESCombatStyle *a_style)
    {
        DecisionResult result = a_decision;

        // Flanking style: Prefers movement and positioning - subtle nudge
        if (result.action == ActionType::Strafe || result.action == ActionType::Dodge) {
            result.priority += 0.15f; // Subtle boost
            // Use circle multiplier for intensity if available (more conservative)
            if (a_style) {
                float circleMult = a_style->closeRangeData.circleMult;
                result.intensity = (std::min)(1.0f, result.intensity * (1.0f + circleMult * 0.1f)); // Reduced from 0.2f
            } else {
                result.intensity = (std::min)(1.0f, result.intensity * 1.1f); // Reduced from 1.2f
            }
        }

        // Flanking style: Less likely to backoff (prefer strafe/circling instead)
        if (result.action == ActionType::Backoff) {
            result.priority = (std::max)(0.0f, result.priority - 0.3f); // Prefer tactical positioning over backing off
        }

        // Prefer sprint attacks for flanking (closing distance quickly)
        if (result.action == ActionType::SprintAttack) {
            result.priority += 0.15f; // Subtle boost
        }

        // Less likely to use power attacks (flankers prefer mobility)
        if (result.action == ActionType::PowerAttack) {
            result.priority = (std::max)(0.0f, result.priority - 0.3f);
        }

        return result;
    }

    RE::NiPoint3 CombatStyleEnhancer::CalculateStrafeDirection(const ActorStateData &a_state)
    {
        if (!a_state.target.isValid) {
            return RE::NiPoint3(1.0f, 0.0f, 0.0f);
        }
        RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
        toTarget.z = 0.0f;
        toTarget.Unitize();
        RE::NiPoint3 strafeDir(-toTarget.y, toTarget.x, 0.0f);
        strafeDir.Unitize();
        return strafeDir;
    }

    DecisionResult CombatStyleEnhancer::EnhanceForAggressiveStyle([[maybe_unused]] RE::Actor *a_actor,
                                                                  const DecisionResult &a_decision,
                                                                  const ActorStateData &a_state,
                                                                  RE::TESCombatStyle *a_style)
    {
        DecisionResult result = a_decision;

        if (result.action == ActionType::Advancing) {
            result.priority += 0.15f; // Subtle boost
        }

        // Aggressive style: Less defensive, more offensive
        if (result.action == ActionType::Retreat) {
            // Use combat style fallback multiplier to determine retreat threshold
            float retreatThreshold = a_style ? a_style->closeRangeData.fallbackMult * 0.05f : 0.1f;
            if (a_state.self.healthPercent > retreatThreshold) {
                result.priority = (std::max)(0.0f, result.priority - 0.3f);
            }
        }

        // Aggressive style: Less likely to backoff (prefer pressing forward or strafing) - subtle
        if (result.action == ActionType::Backoff) {
            // Only reduce priority if health is reasonable (aggressive NPCs don't backoff easily)
            if (a_state.self.healthPercent > 0.3f) {
                result.priority = (std::max)(0.0f, result.priority - 0.15f); // Reduced from 0.4f
            } else {
                // Even aggressive NPCs backoff when very low on health
                result.priority = (std::max)(0.0f, result.priority - 0.08f); // Reduced from 0.2f
            }
        }

        // Boost offensive actions - subtle nudge only (vanilla AI already handles combat style)
        if (result.action == ActionType::Bash || result.action == ActionType::Attack ||
            result.action == ActionType::PowerAttack || result.action == ActionType::SprintAttack) {
            result.priority += 0.1f; // Subtle boost - vanilla AI already respects combat style
        }

        // Prefer power attacks and sprint attacks - subtle nudge
        if (result.action == ActionType::PowerAttack || result.action == ActionType::SprintAttack) {
            result.priority += 0.08f; // Subtle boost
        }

        // Less likely to dodge/strafe (aggressive = press forward) - subtle reduction
        if (result.action == ActionType::Dodge || result.action == ActionType::Strafe) {
            result.priority = (std::max)(0.0f, result.priority - 0.1f); // Subtle penalty
        }

        return result;
    }

    DecisionResult CombatStyleEnhancer::EnhanceForDefensiveStyle([[maybe_unused]] RE::Actor *a_actor,
                                                                 const DecisionResult &a_decision,
                                                                 const ActorStateData &a_state,
                                                                 RE::TESCombatStyle *a_style)
    {
        DecisionResult result = a_decision;

        // Defensive style: More cautious, prefers evasion and retreat - subtle nudge
        if (result.action == ActionType::Retreat || result.action == ActionType::Backoff) {
            result.priority += 0.15f; // Subtle boost - vanilla AI already handles defensive style
        }

        if (result.action == ActionType::Strafe || result.action == ActionType::Dodge ||
            result.action == ActionType::Jump) {
            result.priority += 0.1f; // Subtle boost
        }

        // Less likely to use power attacks (defensive = cautious)
        if (result.action == ActionType::PowerAttack || result.action == ActionType::SprintAttack) {
            result.priority = (std::max)(0.0f, result.priority - 0.3f);
        }

        return result;
    }

    DecisionResult CombatStyleEnhancer::EnhanceForMagicStyle([[maybe_unused]] RE::Actor *a_actor,
                                                             const DecisionResult &a_decision,
                                                             const ActorStateData &a_state, RE::TESCombatStyle *a_style)
    {
        DecisionResult result = a_decision;

        // Only apply if actually using magic
        if (!HasMagicEquipped(a_actor) && !HasRangedWeapon(a_actor)) {
            return result;
        }

        // Magic style: Prefers distance, less melee
        if (result.action == ActionType::Bash) {
            result.priority = (std::max)(0.0f, result.priority - 0.4f); // Strongly discourage bash
        }

        // Discourage melee attacks
        if (result.action == ActionType::Attack || result.action == ActionType::PowerAttack) {
            result.priority = (std::max)(0.0f, result.priority - 0.3f);
        }

        // Prefer strafe/dodge/backoff to maintain range - subtle nudge
        if (result.action == ActionType::Strafe || result.action == ActionType::Dodge ||
            result.action == ActionType::Retreat || result.action == ActionType::Backoff) {
            result.priority += 0.15f; // Subtle boost
        }

        return result;
    }

    DecisionResult CombatStyleEnhancer::EnhanceForRangedStyle([[maybe_unused]] RE::Actor *a_actor,
                                                              const DecisionResult &a_decision,
                                                              [[maybe_unused]] const ActorStateData &a_state,
                                                              RE::TESCombatStyle *a_style)
    {
        DecisionResult result = a_decision;

        // Only apply if actually using ranged weapon
        if (!HasRangedWeapon(a_actor)) {
            return result;
        }

        // Ranged style: Similar to magic, prefers distance
        if (result.action == ActionType::Bash) {
            result.priority = (std::max)(0.0f, result.priority - 0.4f); // Strongly discourage bash
        }

        // Discourage melee attacks
        if (result.action == ActionType::Attack || result.action == ActionType::PowerAttack ||
            result.action == ActionType::SprintAttack) {
            result.priority = (std::max)(0.0f, result.priority - 0.3f);
        }

        // Prefer strafe, dodge, jump, or backoff to maintain optimal range - subtle nudge
        if (result.action == ActionType::Strafe || result.action == ActionType::Dodge ||
            result.action == ActionType::Jump || result.action == ActionType::Retreat ||
            result.action == ActionType::Backoff) {
            result.priority += 0.15f; // Subtle boost
        }

        return result;
    }

    void CombatStyleEnhancer::ApplyStyleMultipliers(RE::TESCombatStyle *a_style, DecisionResult &a_decision,
                                                    const ActorStateData &a_state)
    {
        if (!a_style) {
            return;
        }

        // Apply melee multipliers - subtle adjustments (vanilla AI already uses these)
        if (a_decision.action == ActionType::Bash) {
            float bashMult = a_style->meleeData.bashMult;
            if (bashMult > 1.0f) {
                a_decision.priority += 0.1f; // Reduced from 0.2f
            } else if (bashMult < 0.5f) {
                a_decision.priority = (std::max)(0.0f, a_decision.priority - 0.1f); // Reduced from 0.2f
            }
        }

        // Apply power attack multipliers (affects PowerAttack action) - subtle adjustments
        if (a_decision.action == ActionType::PowerAttack) {
            float powerAttackBlockingMult = a_style->meleeData.powerAttackBlockingMult;
            if (powerAttackBlockingMult > 1.0f) {
                a_decision.priority += 0.1f; // Reduced from 0.2f
            } else if (powerAttackBlockingMult < 0.5f) {
                a_decision.priority = (std::max)(0.0f, a_decision.priority - 0.1f); // Reduced from 0.2f
            }
        }

        // Apply close range multipliers (affects movement actions)
        if ((a_decision.action == ActionType::Strafe || a_decision.action == ActionType::Dodge) &&
            a_state.target.distance < 200.0f) {
            float circleMult = a_style->closeRangeData.circleMult;
            a_decision.intensity *= circleMult;
            a_decision.intensity = ClampValue(a_decision.intensity, 0.0f, 1.0f);
        }

        // Apply fallback multiplier (affects retreat) - subtle adjustments
        if (a_decision.action == ActionType::Retreat) {
            float fallbackMult = a_style->closeRangeData.fallbackMult;
            if (fallbackMult > 1.0f) {
                a_decision.priority += 0.1f; // Reduced from 0.2f
            } else if (fallbackMult < 0.5f) {
                a_decision.priority = (std::max)(0.0f, a_decision.priority - 0.1f); // Reduced from 0.2f
            }

            // Personality/trait-based adjustments for retreat
            // DecisionMatrix handles outnumbering logic, here we adjust based on combat style traits
            // Check if NPC has defensive/cowardly traits based on combat style
            // High defensiveMult, high fallbackMult, or high avoidThreatChance indicate defensive/cowardly behavior
            bool isDefensiveOrCowardly =
                (a_style->generalData.defensiveMult > a_style->generalData.offensiveMult * 1.5f) ||
                (fallbackMult > 1.2f) || (a_style->generalData.avoidThreatChance > 0.6f);

            // Apply trait-based adjustments (regardless of outnumbering - DecisionMatrix handles that)
            if (isDefensiveOrCowardly) {
                // Defensive/cowardly NPCs - boost retreat priority (they're naturally more cautious)
                a_decision.priority += 0.15f;
            } else {
                // Aggressive/brave NPCs - reduce retreat priority slightly (they're more willing to fight)
                a_decision.priority = (std::max)(0.0f, a_decision.priority - 0.1f);
            }
        }

        // Apply avoid threat chance (affects all evasion actions) - subtle adjustments
        if (a_decision.action == ActionType::Strafe || a_decision.action == ActionType::Dodge ||
            a_decision.action == ActionType::Jump) {
            float avoidThreat = a_style->generalData.avoidThreatChance;
            if (avoidThreat > 0.5f) {
                a_decision.priority += 0.1f; // Reduced from 0.2f
            } else if (avoidThreat < 0.3f) {
                a_decision.priority = (std::max)(0.0f, a_decision.priority - 0.1f); // Reduced from 0.2f
            }
        }

        // Apply melee score multiplier (affects Attack and PowerAttack)
        // Higher melee score means more likely to attack
        if (a_decision.action == ActionType::Attack) {
            float meleeScoreMult = a_style->generalData.meleeScoreMult;
            if (meleeScoreMult > 1.0f) {
                a_decision.priority += 0.2f;
            } else if (meleeScoreMult < 0.5f) {
                a_decision.priority = (std::max)(0.0f, a_decision.priority - 0.2f);
            }
        } else if (a_decision.action == ActionType::PowerAttack) {
            // Power attacks also benefit from melee score
            float meleeScoreMult = a_style->generalData.meleeScoreMult;
            if (meleeScoreMult > 1.0f) {
                a_decision.priority += 0.2f;
            }
        }
    }
} // namespace CombatAI

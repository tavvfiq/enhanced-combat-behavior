#include "Humanizer.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Boss.h"
#include "Logger.h"
#include "pch.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <random>
#include <tuple>

namespace CombatAI
{
    namespace
    {
        // Thread-local random number generators (one per thread)
        thread_local std::random_device g_rd;
        thread_local std::mt19937 g_gen(g_rd());
    } // namespace

    bool Humanizer::CanReact(RE::Actor *a_actor, RE::FormID a_formID)
    {
        if (!a_actor || a_formID == RE::FormID(0)) {
            return false;
        }

        // Only process actors in combat - clean up if not in combat
        if (!ActorUtils::SafeIsInCombat(a_actor)) {
            // Remove reaction state if actor left combat
            m_reactionStates.Erase(a_formID);
            m_skillRolls.Erase(a_formID);
            return false;
        }

        // Roll outside the lock (reads the actor); only used if the entry is new.
        const float now = m_time.load(std::memory_order_relaxed);
        const float delay = RollReactionDelaySeconds(a_actor);
        bool canReact = false;
        m_reactionStates.ModifyOrCreate(a_formID, [&](ActorReactionState &state) {
            if (state.entryReadyTime < 0.0f) {
                state.entryReadyTime = now + delay;
            }
            canReact = now >= state.entryReadyTime;
        });
        return canReact;
    }

    Humanizer::SkillClass Humanizer::ClassOf(ActionType a_action)
    {
        switch (a_action) {
        case ActionType::Dodge:
        case ActionType::Parry:
        case ActionType::TimedBlock:
        case ActionType::Bash:
        case ActionType::Jump:
            return SkillClass::Defense;
        case ActionType::Attack:
        case ActionType::PowerAttack:
        case ActionType::SprintAttack:
        case ActionType::Feint:
            return SkillClass::Offense;
        default:
            return SkillClass::None;
        }
    }

    namespace
    {
        // A boss's skill tops out here, so it can still err (the bonus used to push level 14+ bosses to 1.0: no
        // failures, and fatigue and panic multiplied 0)
        constexpr float kBossSkillCap = 0.97f;

        constexpr float Skill(float a_levelSkill, float a_bossBonus, bool a_boss)
        {
            return a_boss ? std::clamp(a_levelSkill + a_bossBonus, 0.0f, kBossSkillCap) : std::clamp(a_levelSkill, 0.0f, 1.0f);
        }
        static_assert(Skill(0.95f, 0.15f, true) == kBossSkillCap && Skill(0.5f, 0.25f, true) == 0.75f &&
                      Skill(0.95f, 0.15f, false) == 0.95f);

        // The skill an NPC's weapon uses (fists: one-handed)
        RE::ActorValue WeaponSkill(RE::Actor *a_actor)
        {
            const auto *object = a_actor ? a_actor->GetEquippedObject(false) : nullptr;
            const auto *weapon = object ? object->As<RE::TESObjectWEAP>() : nullptr;
            return weapon ? weapon->weaponData.skill.get() : RE::ActorValue::kOneHanded;
        }

        float Value(RE::Actor *a_actor, RE::ActorValue a_value)
        {
            auto *owner = a_value != RE::ActorValue::kNone ? ActorUtils::SafeAsActorValueOwner(a_actor) : nullptr;
            return owner ? owner->GetActorValue(a_value) : 0.0f;
        }

        // The NPC's actor value an action rolls on, and the target's value it is measured against:
        // parry / timed block / bash: Block vs the target's weapon skill; dodge / jump: the best of OneHanded,
        // TwoHanded and Block vs the target's weapon skill; attacks and feints: its weapon skill vs the target's Block
        std::pair<float, float> Values(RE::Actor *a_self, RE::Actor *a_target, ActionType a_action)
        {
            if (!a_self || !a_target) {
                return {0.0f, 0.0f};
            }
            switch (a_action) {
            case ActionType::Parry:
            case ActionType::TimedBlock:
            case ActionType::Bash:
                return {Value(a_self, RE::ActorValue::kBlock), Value(a_target, WeaponSkill(a_target))};
            case ActionType::Dodge:
            case ActionType::Jump:
                return {(std::max)({Value(a_self, RE::ActorValue::kOneHanded), Value(a_self, RE::ActorValue::kTwoHanded),
                                    Value(a_self, RE::ActorValue::kBlock)}),
                        Value(a_target, WeaponSkill(a_target))};
            default:
                return {Value(a_self, WeaponSkill(a_self)), Value(a_target, RE::ActorValue::kBlock)};
            }
        }

        // Skill from the actor-value gap: a_weight per 100 points, the gap clamped to +-a_maxGap (a player with
        // fortified skills past 100 would otherwise make NPCs fail half their rolls); 0 when either side has none
        constexpr float GapSkill(float a_self, float a_target, float a_weight, float a_maxGap)
        {
            return a_self > 0.0f && a_target > 0.0f ? std::clamp(a_self - a_target, -a_maxGap, a_maxGap) * a_weight / 100.0f
                                                    : 0.0f;
        }
        static_assert(GapSkill(50.0f, 30.0f, 0.4f, 50.0f) == 0.08f && GapSkill(20.0f, 120.0f, 0.4f, 50.0f) == -0.2f &&
                      GapSkill(0.0f, 50.0f, 0.4f, 50.0f) == 0.0f);

        // Exertion: stamina spent decays with time constant a_seconds; returns the new spent amount
        float Decayed(float a_spent, float a_last, float a_now, float a_elapsed, float a_seconds)
        {
            const float decayed = a_spent * std::exp(-a_elapsed / (std::max)(1.0f, a_seconds));
            return decayed + (a_last >= 0.0f && a_now < a_last ? a_last - a_now : 0.0f);
        }

        // Each actor's offense window starts at its own offset (0..1 window): they used to re-roll on one shared tick
        constexpr float WindowPhase(RE::FormID a_formID)
        {
            return static_cast<float>((a_formID * 2654435761u) >> 8) / 16777216.0f;
        }
        static_assert(WindowPhase(0) == 0.0f && WindowPhase(0xFFFFFFFF) < 1.0f && WindowPhase(1) != WindowPhase(2));
    } // namespace

    bool Humanizer::PassesSkillCheck(RE::Actor *a_actor, RE::FormID a_formID, ActionType a_action,
                                     const ActorStateData &a_state, float a_combatTime)
    {
        const SkillClass skillClass = ClassOf(a_action);
        if (!m_config.enableSkillRolls || skillClass == SkillClass::None || !a_actor) {
            return true;
        }

        // Opportunity key. Defense: the target's current swing (its start time identifies it),
        // so one roll covers the whole swing. Otherwise a time window, so a failed roll
        // passes on that opening and a new one comes after the window.
        const float now = m_time.load(std::memory_order_relaxed);
        const float window = (std::max)(0.1f, m_config.skillRerollSeconds);
        std::uint64_t key = static_cast<std::uint64_t>(now / window + WindowPhase(a_formID)) | (1ull << 63);
        const auto &target = a_state.target;
        if (skillClass == SkillClass::Defense && target.isValid && (target.isAttacking || target.isPowerAttacking) &&
            a_state.temporal.target.attackStartTime >= 0.0f) {
            key = (static_cast<std::uint64_t>(target.targetFormID) << 32) | a_state.temporal.target.swingCount;
        }

        const auto parts = ExplainSkill(a_actor, a_action, a_combatTime, a_state);
        bool passed = true;
        bool rolled = false;
        m_skillRolls.ModifyOrCreate(a_formID, [&](SkillState &state) {
            auto &storedKey = skillClass == SkillClass::Defense ? state.defenseKey : state.offenseKey;
            auto &storedPassed = skillClass == SkillClass::Defense ? state.defensePassed : state.offensePassed;
            if (storedKey != key) {
                storedKey = key;
                storedPassed = std::uniform_real_distribution<float>(0.0f, 1.0f)(g_gen) < parts.chance;
                rolled = true;
            }
            passed = storedPassed;
        });
        if (rolled && Config::GetInstance().IsDebugTarget(a_formID)) {
            LOG_DEBUG("Roll 0x{:08X}: {} {} skill {:.2f} (level {:.2f}, actor value {:.0f} vs {:.0f} -> {:+.2f}, fatigue "
                      "{:.2f}, panic {:.2f}) -> {}",
                      a_formID, skillClass == SkillClass::Defense ? "defense" : "offense", ActionName(a_action),
                      parts.chance, parts.level, parts.selfValue, parts.targetValue, parts.gapSkill, parts.fatigue,
                      parts.panic, passed ? "pass" : "fail");
        }
        return passed;
    }

    bool Humanizer::RollBackoff(RE::FormID a_formID, std::uint64_t a_key, float a_chance)
    {
        bool passed = true;
        m_skillRolls.ModifyOrCreate(a_formID, [&](SkillState &state) {
            if (state.backoffKey != a_key) {
                state.backoffKey = a_key;
                state.backoffPassed = std::uniform_real_distribution<float>(0.0f, 1.0f)(g_gen) < a_chance;
            }
            passed = state.backoffPassed;
        });
        return passed;
    }

    Humanizer::SkillParts Humanizer::ExplainSkill(RE::Actor *a_actor, ActionType a_action, float a_combatTime,
                                                  const ActorStateData &a_state) const
    {
        SkillParts parts;
        const float level = static_cast<float>(ActorUtils::SafeGetLevel(a_actor));
        parts.level = (std::clamp)(m_config.skillBase + m_config.skillPerLevel * (level - 1.0f), m_config.skillMin,
                                   m_config.skillMax);
        // Level sets the baseline; the actor-value gap to this target moves it (two bandits of one level now differ by
        // class, and a strong opponent makes an NPC sloppier). A gap on top of level, not instead: replacing level
        // would drop every level 26+ NPC from 95% to ~75% skill.
        if (m_config.skillActorValueWeight > 0.0f && a_state.target.isValid) {
            auto *target = RE::TESForm::LookupByID<RE::Actor>(a_state.target.targetFormID);
            std::tie(parts.selfValue, parts.targetValue) = Values(a_actor, target, a_action);
            parts.gapSkill = GapSkill(parts.selfValue, parts.targetValue, m_config.skillActorValueWeight,
                                      m_config.skillActorValueMaxGap);
        }
        // A boss makes fewer mistakes (+0.15 skill, capped at 0.97)
        const bool boss = Boss::Get(a_actor).boss;
        const float skill = Skill((std::clamp)(parts.level + parts.gapSkill, m_config.skillMin, m_config.skillMax),
                                  Config::GetInstance().GetBoss().skillBonus, boss);
        // Fatigue and panic scale the failure chance: x(1 + penalty) at full fatigue/panic.
        // (Dividing skill instead turned a 95% veteran into 63% after three minutes.)
        // A boss keeps its nerve until the desperate phase, where it turns reckless.
        const bool panics = !boss || Boss::CurrentPhase(a_actor) == Boss::Phase::Desperate;
        std::tie(parts.fatigue, parts.panic) =
            CalculateStress(a_actor ? a_actor->GetFormID() : 0, a_action, a_combatTime, a_state.self, panics);
        const float failure = (1.0f - skill) * (1.0f + parts.fatigue * m_config.fatigueSkillPenalty +
                                                parts.panic * m_config.panicSkillPenalty);
        parts.chance = (std::max)(0.0f, 1.0f - failure);
        return parts;
    }

    void Humanizer::TrackExertion(RE::FormID a_formID, float a_stamina, float a_maxStamina, float a_elapsed)
    {
        const float seconds = m_config.fatigueExertionSeconds;
        m_exertion.ModifyOrCreate(a_formID, [&](Exertion &e) {
            e.spent = Decayed(e.spent, e.last, a_stamina, a_elapsed, seconds);
            e.last = a_stamina;
            e.fraction = a_maxStamina > 0.0f ? (std::min)(1.0f, e.spent / a_maxStamina) : 0.0f;
        });
    }

    std::pair<float, float> Humanizer::CalculateStress(RE::FormID a_formID, ActionType a_action, float a_combatTime,
                                                       const SelfState &a_self, bool a_panics) const
    {
        float fatigue = 0.0f;
        if (m_config.enableFatigue) {
            if (m_config.fatigueFromExertion) {
                // Stamina spent lately (a full bar within ~FatigueExertionSeconds = full fatigue), not the fight's
                // length: a 3-minute boss fight no longer makes the boss sloppy from the clock alone
                fatigue = m_exertion.Find(a_formID).value_or(Exertion{}).fraction;
            } else {
                const float span = (std::max)(1.0f, m_config.fatigueFullSeconds - m_config.fatigueOnsetSeconds);
                fatigue = (std::clamp)((a_combatTime - m_config.fatigueOnsetSeconds) / span, 0.0f, 1.0f);
            }
            if (m_config.fatigueStaminaThreshold > 0.0f && a_self.staminaPercent < m_config.fatigueStaminaThreshold) {
                fatigue = (std::max)(fatigue, 1.0f - a_self.staminaPercent / m_config.fatigueStaminaThreshold);
            }
        }

        // Panic makes fighting sloppier, not running away.
        float panic = 0.0f;
        const bool fleeing = a_action == ActionType::Retreat || a_action == ActionType::Backoff;
        if (m_config.enablePanic && a_panics && !fleeing && m_config.panicHealthThreshold > 0.0f &&
            a_self.healthPercent < m_config.panicHealthThreshold) {
            panic = 1.0f - a_self.healthPercent / m_config.panicHealthThreshold;
        }

        return {fatigue, panic};
    }

    bool Humanizer::IsOnCooldown(RE::FormID a_formID, ActionType a_action)
    {
        // Validate FormID before using it
        if (a_formID == RE::FormID(0)) {
            return true; // Invalid FormID - safe default (on cooldown)
        }

        // Read the cooldown atomically. Holding the outer map's lock for the lookup
        // prevents another thread from erasing the actor entry (and thus the nested
        // cooldown map) while we are reading from it.
        bool onCooldown = false;
        m_cooldownStates.Modify(a_formID, [&](ActorCooldownState &cooldownState) {
            auto cooldownOpt = cooldownState.cooldowns.Find(a_action);
            onCooldown = cooldownOpt.has_value() && cooldownOpt.value() > 0.0f;
        });

        return onCooldown;
    }

    float Humanizer::GetCooldownForAction(ActionType a_action) const
    {
        // No action cooldowns: the NPC's own state gates repeats (stamina for dodge, jump and bash; not
        // mid-swing; a sustained sprint for a sprint attack). Only the timed-block settle hold is timed.
        switch (a_action) {
        case ActionType::TimedBlock:
            // Briefly preserve the block animation before a movement decision can release it.
            return 0.15f;
        case ActionType::ForwardDodge:
            // A risky gap-close, not a habit (on cooldown the director turns it into a plain advance)
            return Config::GetInstance().GetDodgeSystem().forwardDodgeCooldownSeconds;
        default:
            // Actions without cooldown return 0
            return 0.0f;
        }
    }

    void Humanizer::MarkActionUsed(RE::FormID a_formID, ActionType a_action)
    {
        float cooldown = GetCooldownForAction(a_action);
        if (cooldown <= 0.0f) {
            return; // No cooldown for this action
        }

        // Validate FormID before using it
        if (a_formID == RE::FormID(0)) {
            return; // Invalid FormID
        }

        // Get-or-create the actor's cooldown state and record the cooldown atomically.
        m_cooldownStates.ModifyOrCreate(
            a_formID, [&](ActorCooldownState &cooldownState) { cooldownState.cooldowns.Emplace(a_action, cooldown); });
    }

    void Humanizer::Update(float a_deltaTime)
    {
        m_time.store(m_time.load(std::memory_order_relaxed) + a_deltaTime, std::memory_order_relaxed);

        // Update cooldowns and clean up expired ones
        // Use thread-safe iteration with write lock
        m_cooldownStates.WithWriteLock([&](auto &cooldownStatesMap) {
            auto actorIt = cooldownStatesMap.begin();
            while (actorIt != cooldownStatesMap.end()) {
                auto &cooldownState = actorIt->second;

                // Update nested cooldown map (also thread-safe)
                cooldownState.cooldowns.WithWriteLock([&](auto &cooldownsMap) {
                    auto it = cooldownsMap.begin();
                    while (it != cooldownsMap.end()) {
                        if (it->second > 0.0f) {
                            it->second -= a_deltaTime;
                            if (it->second <= 0.0f) {
                                // Cooldown expired, remove from map to save memory
                                it = cooldownsMap.erase(it);
                            } else {
                                ++it;
                            }
                        } else {
                            // Already zero or negative, remove it
                            it = cooldownsMap.erase(it);
                        }
                    }
                });

                // If all cooldowns expired, remove the entire actor entry
                cooldownState.cooldowns.WithReadLock([&](const auto &cooldownsMap) {
                    if (cooldownsMap.empty()) {
                        actorIt = cooldownStatesMap.erase(actorIt);
                    } else {
                        ++actorIt;
                    }
                });
            }
        });
    }

    void Humanizer::Cleanup()
    {
        // With FormID keys, cleanup is much simpler
        // FormIDs are stable identifiers - they don't become invalid
        // We rely on lazy cleanup: entries are removed when actors leave combat
        // (checked in CanReact() when actor is not in combat)
        // This avoids expensive LookupByID() calls that could crash

        // Note: We don't need to do anything here since:
        // 1. FormIDs don't become invalid (unlike pointers)
        // 2. Entries are cleaned up lazily in CanReact() when actors leave combat
        // 3. Expired cooldowns are cleaned up in Update()
        // 4. Avoiding LookupByID() prevents crashes from invalid FormIDs or deleted forms

        // If we want to be more aggressive, we could add a size limit and remove oldest entries
        // But for now, lazy cleanup is safer and more efficient
    }

    float Humanizer::CalculateReactionDelay(std::uint16_t a_level) const
    {
        // Calculate reaction delay using linear reduction
        float reduction = static_cast<float>(a_level) * m_config.reactionDelayReductionPerLevelMs;
        return (std::max)(m_config.minReactionDelayMs, m_config.baseReactionDelayMs - reduction);
    }

    float Humanizer::RollReactionDelaySeconds(RE::Actor *a_actor) const
    {
        const float baseDelayMs = CalculateReactionDelay(ActorUtils::SafeGetLevel(a_actor));
        std::uniform_real_distribution<float> dist(0.0f, m_config.reactionVarianceMs);
        return (baseDelayMs + dist(g_gen)) / 1000.0f;
    }
} // namespace CombatAI

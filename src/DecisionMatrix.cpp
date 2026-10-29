#include "DecisionMatrix.h"
#include "ActorUtils.h"
#include "Config.h"
#include "BlockRules.h"
#include "Intents.h"
#include "Personality.h"
#include "Boss.h"
#include "Logger.h"
#include "ProjectileAwareness.h"
#include "SCARIntegration.h"
#include "TimedBlockIntegration.h"
#include "pch.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace CombatAI
{
    // Helper function to clamp values
    template <typename T> constexpr T ClampValue(const T &value, const T &minVal, const T &maxVal)
    {
        return (std::max)(minVal, (std::min)(maxVal, value));
    }

    // Effective melee reach in game units. Delegates to the shared StateHelpers
    // formula so the decision matrix and the observer's range categories agree.
    // True when a shooter a_distance away, whose facing has dot a_dot with the direction to us, would put the
    // arrow within a_radius of our centre. A plain cone (dot > 0.9) let every NPC within ~25 degrees jump.
    constexpr bool IsAimedAt(float a_distance, float a_dot, float a_radius)
    {
        return a_dot > 0.0f && a_distance * a_distance * (1.0f - a_dot * a_dot) < a_radius * a_radius;
    }
    static_assert(IsAimedAt(800.0f, 0.999f, 100.0f));  // ~36 units off the line
    static_assert(!IsAimedAt(800.0f, 0.95f, 100.0f));  // ~250 units off: the NPC next to the one aimed at
    static_assert(!IsAimedAt(800.0f, -0.999f, 100.0f)); // aiming away
    constexpr float kAimSlack = 75.0f; // third-person camera offset and aim wobble, on top of our body radius

    // The target's swing or shot points at this NPC, not a neighbour. The player's body heading misses its bow
    // crosshair by the camera offset (run 11), so the actor under the crosshair counts too.
    static bool TargetAimedAtUs(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (a_state.target.isPlayer) {
            if (auto *player = RE::PlayerCharacter::GetSingleton();
                player && player->GetInfoRuntimeData().autoAimActor.get().get() == a_actor) {
                return true;
            }
        }
        return IsAimedAt(a_state.target.distance, a_state.target.orientationDot, a_state.self.boundRadius + kAimSlack);
    }

    // Bash reach (GMST fCombatBashReach, read once; SCAR uses it for bash actions)
    static float BashReach()
    {
        static const float reach = [] {
            auto *settings = RE::GameSettingCollection::GetSingleton();
            auto *setting = settings ? settings->GetSetting("fCombatBashReach") : nullptr;
            const float value = setting ? setting->GetFloat() : 60.0f;
            LOG_INFO("Bash reach {:.1f} (fCombatBashReach{})", value, setting ? "" : " not found, using 60");
            return value;
        }();
        return reach;
    }

    // Stamina vanilla's bash needs (GMST fStaminaBashBase, read once): below it StartAttack refuses the bash
    static float BashStaminaCost()
    {
        static const float cost = [] {
            auto *settings = RE::GameSettingCollection::GetSingleton();
            auto *setting = settings ? settings->GetSetting("fStaminaBashBase") : nullptr;
            const float value = setting ? setting->GetFloat() : 0.0f;
            LOG_INFO("Bash stamina cost {:.1f} (fStaminaBashBase{})", value, setting ? "" : " not found, gate off");
            return value;
        }();
        return cost;
    }

    // A bash needs the target in front (vanilla's hit cone) and inside bash reach. Run 58: 21 of 26 bashes of one NPC
    // never swung; StartAttack's hit cone failed them with the target at 135-166 deg, 62-81 units away.
    // Vanilla's Attack and Bash nodes start nothing unless the graph's IsAttackReady is true (SE 37644 / 37645,
    // AE 38597 / 38598, FixedStrings +0x320 "IsAttackReady", called right before StartAttack at SE 48147+0x164 /
    // 46638+0x160). An intent posted while it is false is taken by the tree and wasted: run 59's Marauder Champion
    // had 9 of 9 bashes taken with no StartAttack. A graph without the variable counts as ready.
    static bool AttackReady(RE::Actor *a_actor)
    {
        bool ready = true;
        return !a_actor || !a_actor->GetGraphVariableBool("IsAttackReady", ready) || ready;
    }

    // Why a bash can't start now (nullptr: it can)
    static const char *BashRefusal(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        if (!AttackReady(a_actor)) {
            return "graph not attack-ready";
        }
        RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
        RE::NiPoint3 forward = a_state.self.forwardVector;
        toTarget.z = forward.z = 0.0f;
        if (toTarget.Unitize() > 1.0f && forward.Unitize() > 0.01f && forward.Dot(toTarget) < 0.259f) { // cos 75 deg
            return "target not in front";
        }
        if (a_state.target.distance > BashReach() * a_actor->GetScale() + a_state.target.boundRadius) {
            return "outside bash reach";
        }
        return nullptr;
    }

    static bool CanBashNow(RE::Actor *a_actor, const ActorStateData &a_state) { return BashRefusal(a_actor, a_state) == nullptr; }


    static float EffectiveAttackRange(const ActorStateData &a_state, float a_weaponReach, float a_multiplier,
                                      float a_safetyMargin = 0.0f)
    {
        return StateHelpers::EffectiveAttackRange(a_weaponReach, a_state.target.boundRadius, a_multiplier,
                                                  a_safetyMargin);
    }
    namespace
    {
        thread_local std::vector<DecisionResult> t_candidates;
    }

    const std::vector<DecisionResult> &DecisionMatrix::LastCandidates() { return t_candidates; }

    DecisionMatrix::DamageRace DecisionMatrix::Race(const ActorStateData &a_state)
    {
        const auto &dm = Config::GetInstance().GetDamageMomentum();
        DamageRace race;
        if (!dm.enableDamageRace || !a_state.target.isValid) {
            return race;
        }
        if (a_state.temporal.self.ourAverageHit > 0.0f) {
            race.toKill = a_state.target.healthPercent / a_state.temporal.self.ourAverageHit;
        }
        if (a_state.temporal.self.theirAverageHit > 0.0f) {
            race.toDie = a_state.self.healthPercent / a_state.temporal.self.theirAverageHit;
        }
        race.winning = race.toKill > 0.0f && race.toDie > 0.0f && race.toKill < race.toDie;
        race.losing = race.toDie > 0.0f && race.toDie <= dm.raceRetreatHits && !race.winning &&
                      a_state.self.healthPercent <= dm.raceRetreatMaxHealth;
        return race;
    }

    namespace
    {
        constexpr std::uint64_t SplitMix64(std::uint64_t a_state)
        {
            std::uint64_t z = a_state + 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }
        static_assert(SplitMix64(0) == 0xE220A8397B1DCDAFull); // reference first output for seed 0

        // A stateless once-per-key roll: every tick of the same swing gets the same answer
        float RollValue(std::uint64_t a_key)
        {
            static const std::uint64_t salt = std::random_device{}(); // a new sequence each session
            return static_cast<float>(SplitMix64(a_key + salt) >> 40) * (1.0f / 16777216.0f);
        }
        bool RollOnce(std::uint64_t a_key, float a_chance) { return RollValue(a_key) < a_chance; }
    }

    // Per-swing defend roll for parry and timed block (DefendOnSwingChance). Rolled once per swing (bit 30 keeps the
    // key apart from the trade-blows and close-dodge rolls); a won roll outranks back-off, strafe and trade-blow
    // attacks but not a won close dodge (10).
    constexpr float kDefendPriority = 6.0f;
    static bool DefendRoll(RE::Actor *a_actor, const ActorStateData &a_state, const char *a_what, float a_timeUntilHit)
    {
        const auto swing = ((static_cast<std::uint64_t>(a_actor->GetFormID()) << 32) |
                            a_state.temporal.target.swingCount) ^ (1ull << 30);
        const bool won = RollOnce(swing, (std::min)(1.0f, Config::GetInstance().GetDecisionMatrix().defendOnSwingChance *
                                                            Personality::CautionScale(Personality::Effective(a_actor).caution)));
        if (Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
            LOG_DEBUG("Defend 0x{:08X}: swing in the {} window (hit in {:.2f}s) -> {}", a_actor->GetFormID(), a_what,
                      a_timeUntilHit, won ? a_what : "no (roll)");
        }
        return won;
    }

    // Fights from range: bow, crossbow, staff or a spell in the right hand. Vanilla runs its RangedMovement tree for
    // them, and the melee rules (advance to reach, attack, flank, feint, bash) don't apply.
    bool DecisionMatrix::IsRangedFighter(const ActorStateData &a_state)
    {
        const auto w = a_state.self.weaponType;
        return a_state.self.isRanged || w == WeaponType::Staff || w == WeaponType::Spell;
    }

    bool DecisionMatrix::AllyInLineOfFire(const ActorStateData &a_state, RE::NiPoint3 *a_where)
    {
        // Every ally the combat scan saw (up to 8; the scan is cached for a moment, so positions can lag a little)
        const auto &cc = a_state.combatContext;
        if (!a_state.target.isValid || cc.allyPositionCount <= 0) {
            return false;
        }
        RE::NiPoint3 line = a_state.target.position - a_state.self.position;
        line.z = 0.0f;
        const float length = line.Unitize();
        if (length < 1.0f) {
            return false;
        }
        constexpr float kClearance = 55.0f; // ally body ~30 plus the arrow's or spell's width
        for (int i = 0; i < cc.allyPositionCount; ++i) {
            RE::NiPoint3 toAlly = cc.allyPositions[i] - a_state.self.position;
            toAlly.z = 0.0f;
            const float along = toAlly.Dot(line);
            if (along <= 30.0f || along >= length - 30.0f) {
                continue; // beside us or past the target
            }
            if ((toAlly - line * along).Length() < kClearance) {
                if (a_where) {
                    *a_where = cc.allyPositions[i];
                }
                return true;
            }
        }
        return false;
    }

    float DecisionMatrix::RangedShotFactor(const ActorStateData &a_state)
    {
        if (!a_state.target.isValid) {
            return 1.0f;
        }
        if (AllyInLineOfFire(a_state)) {
            return 0.0f; // don't shoot a friend in the back
        }
        const auto &t = a_state.target;
        // A raised guard facing the archer stops the arrow: wait for it to drop
        if (t.isBlocking && t.orientationDot > 0.5f) {
            return 0.3f;
        }
        // An opening: recovering, casting or drawing, staggered or fleeing. Not "looking away": in a group fight the
        // target faces someone else most of the time (run 47: x2 on 266 ticks, mostly that)
        if (t.isInAttackRecovery || t.isCasting || t.isDrawingBow || t.isFleeing ||
            t.knockState != RE::KNOCK_STATE_ENUM::kNormal) {
            return 2.0f;
        }
        return 1.0f;
    }

    // Is the target casting a healing spell (a beneficial value modifier on health) in either hand?
    static bool TargetHealing(const ActorStateData &a_state)
    {
        if (!a_state.target.isCasting) {
            return false;
        }
        auto *target = RE::TESForm::LookupByID<RE::Actor>(a_state.target.targetFormID);
        if (!target) {
            return false;
        }
        for (const auto source : {RE::MagicSystem::CastingSource::kLeftHand, RE::MagicSystem::CastingSource::kRightHand}) {
            auto *caster = target->GetMagicCaster(source);
            auto *spell = caster ? caster->currentSpell : nullptr;
            auto *effect = spell ? spell->GetCostliestEffectItem() : nullptr;
            const auto *base = effect ? effect->baseEffect : nullptr;
            if (!base || base->IsHostile() || base->data.primaryAV != RE::ActorValue::kHealth) {
                continue;
            }
            const auto archetype = base->GetArchetype();
            if (archetype == RE::EffectSetting::Archetype::kValueModifier || archetype == RE::EffectSetting::Archetype::kPeakValueModifier ||
                archetype == RE::EffectSetting::Archetype::kDualValueModifier) {
                return true;
            }
        }
        return false;
    }

    DecisionResult DecisionMatrix::EvaluateBoss(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        // Boss only: punish turtling, kiting and healing; a careful boss near death falls back among its minions.
        // Priorities sit above ordinary attacks (~1.5-3) and below a won defend roll (6) or close dodge (10).
        DecisionResult result;
        const auto phase = Boss::CurrentPhase(a_actor);
        if (phase == Boss::Phase::None || !a_state.target.isValid || (a_state.target.isPlayer && !a_state.target.isDetected)) {
            return result;
        }
        const bool debug = Config::GetInstance().IsDebugTarget(a_actor->GetFormID());
        const auto &dm = Config::GetInstance().GetDecisionMatrix();
        const float reach = EffectiveAttackRange(a_state, a_state.weaponReach > 0.0f ? a_state.weaponReach : 150.0f,
                                                 dm.offenseReachMultiplier, dm.offenseReachSafetyMargin);
        const float dist = a_state.target.distance;
        RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
        toTarget.z = 0.0f;
        toTarget.Unitize();
        const auto pick = [&](ActionType a_action, float a_priority, const char *a_why) {
            result.action = a_action;
            result.priority = a_priority;
            result.direction = a_action == ActionType::Retreat ? -toTarget : toTarget;
            result.intensity = 1.0f;
            if (debug) {
                LOG_DEBUG("Boss 0x{:08X}: {} -> {} (priority {:.1f})", a_actor->GetFormID(), a_why, ActionName(a_action), a_priority);
            }
            return result;
        };

        // Desperate and careful: fall back, when its minions are behind it (away from the target)
        const auto &cc = a_state.combatContext;
        if (phase == Boss::Phase::Desperate && !Boss::Brave(a_actor) && cc.hasNearbyAlly && cc.closestAllyDistance > 250.0f) {
            RE::NiPoint3 toAlly = cc.closestAllyPosition - a_state.self.position;
            toAlly.z = 0.0f;
            if (toAlly.Dot(toTarget) < 0.0f) {
                return pick(ActionType::Retreat, 2.0f, "desperate, falls back toward its minions");
            }
        }

        // Punish healing: the target is casting a heal
        if (TargetHealing(a_state)) {
            if (cc.isInAttackRange && a_state.self.isIdle) {
                return pick(ActionType::Attack, 2.8f, "target healing");
            }
            if (dist < 1200.0f) {
                pick(ActionType::Advancing, 2.2f, "target healing, rushes in");
                return result;
            }
        }

        // Guard break: the target has held its block for a second, in reach and facing us. A bash only inside bash
        // reach (GMST fCombatBashReach, as SCAR measures it): run 56's 119 guard-break bashes were taken by the Block
        // tree but only 2 swung, the rest failed vanilla's range check at weapon reach.
        if (a_state.target.isBlocking &&
            a_state.temporal.target.blockingDuration >= Config::GetInstance().GetBoss().guardBreakBlockSeconds && cc.isInAttackRange &&
            a_state.self.isIdle && a_state.target.orientationDot > 0.5f) {
            if (a_state.self.staminaPercent >= Boss::PowerStaminaMin(a_actor)) {
                return pick(ActionType::PowerAttack, 2.6f, "guard break (target turtling)");
            }
            if (BlockRules::CanRunBlockTree(a_actor) && a_state.self.currentStamina >= BashStaminaCost() &&
                CanBashNow(a_actor, a_state)) {
                return pick(ActionType::Bash, 2.4f, "guard break with a bash (low stamina)");
            }
        }

        // Anti-kite: the target keeps its distance shooting or casting; close the gap
        const bool kiting = a_state.target.isRanged || a_state.target.isCasting || a_state.target.isDrawingBow;
        if (kiting && dist > reach * 2.0f && dist < 1500.0f && !a_state.self.isSprinting) {
            pick(ActionType::Advancing, 1.9f, "target kiting, closes the gap");
            return result;
        }
        return result;
    }

    bool DecisionMatrix::RollSelfTest()
    {
        // Keys shaped like the forward-dodge roll (actor id high, a flag bit, consecutive time buckets). Runs 53-54
        // logged 0 "go" in ~10 openings at 0.35; this tells a skewed roll from bad luck.
        bool ok = true;
        std::string report;
        for (const float chance : {0.35f, 0.5f}) {
            int hits = 0;
            constexpr int kKeys = 20000;
            for (int i = 0; i < kKeys; ++i) {
                hits += RollOnce((0x0008E0EDull << 32) | (1ull << 61) | static_cast<std::uint64_t>(100000 + i), chance);
            }
            const float rate = static_cast<float>(hits) / kKeys;
            ok &= std::fabs(rate - chance) < 0.02f;
            report += std::format(" {:.2f} -> {:.3f};", chance, rate);
        }
        LOG_INFO("Roll self-test{} ({} over 20000 keys each)", ok ? " passed" : " FAILED", report);
        return ok;
    }

    DecisionResult DecisionMatrix::EvaluateRangedPositioning(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;
        const auto &dm = Config::GetInstance().GetDecisionMatrix();
        if (!a_state.target.isValid || (a_state.target.isPlayer && !a_state.target.isDetected)) {
            return result;
        }
        const float dist = a_state.target.distance;
        const bool debug = a_actor && Config::GetInstance().IsDebugTarget(a_actor->GetFormID());
        RE::NiPoint3 away = a_state.self.position - a_state.target.position;
        away.z = 0.0f;
        away.Unitize();

        // In contact: no more falling back, a walking archer can't outrun a running attacker (run 45: it backed off
        // 30-54 units while the player closed 133 -> 88 and kept hitting). Vanilla switches to its melee weapon.
        constexpr float kContactDistance = 150.0f;
        if (!a_state.target.isRanged && dist < kContactDistance) {
            if (debug) {
                LOG_DEBUG("Ranged 0x{:08X}: melee target in contact at {:.0f} -> left to vanilla", a_actor->GetFormID(), dist);
            }
            return result;
        }

        // A melee attacker inside the keep-away distance: fall back (kite). More urgent when it is swinging at us.
        // Not mid-draw or mid-cast: the Fallback node refuses to start then (run 46: an archer's 9 fallbacks all
        // failed at once, 7 of them in bow states 8-12). It lets the shot go and falls back after.
        if (!a_state.target.isRanged && dist < dm.rangedKeepAwayDistance &&
            a_state.self.attackState != RE::ATTACK_STATE_ENUM::kNone) {
            if (debug) {
                LOG_DEBUG("Ranged 0x{:08X}: melee target at {:.0f}, finishing its shot first", a_actor->GetFormID(), dist);
            }
            return result;
        }
        if (!a_state.target.isRanged && dist < dm.rangedKeepAwayDistance) {
            const bool swinging = (a_state.target.isAttacking || a_state.target.isPowerAttacking) &&
                                  TargetAimedAtUs(a_actor, a_state);
            result.action = ActionType::Retreat;
            result.priority = swinging ? 2.2f : 1.9f;
            result.direction = away;
            result.intensity = 1.0f;
            if (debug) {
                LOG_DEBUG("Ranged 0x{:08X}: melee target at {:.0f} (keep away {:.0f}) -> fall back", a_actor->GetFormID(),
                          dist, dm.rangedKeepAwayDistance);
            }
            return result;
        }

        // Too far, or no line of sight: close in to a little inside the max distance. An ally in the line of fire
        // can be what blocks the sight, so inside the max distance stepping aside comes first (run 48: a sorcerer
        // closed in "out of sight" behind an ally and held fire 15 s).
        const bool noSight = a_state.env.losChecked && !a_state.env.losToTarget;
        RE::NiPoint3 blocker;
        const bool allyInLine = dist <= dm.rangedMaxDistance && AllyInLineOfFire(a_state, &blocker);
        if (dist > dm.rangedMaxDistance || (noSight && !allyInLine)) {
            result.action = ActionType::Advancing;
            result.priority = 1.0f;
            result.holdDistance = noSight ? (std::max)(dm.rangedKeepAwayDistance, dist * 0.6f) : dm.rangedMaxDistance * 0.8f;
            result.direction = -away;
            result.intensity = 0.8f;
            if (debug) {
                LOG_DEBUG("Ranged 0x{:08X}: target at {:.0f}{} -> close in to {:.0f}", a_actor->GetFormID(), dist,
                          noSight ? " out of sight" : "", result.holdDistance);
            }
        }
        // An ally between us and the target: step sideways to clear the line of fire
        if (result.action == ActionType::None && allyInLine) {
            RE::NiPoint3 toAlly = blocker - a_state.self.position;
            toAlly.z = 0.0f;
            RE::NiPoint3 side(-away.y, away.x, 0.0f);
            if (side.Dot(toAlly) > 0.0f) {
                side = -side; // step to the side away from the ally
            }
            // That side's Circle just failed (no path, run 48): the other side (sign as ActionExecutor::CircleSide)
            const float cross = -away.x * side.y + away.y * side.x;
            const bool flipped = Intents::CircleRefused(a_actor, cross > 0.0f ? 1.0f : -1.0f);
            if (flipped) {
                side = -side;
            }
            result.action = ActionType::Strafe;
            result.priority = 1.5f;
            result.direction = side;
            result.intensity = 0.7f;
            if (debug) {
                LOG_DEBUG("Ranged 0x{:08X}: ally in the line of fire -> step aside{}", a_actor->GetFormID(),
                          flipped ? " (other side: the last one failed)" : "");
            }
        }
        // Otherwise in its band: vanilla shoots, casts, strafes and keeps its own optimal range
        return result;
    }

    DecisionResult DecisionMatrix::Evaluate(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        std::vector<DecisionResult> possibleDecisions;
        const bool ranged =
            IsRangedFighter(a_state) && Config::GetInstance().GetDecisionMatrix().enableRangedPositioning;

        // Evaluate all potential actions
        DecisionResult survivalResult = EvaluateSurvival(a_actor, a_state);
        // An enraged boss, or a brave desperate one going all-in, doesn't run from the damage race (run 56: the
        // "all-in" chief still retreated 35 times below 25% health, 13 times enraged)
        if (survivalResult.action == ActionType::Retreat) {
            const auto phase = Boss::CurrentPhase(a_actor);
            if (phase == Boss::Phase::Enraged || (phase == Boss::Phase::Desperate && Boss::Brave(a_actor))) {
                survivalResult = DecisionResult();
            }
        }
        if (survivalResult.action != ActionType::None) {
            possibleDecisions.push_back(survivalResult);
        }

        DecisionResult interruptResult = ranged ? DecisionResult() : EvaluateInterrupt(a_actor, a_state);
        if (interruptResult.action != ActionType::None) {
            possibleDecisions.push_back(interruptResult);
        }

        // Humanoid creatures (draugr, falmer, skeletons) have no dodge or jump animations
        const bool canDodge = !ActorUtils::IsHumanoidCreature(a_actor);
        const auto dodges = [](const DecisionResult &a_result) {
            return a_result.action == ActionType::Dodge || a_result.action == ActionType::Jump ||
                   a_result.action == ActionType::ForwardDodge;
        };

        DecisionResult projectileResult = EvaluateProjectileDodge(a_actor, a_state);
        if (projectileResult.action != ActionType::None && (canDodge || !dodges(projectileResult))) {
            possibleDecisions.push_back(projectileResult);
        }

        DecisionResult evasionResult = EvaluateEvasion(a_actor, a_state);
        if (evasionResult.action != ActionType::None && (canDodge || !dodges(evasionResult))) {
            possibleDecisions.push_back(evasionResult);
        }

        DecisionResult backoffResult = EvaluateBackoff(a_actor, a_state);
        if (backoffResult.action != ActionType::None) {
            possibleDecisions.push_back(backoffResult);
        }

        DecisionResult flankingResult = ranged ? EvaluateRangedPositioning(a_actor, a_state) : EvaluateFlanking(a_actor, a_state);
        if (flankingResult.action != ActionType::None) {
            possibleDecisions.push_back(flankingResult);
        }

        DecisionResult feintingResult = ranged ? DecisionResult() : EvaluateFeinting(a_actor, a_state);
        if (feintingResult.action != ActionType::None) {
            possibleDecisions.push_back(feintingResult);
        }

        DecisionResult bossResult = ranged ? DecisionResult() : EvaluateBoss(a_actor, a_state);
        if (bossResult.action != ActionType::None) {
            possibleDecisions.push_back(bossResult);
        }

        DecisionResult offenseResult = ranged ? DecisionResult() : EvaluateOffense(a_actor, a_state);
        // SCAR-patched movesets pick their own normal/power attacks (sprint attack stays ours)
        if ((offenseResult.action == ActionType::Attack || offenseResult.action == ActionType::PowerAttack) &&
            SCARIntegration::GetInstance().OwnsAttacks(a_actor->GetFormID())) {
            offenseResult = DecisionResult();
        }
        if (offenseResult.action != ActionType::None) {
            possibleDecisions.push_back(offenseResult);
        }

        // Moves the tree can't start right now. The Circle, Backoff and Fallback starts all refuse while the NPC's
        // movement is animation-driven (mid-swing, stagger, dodge recovery): 10 of 15 Circle and 2 of 5 Backoff
        // refusals (Somnium run, 2026-10-27). Those wait until the animation lets go; the brain asks again next
        // decision. Our Circle failed for another reason under 2 s ago: asking again left the NPC standing still,
        // 14 ticks at 384 units (AE run, 2026-10-26), so melee picks something else.
        const bool animDriven = a_actor->IsMovementAnimationDriven();
        const bool circleRefused = !ranged && Intents::CircleRefusedRecently(a_actor);
        if (animDriven || circleRefused) {
            const auto before = possibleDecisions.size();
            std::erase_if(possibleDecisions, [&](const DecisionResult &a_result) {
                const bool circles = a_result.action == ActionType::Strafe || a_result.action == ActionType::Flanking;
                const bool spacing = circles || a_result.action == ActionType::Backoff || a_result.action == ActionType::Retreat;
                return (animDriven && spacing) || (circleRefused && circles);
            });
            if (possibleDecisions.size() != before && Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
                LOG_DEBUG("Hold 0x{:08X}: {} spacing move(s) held ({})", a_actor->GetFormID(), before - possibleDecisions.size(),
                          animDriven ? "movement animation-driven" : "Circle refused under 2 s ago");
            }
        }

        // If no decisions, return None
        t_candidates.clear();
        if (possibleDecisions.empty()) {
            return DecisionResult();
        }

        // for debugging
        std::string actions;
        auto &config = Config::GetInstance();
        auto selectedRef = RE::Console::GetSelectedRef();

        bool debugEnabled = config.GetGeneral().enableDebugLog;

        // Enhance all decisions and find the one with the highest priority
        std::vector<DecisionResult> enhancedDecisions;
        float highestPriority = -1.0f;
        for (size_t i = 0; i < possibleDecisions.size(); ++i) {
            DecisionResult enhancedDecision = m_styleEnhancer.EnhanceDecision(a_actor, possibleDecisions[i], a_state);
            enhancedDecisions.push_back(enhancedDecision);

            if (enhancedDecision.priority >= highestPriority) {
                highestPriority = enhancedDecision.priority;
            }

            if (debugEnabled && selectedRef && a_actor == selectedRef.get()) {
                actions +=
                    std::format("({},{:.2f}),", static_cast<int>(enhancedDecision.action), enhancedDecision.priority);
            }
        }

        // Second pass: collect all decisions with highest priority (using epsilon for
        // float comparison)
        const float epsilon = 0.01f;
        std::vector<DecisionResult> topPriorityDecisions;
        for (const auto &decision : enhancedDecisions) {
            if (std::abs(decision.priority - highestPriority) < epsilon) {
                topPriorityDecisions.push_back(decision);
            }
        }

        // Ties are a coin flip: the skill roll, not a tie heuristic, decides how good the pick is
        // (empty when every priority is below -1 or NaN; the old tie-break returned None then, so do the same)
        thread_local std::mt19937 tieGen{std::random_device{}()};
        DecisionResult bestDecision =
            topPriorityDecisions.empty()
                ? DecisionResult()
                : topPriorityDecisions[std::uniform_int_distribution<std::size_t>(0, topPriorityDecisions.size() - 1)(tieGen)];
        t_candidates = std::move(enhancedDecisions);

        // Log comprehensive state for selected actor (debugging)
        if (debugEnabled && selectedRef && a_actor == selectedRef.get()) {
            LogActorState(a_actor, a_state);

            std::ostringstream logs;
            if (auto formID = ActorUtils::SafeGetFormID(a_actor)) {
                logs << "actor: " << std::hex << *formID << std::dec
                     << " | action: " << static_cast<int>(bestDecision.action)
                     << " | possible actions (a,p): " << actions;
                ConsolePrint(logs.str().c_str());
                LOG_DEBUG("{}", logs.str());
            }
        }

        return bestDecision;
    }

    DecisionResult DecisionMatrix::EvaluateHeldBack([[maybe_unused]] RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;
        if (!a_state.target.isValid) {
            return result; // nothing to circle
        }
        // Circle/flank the target while waiting for an attack slot. ExecuteFlanking uses
        // the Circle node on the brain's side.
        result.action = ActionType::Flanking;
        result.direction = CalculateFlankingDirection(a_state);
        result.priority = 1.0f;
        result.intensity = 0.6f;
        return result;
    }

    DecisionResult DecisionMatrix::EvaluateInterrupt(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        // Check if target is valid
        if (!a_state.target.isValid) {
            return result;
        }

        // Can't bash with a bow or crossbow - use weapon type from state
        if (a_state.self.isRanged) {
            return result; // Return None - ranged weapons can't bash
        }
        if (Intents::WantsDualBlock(a_actor)) {
            return result; // holding a direct block already
        }
        // Without the Block tree there is no bash or parry, only our direct timed block. Run 38: a sword+spell NPC
        // posted 8 bashes that could never be taken, and the refusal guard below then shut its timed block off too.
        if (!BlockRules::CanRunBlockTree(a_actor)) {
            return a_state.temporal.self.timeSinceLastAction < 0.3f ? result : EvaluateTimedBlock(a_actor, a_state);
        }
        if (Intents::DefendUnavailable(a_actor)) {
            return result;
        }

        // Same anti-spam gate as attacks: without it the bash was re-posted every tick mid-bash
        if (a_state.temporal.self.timeSinceLastAction < 0.3f) {
            return result;
        }

        // Shortcut: base cost only (vanilla may scale it), upgrade if bashes are still refused with stamina left.
        // Run 28: 10 of 11 refused bashes came at 0-8% stamina.
        if (a_state.self.currentStamina < BashStaminaCost()) {
            return EvaluateTimedBlock(a_actor, a_state); // a block costs no stamina: a timed block can still go
        }

        // Get interrupt distance - use actual weapon reach from state
        // Fallback to config value if weapon reach not available
        auto &config = Config::GetInstance();
        float reachDistance = a_state.weaponReach;
        if (reachDistance <= 0.0f) {
            reachDistance = config.GetDecisionMatrix().interruptMaxDistance;
        } else {
            // Apply reach multiplier from config
            reachDistance *= config.GetDecisionMatrix().interruptReachMultiplier;
        }
        // Pad with target body radius (distance is center-to-center)
        reachDistance += a_state.target.boundRadius;

        // Optimal bash range: closer is better, but not too close
        float optimalBashDistance = reachDistance * 0.7f; // Optimal is 70% of max reach
        float minBashDistance = reachDistance * 0.3f;     // Too close might be risky

        // Bash opportunities: interrupt dangerous actions or break guard
        bool shouldBash = false;
        float basePriority = 0.0f;
        float situationBonus = 0.0f;

        // 1. Interrupt power attacks (highest priority - very dangerous)
        if (a_state.target.isPowerAttacking && a_state.target.distance < reachDistance) {
            shouldBash = true;
            basePriority = config.GetScoringWeights().interruptPowerAttackBase; // Interrupt power attacks

            // Higher priority if target is facing us (more dangerous)
            if (a_state.target.orientationDot > 0.7f) {
                situationBonus += 0.3f; // Target facing us - very dangerous
            }

            // Higher priority if we're in optimal range
            if (a_state.target.distance >= minBashDistance && a_state.target.distance <= optimalBashDistance) {
                situationBonus += 0.2f; // Optimal bash range
            }
        }

        // 2. Interrupt casting/drawing (high priority - vulnerable target)
        if (!shouldBash && (a_state.target.isCasting || a_state.target.isDrawingBow) &&
            a_state.target.distance < reachDistance) {
            shouldBash = true;
            basePriority = 1.3f; // High priority for interrupting spells/bows

            // Use temporal state: Higher priority if target has been casting/drawing
            // for a while (more vulnerable)
            float castingDuration = a_state.temporal.target.castingDuration;
            float drawingDuration = a_state.temporal.target.drawingDuration;
            float vulnerableDuration = (castingDuration > drawingDuration) ? castingDuration : drawingDuration;

            if (vulnerableDuration > 1.0f) {
                // Target has been casting/drawing for > 1 second - very vulnerable
                situationBonus += 0.3f; // Significant bonus for extended vulnerability
            } else if (vulnerableDuration > 0.5f) {
                // Target has been casting/drawing for > 0.5 seconds - vulnerable
                situationBonus += 0.15f; // Moderate bonus
            }

            // Higher priority if target is facing us (interrupt before they finish)
            if (a_state.target.orientationDot > 0.6f) {
                situationBonus += 0.2f; // Target facing us - interrupt before cast completes
            }

            // Higher priority if we're in optimal range
            if (a_state.target.distance >= minBashDistance && a_state.target.distance <= optimalBashDistance) {
                situationBonus += 0.15f; // Optimal bash range
            }
        }

        // 3. Break guard when target is blocking (tactical bash)
        if (!shouldBash && a_state.target.isBlocking && !a_state.target.isAttacking &&
            a_state.target.distance < reachDistance) {
            shouldBash = true;
            basePriority = 1.1f; // Moderate priority for breaking guard

            // Use temporal state: Higher priority if target has been blocking for a
            // while (committed to defense)
            float targetBlockingDuration = a_state.temporal.target.blockingDuration;
            if (targetBlockingDuration > 1.0f) {
                // Target has been blocking for > 1 second - very committed to defense,
                // excellent bash opportunity
                situationBonus += 0.4f; // Significant bonus for committed blocking
            } else if (targetBlockingDuration > 0.5f) {
                // Target has been blocking for > 0.5 seconds - committed to defense
                situationBonus += 0.2f; // Moderate bonus
            } else {
                // Target just started blocking - might be reactive, less committed
                basePriority -= 0.2f; // Reduce priority for reactive blocking
            }

            // Weapon type considerations: Two-handed weapons break blocks better, so
            // bash less needed One-handed weapons benefit more from bash to break guard
            if (a_state.self.isTwoHanded) {
                basePriority -= 0.2f; // Two-handed can break blocks easier, bash less critical
            } else if (a_state.self.isOneHanded) {
                situationBonus += 0.15f; // One-handed weapons benefit more from bash
            }

            // Target weapon type: Bash is more effective against one-handed blockers
            if (a_state.target.isOneHanded) {
                situationBonus += 0.1f; // Easier to bash one-handed blockers
            } else if (a_state.target.isTwoHanded) {
                basePriority -= 0.1f; // Two-handed blockers are harder to bash
            }

            // Higher priority if target is facing us defensively
            if (a_state.target.orientationDot > 0.7f) {
                situationBonus += 0.2f; // Target facing us defensively - bash to break guard
            }

            // Higher priority if we're in optimal range
            if (a_state.target.distance >= minBashDistance && a_state.target.distance <= optimalBashDistance) {
                situationBonus += 0.15f; // Optimal bash range
            }
        }

        // 4. Parry opportunity (timed bash for parrying - highest priority when
        // timing is right) Check parry first as it's a specialized action
        auto &parryConfig = Config::GetInstance().GetParry();
        if (parryConfig.enableParry && (a_state.target.isAttacking || a_state.target.isPowerAttacking)) {
            DecisionResult parryResult = EvaluateParry(a_actor, a_state);
            if (parryResult.action == ActionType::Parry) {
                // Parry opportunity found - return immediately (parry takes precedence)
                return parryResult;
            }
        }

        // 4b. Timed Block opportunity (timed block for blocking - check alongside
        // parry) Timed block is evaluated after parry, but can be chosen if parry
        // isn't available
        auto &timedBlockConfig = Config::GetInstance().GetTimedBlock();
        if (timedBlockConfig.enableTimedBlock && (a_state.target.isAttacking || a_state.target.isPowerAttacking)) {
            DecisionResult timedBlockResult = EvaluateTimedBlock(a_actor, a_state);
            if (timedBlockResult.action == ActionType::TimedBlock) {
                // Timed block opportunity found - return immediately (timed block takes
                // precedence over regular bash)
                return timedBlockResult;
            }
        }

        // 5. Interrupt normal attacks (moderate priority - risky but can work)
        if (!shouldBash && a_state.target.isAttacking && !a_state.target.isPowerAttacking &&
            a_state.target.distance < reachDistance * 0.8f) { // Closer range for interrupting attacks
            shouldBash = true;
            basePriority = 1.0f; // Moderate priority for interrupting attacks

            // Higher priority if target is facing us (more dangerous)
            if (a_state.target.orientationDot > 0.8f) {
                situationBonus += 0.2f; // Target facing us - interrupt attack
            }

            // Higher priority if we're in optimal range
            if (a_state.target.distance >= minBashDistance && a_state.target.distance <= optimalBashDistance) {
                situationBonus += 0.15f; // Optimal bash range
            }
        }

        // Additional considerations
        // The bash itself must be able to start (run 60: with this check up front, the debug NPCs had a bash candidate
        // on 7 ticks and the reason was invisible)
        if (shouldBash) {
            if (const char *why = BashRefusal(a_actor, a_state)) {
                if (Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
                    LOG_DEBUG("Bash 0x{:08X}: opening, no bash ({}, dist {:.0f})", a_actor->GetFormID(), why, a_state.target.distance);
                }
                return EvaluateTimedBlock(a_actor, a_state);
            }
        }
        if (shouldBash) {
            // Don't bash if we're already attacking (bash is an interrupt, not a combo)
            if (a_state.self.attackState != RE::ATTACK_STATE_ENUM::kNone &&
                a_state.self.attackState != RE::ATTACK_STATE_ENUM::kDraw) {
                return result; // Already attacking - don't bash
            }

            // Use enhanced combat context: Threat level
            // Higher threat level = less likely to bash (risky when outnumbered)
            ThreatLevel bashThreatLevel = a_state.combatContext.threatLevel;
            if (bashThreatLevel >= ThreatLevel::High) {
                basePriority -= 0.4f; // Reduce priority significantly when high threat
            } else if (bashThreatLevel == ThreatLevel::Moderate) {
                basePriority -= 0.2f; // Reduce priority moderately when moderate threat
            } else if (a_state.combatContext.allyCount > a_state.combatContext.enemyCount) {
                // More allies: can be more aggressive with bash
                situationBonus += 0.15f; // Safer to bash when we have allies
            }

            // More enemies targeting us = less likely to bash
            if (a_state.combatContext.enemiesTargetingUs > 1) {
                basePriority -= 0.2f; // Multiple enemies targeting us - bash is risky
            }

            // Stamina consideration: bash costs stamina, need some left
            if (a_state.self.staminaPercent < 0.2f) {
                basePriority -= 0.2f; // Reduce priority when very low stamina
            } else if (a_state.self.staminaPercent > 0.6f) {
                situationBonus += 0.1f; // Bonus when we have good stamina
            }

            // Distance modifier: penalize if too close or too far
            float distanceModifier = 0.0f;
            if (a_state.target.distance < minBashDistance) {
                // Too close - risky
                distanceModifier = -0.2f;
            } else if (a_state.target.distance > optimalBashDistance) {
                // Beyond optimal - less effective
                float beyondRatio =
                    (a_state.target.distance - optimalBashDistance) / (reachDistance - optimalBashDistance);
                distanceModifier = -0.3f * beyondRatio; // Up to -0.3f when at max range
            }

            // Final priority calculation
            float finalPriority = basePriority + situationBonus + distanceModifier;

            // Only bash if priority is reasonable
            if (finalPriority >= 0.7f) {
                result.action = ActionType::Bash;
                result.priority = finalPriority;

                // Bash intensity: higher for urgent interrupts (power attacks, casting)
                if (a_state.target.isPowerAttacking || a_state.target.isCasting || a_state.target.isDrawingBow) {
                    result.intensity = 1.0f; // Maximum intensity for urgent interrupts
                } else {
                    result.intensity = 0.8f; // High intensity for tactical bashes
                }
            }
        }

        return result;
    }

    DecisionResult DecisionMatrix::EvaluateParry(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        auto &config = Config::GetInstance();
        const auto &parryConfig = config.GetParry();

        // Check if parry is enabled
        if (!parryConfig.enableParry) {
            return result;
        }

        // Check if target is valid and attacking, at this NPC (not a neighbour in a group fight)
        if (!a_state.target.isValid || (!a_state.target.isAttacking && !a_state.target.isPowerAttacking) ||
            !TargetAimedAtUs(a_actor, a_state)) {
            return result;
        }

        // A parry is a bash from the Block tree
        if (!BlockRules::CanRunBlockTree(a_actor) || !CanBashNow(a_actor, a_state)) {
            return result;
        }

        // Check if self is idle/ready (can't parry while attacking)
        if (a_state.self.attackState != RE::ATTACK_STATE_ENUM::kNone &&
            a_state.self.attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            return result; // Already attacking - can't parry
        }

        // Check distance
        if (a_state.target.distance < parryConfig.parryMinDistance ||
            a_state.target.distance > parryConfig.parryMaxDistance) {
            return result; // Out of parry range
        }

        // Check timing - need valid attack timing data
        float timeUntilHit = a_state.temporal.target.timeUntilAttackHits;
        if (timeUntilHit > parryConfig.parryWindowEnd || timeUntilHit < parryConfig.parryWindowStart) {
            return result; // Outside parry window
        }
        if (!DefendRoll(a_actor, a_state, "parry", timeUntilHit)) {
            return result;
        }

        // The won per-swing defend roll decides (a scored priority could never outrank it)
        result.action = ActionType::Parry;
        result.priority = kDefendPriority;
        result.intensity = 1.0f; // Maximum intensity for parry

        return result;
    }

    DecisionResult DecisionMatrix::EvaluateTimedBlock(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        auto &config = Config::GetInstance();
        const auto &timedBlockConfig = config.GetTimedBlock();

        // Check if timed block is enabled
        if (!timedBlockConfig.enableTimedBlock) {
            return result;
        }

        // No mod check: the trigger is a vanilla block at the right moment. Simple Timed Block, when loaded, only
        // adds its window effect (ActionExecutor::ExecuteTimedBlock).

        // Check if target is valid and attacking, at this NPC (not a neighbour in a group fight)
        if (!a_state.target.isValid || (!a_state.target.isAttacking && !a_state.target.isPowerAttacking) ||
            !TargetAimedAtUs(a_actor, a_state)) {
            return result;
        }

        // Check if self is idle/ready (can't timed block while attacking)
        if (a_state.self.attackState != RE::ATTACK_STATE_ENUM::kNone &&
            a_state.self.attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            return result; // Already attacking - can't timed block
        }

        // Run 33 posted 3 untaken blocks for a dagger bandit: only some equipment runs the Block tree
        const bool blockTree = BlockRules::CanRunBlockTree(a_actor);

        // Check distance
        if (a_state.target.distance < timedBlockConfig.timedBlockMinDistance ||
            a_state.target.distance > timedBlockConfig.timedBlockMaxDistance) {
            return result; // Out of timed block range
        }

        // Check timing - need valid attack timing data
        float timeUntilHit = a_state.temporal.target.timeUntilAttackHits;
        if (timeUntilHit > timedBlockConfig.timedBlockWindowEnd ||
            timeUntilHit < timedBlockConfig.timedBlockWindowStart) {
            return result; // Outside timed block window
        }
        // No Block tree: a weapon with a spell, torch or second weapon blocks through our DualBlock node instead
        if (!blockTree && !BlockRules::CanDualBlock(a_actor)) {
            return result;
        }
        if (!DefendRoll(a_actor, a_state, "timed block", timeUntilHit)) {
            return result;
        }

        // The won per-swing defend roll decides (a scored priority could never outrank it)
        result.action = ActionType::TimedBlock;
        result.priority = kDefendPriority;
        result.intensity = 1.0f; // Maximum intensity for timed block

        return result;
    }

    DecisionResult DecisionMatrix::EvaluateProjectileDodge(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;
        const auto &settings = Config::GetInstance().GetProjectileAwareness();
        const auto &incoming = a_state.incomingProjectile;
        if (!settings.enableProjectileDodge || !incoming.valid ||
            !Config::GetInstance().GetModIntegrations().enableTKDodgeIntegration ||
            incoming.timeToImpact < settings.reactMinSeconds || incoming.timeToImpact > settings.reactMaxSeconds) {
            return result;
        }
        // An arrow from a player it hasn't found (searching after the hit, sneak archery) can't be seen coming. Its
        // combat target is usually the player, whose detection is already read; otherwise ask (rare: one arrow).
        if (incoming.shooterID == 0x14) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            const bool seen = a_state.target.isPlayer ? a_state.target.isDetected
                                                      : ActorUtils::SafeRequestDetectionLevel(a_actor, player) > 0;
            if (!seen) {
                if (Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
                    LOG_DEBUG("Projectile 0x{:08X}: threat 0x{:08X} from the player, who is hidden from it -> no reaction",
                              static_cast<std::uint32_t>(a_actor->GetFormID()),
                              static_cast<std::uint32_t>(incoming.threatID));
                }
                return result;
            }
        }

        const RE::FormID formID = a_actor->GetFormID();
        const bool react = ProjectileAwareness::GetInstance().RollReaction(formID, incoming.threatID,
                                                                           settings.dodgeChance);
        if (Config::GetInstance().IsDebugTarget(formID)) {
            LOG_DEBUG("Projectile 0x{:08X}: threat 0x{:08X} from 0x{:08X} impact in {:.2f}s speed {:.0f} -> {}",
                      static_cast<std::uint32_t>(formID), static_cast<std::uint32_t>(incoming.threatID),
                      static_cast<std::uint32_t>(incoming.shooterID), incoming.timeToImpact, incoming.speed,
                      react ? "sidestep" : "no reaction (chance)");
        }
        if (!react) {
            return result;
        }

        // Sidestep perpendicular to the flight path, to the side the actor already leans
        // toward (away from the line), so the shortest move clears it
        RE::NiPoint3 side(-incoming.flightDir.y, incoming.flightDir.x, 0.0f);
        if (side.Dot(incoming.missOffset) < 0.0f) {
            side = -side;
        }
        result.action = ActionType::Dodge;
        result.direction = side;
        result.priority = settings.dodgePriority;
        result.reflex = true;
        return result;
    }

    DecisionResult DecisionMatrix::EvaluateEvasion(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        auto &config = Config::GetInstance();

        // Check if target is valid. A hidden player's swing or aimed bow isn't seen either (searching NPC).
        if (!a_state.target.isValid || (a_state.target.isPlayer && !a_state.target.isDetected)) {
            return result;
        }

        // Allow evasion even when not idle if:
        // 1. Low health and target is attacking (survival priority)
        // 2. Target is power attacking (urgent evasion needed)
        if (!a_state.self.isIdle) {
            bool shouldAllowEvasion = (a_state.self.healthPercent < 0.5f &&
                                       (a_state.target.isAttacking || a_state.target.isPowerAttacking)) ||
                                      a_state.target.isPowerAttacking;
            if (!shouldAllowEvasion) {
                return result; // Not idle and conditions not met - skip evasion
            }
        }

        // --- Tactical Spacing: Maintain distance when observing ---
        // If actor is idle and too close to target (not attacking), create space
        // But only if we're VERY close (within weapon reach) - don't overuse this
        float weaponReach = a_state.weaponReach;
        if (weaponReach <= 0.0f) {
            weaponReach = 150.0f; // Fallback reach
        }

        // Tactical spacing distance: only when dangerously close (within weapon
        // reach) Reduced from 1.3f to 1.0f to be less aggressive about spacing
        float tacticalSpacingDistance = weaponReach * 1.0f;

        // Check if we're too close and just observing (not attacking, target not
        // attacking)
        bool isTooClose = a_state.target.distance < tacticalSpacingDistance;
        bool isObserving = !a_state.target.isAttacking && !a_state.target.isPowerAttacking &&
                           !a_state.target.isCasting && !a_state.target.isDrawingBow;
        bool justFinishedAttack = (a_state.self.attackState == RE::ATTACK_STATE_ENUM::kFollowThrough);

        // Only maintain spacing when there's a defensive reason to: the target is
        // guarding (reposition to flank) or we're low on stamina and want to reset.
        // Against a fully idle, open target, attacking is better than backing off -
        // otherwise NPCs strafe point-blank targets forever instead of committing.
        bool spacingReason = a_state.target.isBlocking ||
                             (config.GetDecisionMatrix().staminaAffectsRetreat && a_state.self.staminaPercent < 0.3f);

        // Only prioritize spacing if VERY close (within weapon reach) and observing
        // This prevents constant strafing when at safe distances
        if (isTooClose && isObserving && spacingReason && !justFinishedAttack) {
            // Prefer strafe for tactical repositioning (better than backoff - maintains
            // engagement)
            result.action = ActionType::Strafe;
            result.priority = config.GetScoringWeights().evasionDodgeBase; // Evasion spacing base

            // Boost priority if dangerously close (within weapon reach)
            if (a_state.target.distance < weaponReach * 0.8f) {
                result.priority = 1.5f; // Higher priority when dangerously close
            }

            // Boost priority if target is blocking (good time to reposition)
            if (a_state.target.isBlocking) {
                result.priority += 0.1f; // Reduced from 0.2f
            }

            result.direction = CalculateStrafeDirection(a_state);
            result.intensity = 0.6f; // Moderate strafe speed for tactical spacing

            return result; // Return early - tactical spacing takes priority
        }

        // --- Jump Evasion Logic ---
        if (config.GetDecisionMatrix().enableJumpEvasion) {
            // Check if target is using a ranged weapon (bow or crossbow)
            // Use weapon type from state instead of checking equipped weapon directly
            // Only while it is drawing (a bow draw reads as attacking) and aimed at this NPC, not its neighbour;
            // only with the stamina the jump costs.
            const bool drawing = a_state.target.isAttacking || a_state.target.isDrawingBow;
            if (a_state.target.isRanged && drawing &&
                a_state.self.currentStamina >= config.GetDecisionMatrix().jumpStaminaCost) {
                // Check distance
                const bool inWindow = a_state.target.distance > config.GetDecisionMatrix().jumpEvasionDistanceMin &&
                                      a_state.target.distance < config.GetDecisionMatrix().jumpEvasionDistanceMax;
                const float aimLimit = a_state.self.boundRadius + kAimSlack;
                // The player's body heading misses the crosshair by the third-person camera offset (run 11: 113-191
                // units off while aimed straight at us), so for the player ask the game which actor the bow
                // crosshair is on. The strict body-line check stays for NPC shooters and as a fallback.
                bool crosshairOnUs = false;
                if (a_state.target.isPlayer) {
                    if (auto *player = RE::PlayerCharacter::GetSingleton()) {
                        crosshairOnUs = player->GetInfoRuntimeData().autoAimActor.get().get() == a_actor;
                    }
                }
                const bool aimed =
                    crosshairOnUs || IsAimedAt(a_state.target.distance, a_state.target.orientationDot, aimLimit);
                if (inWindow && a_actor && config.IsDebugTarget(a_actor->GetFormID())) {
                    const float dot = (std::clamp)(a_state.target.orientationDot, -1.0f, 1.0f);
                    LOG_DEBUG("Jump aim 0x{:08X}: shooter drawing at {:.0f}, aim line {:.0f} units from us (limit {:.0f}), "
                              "crosshair on us {}{}",
                              a_actor->GetFormID(), a_state.target.distance,
                              dot > 0.0f ? a_state.target.distance * std::sqrt(1.0f - dot * dot) : -1.0f, aimLimit,
                              crosshairOnUs, aimed ? " - may jump" : "");
                }
                if (inWindow && aimed) {

                    // Random chance to jump
                    thread_local std::mt19937 gen{std::random_device{}()};
                    std::uniform_real_distribution<> dis(0.0, 1.0);
                    if (dis(gen) < config.GetDecisionMatrix().evasionJumpChance) {
                        result.action = ActionType::Jump;
                        result.priority = 1.5f; // Evasion priority
                        // Jump evasion - high intensity for urgent escape
                        result.intensity = 0.9f; // High intensity for jump evasion
                        return result;
                    }
                }
            }
        }

        // Weapon type considerations for evasion: Two-handed weapons are slower to
        // dodge One-handed weapons can dodge/react faster
        float weaponEvasionModifier = 0.0f;
        if (a_state.self.isTwoHanded) {
            weaponEvasionModifier -= 0.1f; // Two-handed weapons are slower to dodge
        } else if (a_state.self.isOneHanded) {
            weaponEvasionModifier += 0.1f; // One-handed weapons can dodge faster
        }

        // Use temporal state: Prevent dodge spam - don't dodge too frequently
        float timeSinceLastDodge = a_state.temporal.self.timeSinceLastDodge;
        if (timeSinceLastDodge < 1.0f) {
            // Just dodged < 1s ago - prevent spam, prefer strafe instead
            // But allow if target is power attacking (urgent)
            if (!a_state.target.isPowerAttacking) {
                // Not urgent - prefer strafe instead of dodge spam
                // Continue to strafe evaluation below
            }
        }

        // Trigger: the target's attack is aimed at this NPC and can reach it here (the band starts beyond melee
        // reach, so a melee swing only as a power or sprint attack). Run 28 dodged at 254-340 units from the
        // player's swings at someone else, and the old "target is blocking" trigger dodged a guard.
        bool shouldDodge = false;
        bool closeDodge = false;
        const bool reachesBand =
            a_state.target.isRanged || a_state.target.isPowerAttacking || a_state.target.isSprinting;
        if ((a_state.target.isAttacking || a_state.target.isPowerAttacking) && TargetAimedAtUs(a_actor, a_state)) {
            bool conditionsMet = false;
            const float minEvasionDist = config.GetDecisionMatrix().evasionMinDistance;

            // Band: a shot, power attack or sprint attack that reaches this far
            if (reachesBand && a_state.target.distance >= minEvasionDist &&
                a_state.target.distance <= config.GetDecisionMatrix().sprintAttackMaxDistance) {
                conditionsMet = true;
            }

            // Close: any melee swing aimed at us from within its reach, rolled once per swing (it can also block,
            // back off or trade blows). Before 0.12.0 nothing dodged inside the band.
            constexpr float kCloseDodgeMargin = 30.0f;
            const float swingReach = StateHelpers::EffectiveAttackRange(
                a_state.target.weaponReach > 0.0f ? a_state.target.weaponReach : a_state.weaponReach,
                a_state.self.boundRadius, 1.0f) + kCloseDodgeMargin;
            if (!conditionsMet && !a_state.target.isRanged && !a_state.target.isInAttackRecovery &&
                a_state.target.distance <= swingReach) {
                // Own key per swing: bit 31 keeps it apart from the trade-blows roll on the same swing
                const auto swing = ((static_cast<std::uint64_t>(a_actor->GetFormID()) << 32) |
                                    a_state.temporal.target.swingCount) ^ (1ull << 31);
                closeDodge = RollOnce(swing, (std::min)(1.0f, config.GetDecisionMatrix().closeDodgeChance *
                                                            Personality::CautionScale(Personality::Effective(a_actor).caution)));
                conditionsMet = closeDodge;
            }
            const bool closeSwing = closeDodge || (!conditionsMet && !a_state.target.isRanged &&
                                                   !a_state.target.isInAttackRecovery &&
                                                   a_state.target.distance <= swingReach);

            // Check stamina
            auto actorOwner = ActorUtils::SafeAsActorValueOwner(a_actor);
            if (actorOwner) {
                float maxStamina = actorOwner->GetBaseActorValue(RE::ActorValue::kStamina);
                float currentStamina = a_state.self.staminaPercent * maxStamina;
                if (currentStamina < config.GetDodgeSystem().RequiredStamina()) {
                    conditionsMet = false;
                }
            }
            if (closeSwing && config.IsDebugTarget(a_actor->GetFormID())) {
                LOG_DEBUG("Evasion 0x{:08X}: close swing at {:.0f} (reach {:.0f}) -> {}", a_actor->GetFormID(),
                          a_state.target.distance, swingReach,
                          !closeDodge ? "no dodge (roll)" : conditionsMet ? "dodge" : "no dodge (stamina)");
            }
            closeDodge = closeDodge && conditionsMet;

            if (conditionsMet) {
                // Always dodge when conditions are met
                // Stamina cost is now properly deducted in DodgeSystem, so we don't need
                // chance-based selection
                shouldDodge = true;
            }
        }

        if (shouldDodge) {
            result.action = ActionType::Dodge;
            float baseDodgePriority = 1.5f; // Base evasion priority

            // Use enhanced combat context: Threat level
            // Higher threat level = higher priority for dodge
            ThreatLevel dodgeThreatLevel = a_state.combatContext.threatLevel;
            if (dodgeThreatLevel == ThreatLevel::Critical) {
                baseDodgePriority += 0.7f; // Critical threat - very high dodge priority
            } else if (dodgeThreatLevel == ThreatLevel::High) {
                baseDodgePriority += 0.5f; // High threat - high dodge priority
            } else if (dodgeThreatLevel == ThreatLevel::Moderate) {
                baseDodgePriority += 0.3f; // Moderate threat - moderate dodge priority
            }

            // More enemies targeting us = higher dodge priority
            if (a_state.combatContext.enemiesTargetingUs > 1) {
                baseDodgePriority += 0.3f; // Multiple enemies targeting us - dodge is critical
            }

            // Health-based priority: Low health = higher dodge priority (survival
            // instinct)
            float healthModifier = 0.0f;
            if (a_state.self.healthPercent < 0.3f) {
                // Critical health (< 30%) - very high dodge priority
                healthModifier = 0.8f;
            } else if (a_state.self.healthPercent < 0.5f) {
                // Low health (30-50%) - high dodge priority
                healthModifier = 0.5f;
            } else if (a_state.self.healthPercent < 0.7f) {
                // Moderate health (50-70%) - moderate dodge priority boost
                healthModifier = 0.2f;
            }
            baseDodgePriority += healthModifier;

            // Pressure-based priority: Target has more HP and is attacking = pressured
            // situation NPCs should prioritize dodging when outmatched
            float pressureModifier = 0.0f;
            if (a_state.target.isAttacking || a_state.target.isPowerAttacking) {
                // Target is attacking - check if we're pressured
                if (a_state.target.healthPercent > a_state.self.healthPercent + 0.2f) {
                    // Target has significantly more HP (20%+ more) - we're pressured
                    pressureModifier = 0.6f; // High priority to dodge when pressured
                } else if (a_state.target.healthPercent > a_state.self.healthPercent) {
                    // Target has more HP (even slightly) - moderate pressure
                    pressureModifier = 0.3f;
                }

                // Boost priority if target is power attacking (more dangerous)
                if (a_state.target.isPowerAttacking) {
                    pressureModifier += 0.2f;
                }
            }
            baseDodgePriority += pressureModifier;

            // Extra boost when interrupting own attack to dodge (survival takes
            // priority)
            if (!a_state.self.isIdle && (a_state.self.healthPercent < 0.5f || a_state.target.isPowerAttacking)) {
                baseDodgePriority += 0.5f; // High priority to interrupt attack and dodge
                                           // when low health or power attack incoming
            }

            // Apply weapon evasion modifier (one-handed can dodge faster, two-handed
            // slower)
            result.priority = baseDodgePriority + weaponEvasionModifier;
            // A won close-dodge roll goes first: run 30's trade-blows and punish attacks scored 3.05-4.70 and beat
            // a 3.0 floor, so it now outranks every option; back-off and trade rolls decide only lost rolls
            constexpr float kWonCloseDodgePriority = 10.0f;
            if (closeDodge) {
                result.priority = kWonCloseDodgePriority;
            }

            // Dodge intensity based on urgency (closer = more urgent)
            float distance = a_state.target.distance;
            float dodgeIntensity = 0.6f; // Base intensity
            if (distance < config.GetDecisionMatrix().evasionMinDistance) {
                dodgeIntensity = 1.0f; // Maximum intensity when very close
            } else if (distance < config.GetDecisionMatrix().evasionMinDistance &&
                       distance < config.GetDecisionMatrix().sprintAttackMaxDistance) {
                dodgeIntensity = 0.8f; // High intensity when close
            }

            // One-handed weapons can dodge faster/more agile
            if (a_state.self.isOneHanded) {
                dodgeIntensity = (dodgeIntensity + 0.1f > 1.0f) ? 1.0f : (dodgeIntensity + 0.1f);
            }

            result.intensity = dodgeIntensity;
        } else {
            // Strafe should only trigger when there's a tactical reason, not as a
            // default fallback Only strafe if:
            // 1. Target is attacking (evasion)
            // 2. Target is blocking (repositioning)
            // 3. We just finished an attack (repositioning)
            // 4. We're in melee range (tactical positioning)
            // 5. Multiple enemies (defensive positioning)

            bool shouldStrafe = false;
            float strafePriority = 1.3f;
            const float nearReach = a_state.weaponReach > 0.0f ? a_state.weaponReach : 150.0f;
            // A drawn bow counts as attacking for as long as it is held: with no distance limit a melee NPC circled
            // an archer 300-1200 units away instead of closing in (AE run, 2026-10-26). Shots are the projectile
            // dodge / jump rules' job; strafing answers a swing or a guard up close.
            const bool nearTarget = a_state.target.distance <= nearReach * 2.0f;

            // Strong reasons to strafe. A swing at someone else is no reason (1 vs many: you swing at one bandit,
            // the one beside it sidestepped)
            if (nearTarget && (a_state.target.isAttacking || a_state.target.isPowerAttacking) &&
                TargetAimedAtUs(a_actor, a_state)) {
                shouldStrafe = true;
                strafePriority = 1.6f; // Higher priority when target is attacking (evasion)
            } else if (nearTarget && a_state.target.isBlocking && !a_state.target.isAttacking) {
                shouldStrafe = true;
                strafePriority = 1.4f; // Good time to reposition for better angle
            } else if (a_state.self.attackState == RE::ATTACK_STATE_ENUM::kFollowThrough) {
                shouldStrafe = true;
                strafePriority = 1.5f; // Just finished attack, reposition
            } else {
                // Weaker reason: only reposition defensively when actually outnumbered.
                // Being in melee range must NOT by itself trigger a strafe - an idle,
                // attackable target should be ATTACKED, not circled. Previously the
                // in-melee-range strafe (priority ~1.4) outranked the offense attack, so
                // NPCs danced around a point-blank idle target forever.
                bool outnumbered = (a_state.combatContext.enemyCount > a_state.combatContext.allyCount + 1);
                bool significantlyOutnumbered =
                    (a_state.combatContext.enemyCount >= (a_state.combatContext.allyCount + 1) * 2);
                // Only near the target, as the proactive branch below: run 57's lone falmer (8 enemies) strafed 77 of
                // 107 decisions at 200-400 units and never closed in (Advancing drops to 0.95 when outnumbered)
                if (!nearTarget) {
                    outnumbered = false;
                }

                if (outnumbered) {
                    shouldStrafe = true;
                    strafePriority += 0.3f; // More defensive when outnumbered
                    if (significantlyOutnumbered) {
                        strafePriority += 0.4f; // Even higher when significantly outnumbered
                    }
                }
            }

            // Proactive repositioning when outnumbered: strafe even when target is idle
            // This helps avoid being surrounded and creates better positioning
            if (!shouldStrafe) {
                bool outnumbered = (a_state.combatContext.enemyCount > a_state.combatContext.allyCount + 1);
                bool significantlyOutnumbered =
                    (a_state.combatContext.enemyCount >= (a_state.combatContext.allyCount + 1) * 2);

                if (outnumbered) {
                    float reachDistance = a_state.weaponReach;
                    if (reachDistance <= 0.0f) {
                        reachDistance = 150.0f;
                    }

                    // Reposition when outnumbered, especially if close to target
                    bool closeToTarget = (a_state.target.distance <= reachDistance * 2.0f);

                    if (closeToTarget) { // not from afar even when badly outnumbered (run 57's falmer)
                        shouldStrafe = true;
                        strafePriority = 1.4f; // Good priority for proactive repositioning

                        if (significantlyOutnumbered) {
                            strafePriority = 1.6f; // Higher priority when significantly outnumbered
                        }

                        // Boost if we're in melee range (more urgent repositioning)
                        if (a_state.target.distance <= reachDistance * 1.2f) {
                            strafePriority += 0.2f;
                        }
                    }
                }
            }

            if (shouldStrafe) {
                result.action = ActionType::Strafe;
                result.priority = strafePriority;
                result.direction = CalculateStrafeDirection(a_state);
                result.intensity = 0.7f; // Moderate strafe speed
            }
            // If no tactical reason, return None - let other actions (offense,
            // feinting, etc.) compete
        }

        return result;
    }

    DecisionResult DecisionMatrix::EvaluateSurvival([[maybe_unused]] RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        // Check if survival retreat is enabled
        auto &config = Config::GetInstance();
        if (!config.GetDecisionMatrix().enableSurvivalRetreat) {
            return result;
        }

        // Get thresholds from config
        const float healthThreshold = config.GetDecisionMatrix().healthThreshold;
        const float staminaThreshold = config.GetDecisionMatrix().staminaThreshold;

        // Trigger: Health < threshold OR significantly outnumbered (tactical retreat)
        bool shouldRetreat = false;
        bool exhausted = false; // stamina, not health: step back and recover, don't run off
        float retreatPriority = 0.0f;

        // Check if NPC is outnumbering the target (we have more allies than enemies)
        bool outnumberingTarget = (a_state.combatContext.allyCount > a_state.combatContext.enemyCount);

        // Cornered: 8 s of retreating without healing gained it nothing (run 25: 60 s at 24% while the
        // target healed to full), so it stops running and fights
        constexpr float kRetreatGiveUpSeconds = 8.0f;
        const bool cornered = a_state.temporal.self.retreatTime >= kRetreatGiveUpSeconds;

        // Health-based retreat (survival), unless it is winning the damage race: then it presses on
        const DamageRace race = Race(a_state);
        if (a_state.self.healthPercent <= healthThreshold && !race.winning && !cornered) {
            shouldRetreat = true;
            retreatPriority = 2.0f; // Survival is highest priority

            // Multiple enemies = more urgent retreat
            if (a_state.combatContext.enemyCount > 1) {
                retreatPriority += 0.5f; // Higher priority with multiple enemies
            }

            // Reduce survival retreat priority when outnumbering target
            // NPCs with numerical advantage should be less likely to retreat even at
            // low HP CombatStyleEnhancer will further adjust based on
            // personality/traits
            if (outnumberingTarget) {
                retreatPriority *= 0.7f; // Reduce by 30% when outnumbering (still high
                                         // priority but less urgent)
            }
        }

        // A potion or poison is waiting for room (Potions::Update): get away from the target, then drink or coat
        if (!shouldRetreat && Intents::WantsToDrink(a_actor)) {
            shouldRetreat = true;
            retreatPriority = 2.0f;
        }

        // Losing the damage race: a few more of the target's hits would kill it, so it gets out even above the
        // health threshold (up to RaceRetreatMaxHealth)
        if (!shouldRetreat && race.losing && !cornered) {
            shouldRetreat = true;
            retreatPriority = 2.0f;
        }

        // Stamina-based tactical retreat: an exhausted NPC can't attack/power/sprint
        // effectively and vanilla combat AI already backs it off to recover. Match that
        // with a real Backoff decision instead of issuing an Advance the engine ignores
        // (which produced "decision says advance but the NPC backs away"). Lower
        // priority than health survival; only when not already fleeing/finishing.
        if (!shouldRetreat && config.GetDecisionMatrix().staminaAffectsRetreat && staminaThreshold > 0.0f &&
            a_state.self.staminaPercent <= staminaThreshold &&
            a_state.self.healthPercent > healthThreshold) {
            shouldRetreat = true;
            exhausted = true;
            retreatPriority = 1.5f; // Tactical - below health survival (2.0)

            // Less urgent when we have the numbers advantage
            if (outnumberingTarget) {
                retreatPriority *= 0.7f;
            }
        }

        // Use enhanced combat context: Threat level
        // Tactical retreat when significantly outnumbered OR high threat level (even
        // if health is okay) This helps avoid being surrounded and creates better
        // positioning
        ThreatLevel retreatThreatLevel = a_state.combatContext.threatLevel;
        bool outnumbered = (a_state.combatContext.enemyCount > a_state.combatContext.allyCount + 1);
        bool significantlyOutnumbered = (a_state.combatContext.enemyCount >= (a_state.combatContext.allyCount + 1) * 2);

        // Note: Comprehensive state logging is now done in LogActorState() helper
        // This specific debug log can be removed if LogActorState provides sufficient
        // detail

        // A tactical retreat (outnumbered or high threat, not health) is a careful NPC's choice: run 58's Foolhardy falmer
        // (caution 0.05) retreated 107 of 162 decisions at 50-70% health. Bosses use their own phases instead.
        const bool careful = Personality::Effective(a_actor).caution >= config.GetDecisionMatrix().tacticalRetreatMinCaution &&
                             Boss::CurrentPhase(a_actor) == Boss::Phase::None;
        if (!shouldRetreat && careful && (significantlyOutnumbered || retreatThreatLevel >= ThreatLevel::High)) {
            // Retreat when significantly outnumbered or high threat, but only if health
            // is moderate or low Don't retreat if health is high (might want to fight)
            // Exception: If we're outnumbering the target, don't retreat unless very
            // low HP CombatStyleEnhancer will further adjust based on
            // defensive/cowardly traits
            if (outnumberingTarget) {
                // We're outnumbering - only retreat if very low HP
                if (a_state.self.healthPercent < 0.2f) {
                    // Very low HP (< 20%) - allow retreat even when outnumbering
                    shouldRetreat = true;
                    retreatPriority = 1.4f; // Moderate priority - survival instinct
                }
                // Otherwise, don't retreat when outnumbering (we have advantage)
            } else if (a_state.self.healthPercent < 0.7f) {
                // Not outnumbering - normal retreat logic applies
                shouldRetreat = true;
                retreatPriority = 1.5f; // Tactical retreat priority (lower than survival)

                // Higher priority based on threat level
                if (retreatThreatLevel == ThreatLevel::Critical) {
                    retreatPriority += 0.4f; // Critical threat - very high retreat priority
                } else if (retreatThreatLevel == ThreatLevel::High) {
                    retreatPriority += 0.2f; // High threat - higher retreat priority
                }

                // Higher priority if health is lower
                if (a_state.self.healthPercent < 0.5f) {
                    retreatPriority += 0.3f; // Closer to survival priority
                }

                // More enemies targeting us = higher retreat priority
                if (a_state.combatContext.enemiesTargetingUs > 2) {
                    retreatPriority += 0.3f; // Multiple enemies targeting us - very dangerous
                }
            }
        }

        // Also retreat when outnumbered and health is low-moderate
        // But skip if we're outnumbering the target
        if (!shouldRetreat && outnumbered && !outnumberingTarget && a_state.self.healthPercent < 0.5f) {
            shouldRetreat = true;
            retreatPriority = 1.6f; // Tactical retreat when outnumbered and low health
        }

        // If we're outnumbering and already decided to retreat, reduce priority
        // significantly (unless it's survival retreat) CombatStyleEnhancer will
        // further adjust based on defensive/cowardly traits
        if (shouldRetreat && outnumberingTarget && retreatPriority < 2.0f) {
            // Reduce retreat priority when outnumbering (unless survival retreat)
            retreatPriority *= 0.6f; // Reduce by 40% when outnumbering
        }

        if (shouldRetreat) {
            // Run 10: one power attack took a bandit from 100% to 17% stamina; it then ran off ~25 s at full health
            result.action = exhausted ? ActionType::Backoff : ActionType::Retreat;
            result.priority = retreatPriority;

            // Multiple enemies = faster retreat
            if (a_state.combatContext.enemyCount > 1) {
                result.intensity = 1.0f; // Faster retreat when outnumbered
            } else {
                result.intensity = 0.8f; // Normal retreat speed
            }

            // Calculate retreat direction (away from target)
            if (a_state.target.isValid) {
                RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
                toTarget.Unitize();
                result.direction = RE::NiPoint3(-toTarget.x, -toTarget.y, 0.0f); // Opposite direction
            } else {
                // No target, retreat backward
                result.direction = RE::NiPoint3(-a_state.self.forwardVector.x, -a_state.self.forwardVector.y, 0.0f);
            }
        }

        return result;
    }

    DecisionResult DecisionMatrix::EvaluateOffense([[maybe_unused]] RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        auto &config = Config::GetInstance();
        if (!config.GetDecisionMatrix().enableOffense) {
            return result;
        }

        if (!a_state.target.isValid || !a_state.self.isIdle) {
            return result;
        }

        // Combat targets remain assigned while an NPC searches for a hidden
        // player. Avoid revealing the player's exact position through offense.
        if (a_state.target.isPlayer && !a_state.target.isDetected) {
            return result;
        }

        // Calculate max attack distance first (needed for decision logic)
        float reachDistance = a_state.weaponReach;
        if (reachDistance <= 0.0f) {
            reachDistance = 150.0f; // Default fallback
        }
        // Effective range pads weapon reach with the target's body radius so
        // center-to-center distance checks connect regardless of target size.
        float maxAttackDistance =
            EffectiveAttackRange(a_state, reachDistance, config.GetDecisionMatrix().offenseReachMultiplier,
                                 config.GetDecisionMatrix().offenseReachSafetyMargin);

        // Define sprint attack ranges
        const float sprintAttackMaxDist = config.GetDecisionMatrix().sprintAttackMaxDistance;
        const float sprintAttackMinDist = config.GetDecisionMatrix().sprintAttackMinDistance;

        // --- 1. Evaluate Advancing (Running to target) ---
        // Valid whenever we are outside normal attack range
        DecisionResult advancingResult;
        if (a_state.target.distance > maxAttackDistance) {
            bool shouldAdvance = true;

            // Don't advance if target is actively attacking (stay defensive)
            // Exception: if target is fleeing, or recovering from a swing (an opening)
            // A swing aimed at someone else doesn't stop the approach (1 vs many)
            if ((a_state.target.isAttacking || a_state.target.isPowerAttacking) && !a_state.target.isFleeing &&
                !a_state.target.isInAttackRecovery && TargetAimedAtUs(a_actor, a_state)) {
                // If we are in sprint attack range, we might still want to sprint attack
                // (checked later) But generally advancing into an attack is bad unless we
                // sprinting
                shouldAdvance = false;
            }

            if (shouldAdvance) {
                // Forward dodge closes the last stretch, not the whole approach (it used to
                // fire from beyond 2x the sprint band and chain across the map on cooldown):
                //  - gap-close: target just outside reach and open (dashing at a ready,
                //    facing target only walks into its swing, and chained on every cooldown)
                //  - counter: we just evaded and the target is stuck recovering from its swing
                //  A risky move, so a roll, not a reflex: one roll per opening (1.5 s), scaled by the NPC's risk
                //  personality, halved when outnumbered or hurt unless reckless; its own cooldown (Humanizer) and
                //  half stamina. "Target facing away" no longer counts as an opening: in 1 vs many every NPC the
                //  player isn't facing saw one (run 51: all 5 forward dodges of the selected NPCs).
                const auto &dodgeCfg = config.GetDodgeSystem();
                const float gap = a_state.target.distance - maxAttackDistance;
                const bool targetOpen = a_state.target.isCasting || a_state.target.isDrawingBow ||
                                        a_state.target.isInAttackRecovery || a_state.target.isFleeing ||
                                        a_state.target.knockState != RE::KNOCK_STATE_ENUM::kNormal;
                const bool counterOpening =
                    a_state.target.isInAttackRecovery &&
                    a_state.temporal.self.timeSinceLastEvasion <= dodgeCfg.forwardDodgeCounterWindowSeconds &&
                    gap <= dodgeCfg.forwardDodgeMaxGap * 1.5f;
                const bool guarded = a_state.target.isBlocking && a_state.target.orientationDot > 0.5f;
                const bool opening = (gap <= dodgeCfg.forwardDodgeMaxGap && targetOpen) || counterOpening;
                bool canUseForwardDodge = config.GetModIntegrations().enableTKDodgeIntegration && opening && !guarded &&
                                          !ActorUtils::IsHumanoidCreature(a_actor) &&
                                          a_state.self.currentStamina >= dodgeCfg.RequiredStamina() &&
                                          a_state.self.staminaPercent >= dodgeCfg.forwardDodgeMinStamina;
                if (canUseForwardDodge) {
                    const float risk = Personality::Effective(a_actor).risk;
                    const bool exposed = a_state.combatContext.enemyCount > a_state.combatContext.allyCount + 1 ||
                                         a_state.self.healthPercent < 0.5f;
                    float chance = dodgeCfg.forwardDodgeChance * Personality::RiskScale(risk) * (counterOpening ? 2.0f : 1.0f);
                    if (exposed && risk < 0.7f) {
                        chance *= 0.5f;
                    }
                    const auto bucket = static_cast<std::uint32_t>(
                        std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1.5f);
                    const float roll = RollValue((static_cast<std::uint64_t>(a_actor->GetFormID()) << 32) | (1ull << 61) | bucket);
                    canUseForwardDodge = roll < (std::min)(chance, 1.0f);
                    if (a_actor && Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
                        LOG_DEBUG("ForwardDodge 0x{:08X}: opening ({}) gap {:.0f}, risk {:.2f}{} -> chance {:.2f}, rolled {:.2f} (bucket {}): {}",
                                  a_actor->GetFormID(), counterOpening ? "counter" : "target busy", gap, risk,
                                  exposed ? " (exposed)" : "", (std::min)(chance, 1.0f), roll, bucket, canUseForwardDodge ? "go" : "walk");
                    }
                }
                advancingResult.action = canUseForwardDodge ? ActionType::ForwardDodge : ActionType::Advancing;

                // Base priority for advancing
                float basePriority = config.GetScoringWeights().advancingBase;

                // Boost priority if target is fleeing (pursuit)
                if (a_state.target.isFleeing) {
                    basePriority = 1.2f;
                    if (a_state.target.healthPercent < 0.3f) {
                        basePriority = 1.4f; // Very high priority to finish low-health fleeing target
                    }
                }

                // Multiple enemies = less likely to advance recklessly
                bool outnumbered = (a_state.combatContext.enemyCount > a_state.combatContext.allyCount + 1);
                if (outnumbered && !a_state.target.isFleeing) { // a fleeing target keeps the pursuit boost
                    basePriority = 0.8f; // Reduced priority when outnumbered
                }

                // Boost if target is vulnerable
                if (a_state.target.isCasting || a_state.target.isDrawingBow) {
                    basePriority += 0.2f;
                }
                if (a_state.target.knockState != RE::KNOCK_STATE_ENUM::kNormal) {
                    basePriority += 0.3f;
                }
                if (a_state.target.isInAttackRecovery) {
                    basePriority += 0.3f;
                }
                if (canUseForwardDodge && counterOpening) {
                    basePriority += 0.4f; // punish the whiff before the target recovers
                }

                advancingResult.priority = basePriority;

                // Direction calculation
                if (a_state.target.isValid) {
                    RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
                    toTarget.z = 0.0f;
                    toTarget.Unitize();
                    advancingResult.direction = toTarget;
                } else {
                    advancingResult.direction = a_state.self.forwardVector;
                }

                // Intensity based on distance
                if (a_state.target.distance > sprintAttackMaxDist * 2.0f) {
                    advancingResult.intensity = 1.0f; // Fast advance
                } else if (a_state.target.distance > sprintAttackMaxDist) {
                    advancingResult.intensity = 0.8f; // Medium-fast
                } else {
                    advancingResult.intensity = 0.6f; // Normal
                }
            }
        }

        // --- 2. Evaluate Sprint Attack ---
        // Valid in specific range, competes with Advancing
        DecisionResult sprintAttackResult;
        // Only from a real sprint (ProjectGapClose's rule). Where NPCs never sprint (vanilla
        // can leave combat movement at a run), a sprint-attack decision could never launch,
        // yet it won the whole band and starved Advancing/ForwardDodge.
        bool checkSprintAttack = a_state.self.isSprinting && a_state.target.distance > sprintAttackMinDist &&
                                 a_state.target.distance < sprintAttackMaxDist;

        if (checkSprintAttack) {
            bool shouldSprintAttack = config.GetDecisionMatrix().enableSprintAttack;

            // Don't sprint attack if target is actively attacking (too risky)
            // Exception: if target is in attack recovery or fleeing
            if ((a_state.target.isAttacking || a_state.target.isPowerAttacking) && !a_state.target.isInAttackRecovery &&
                !a_state.target.isFleeing && TargetAimedAtUs(a_actor, a_state)) {
                shouldSprintAttack = false;
            }

            // Stamina check
            if (shouldSprintAttack && config.GetDecisionMatrix().enableSprintAttackStaminaCheck) {
                float sprintAttackCost = config.GetDecisionMatrix().sprintAttackStaminaCost;
                if (a_state.self.currentStamina < sprintAttackCost) {
                    shouldSprintAttack = false; // Not enough stamina
                }
            }

            // Personality: a sprint attack is a commitment; one roll per 2 s, 40% (cautious) to 100% (reckless)
            if (shouldSprintAttack) {
                const auto bucket = static_cast<std::uint32_t>(
                    std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count() / 2.0f);
                shouldSprintAttack = RollOnce((static_cast<std::uint64_t>(a_actor->GetFormID()) << 32) | (1ull << 60) | bucket,
                                              0.4f + 0.6f * Personality::Effective(a_actor).risk);
            }

            if (shouldSprintAttack) {
                sprintAttackResult.action = ActionType::SprintAttack;

                float basePriority = config.GetScoringWeights().sprintAttackBase;

                // Stamina modifier
                float staminaModifier = 0.0f;
                if (config.GetDecisionMatrix().enableSprintAttackStaminaCheck) {
                    float currentStamina = a_state.self.currentStamina;
                    float sprintAttackCost = config.GetDecisionMatrix().sprintAttackStaminaCost;
                    if (currentStamina < sprintAttackCost * 1.2f) {
                        float staminaRatio = currentStamina / (sprintAttackCost * 1.2f);
                        staminaModifier = -0.3f * (1.0f - staminaRatio);
                    }
                }

                // Risk Assessment: Target Ready & Facing Us
                bool targetIsReady =
                    (!a_state.target.isAttacking && !a_state.target.isPowerAttacking && !a_state.target.isCasting &&
                     !a_state.target.isDrawingBow && a_state.target.knockState == RE::KNOCK_STATE_ENUM::kNormal);
                bool targetFacingUs = (a_state.target.orientationDot > 0.7f);
                float sprintOpeningRisk = 0.0f;

                if (targetIsReady && targetFacingUs) {
                    sprintOpeningRisk = -0.6f; // Significant penalty (increased from -0.5)
                    // This allows Advancing (Running) to usually win (1.0 vs 0.7) when
                    // neutral

                    if (a_state.combatContext.enemyCount > a_state.combatContext.allyCount + 1) {
                        sprintOpeningRisk = -0.8f; // Very risky when outnumbered
                    }
                }

                if (a_state.combatContext.hasNearbyAlly && a_state.combatContext.allyCount >= 1) {
                    sprintOpeningRisk += 0.3f;
                }

                // Threat level
                ThreatLevel sprintThreatLevel = a_state.combatContext.threatLevel;
                if (sprintThreatLevel >= ThreatLevel::High) {
                    basePriority = 1.0f;
                } else if (sprintThreatLevel == ThreatLevel::Moderate) {
                    basePriority = 1.1f;
                } else if (a_state.combatContext.allyCount > a_state.combatContext.enemyCount) {
                    basePriority = 1.5f;
                }

                if (a_state.combatContext.enemiesTargetingUs > 1) {
                    basePriority -= 0.2f;
                }

                // Variable priority boosts based on opportunity
                if (a_state.target.knockState != RE::KNOCK_STATE_ENUM::kNormal) {
                    basePriority += 0.5f;
                    sprintOpeningRisk += 0.3f;
                }
                if (a_state.target.isCasting || a_state.target.isDrawingBow) {
                    basePriority += 0.4f;
                    sprintOpeningRisk += 0.3f;
                }
                if (a_state.target.isInAttackRecovery) {
                    basePriority += 0.4f;
                    sprintOpeningRisk += 0.3f;
                }
                if (a_state.target.isFleeing) {
                    basePriority += 0.5f;
                    sprintOpeningRisk += 0.4f;
                    if (a_state.target.healthPercent < 0.3f) {
                        basePriority += 0.3f;
                    }
                }
                if (a_state.target.orientationDot < 0.5f) {
                    basePriority += 0.2f;
                    sprintOpeningRisk += 0.2f;
                }

                if (a_state.target.healthPercent < 0.2f) {
                    basePriority += 0.4f;
                } else if (a_state.target.healthPercent < 0.4f) {
                    basePriority += 0.2f;
                }

                basePriority += sprintOpeningRisk + staminaModifier;

                sprintAttackResult.priority = basePriority;
                sprintAttackResult.intensity = 0.9f;
            }
        }

        // --- 3. Selection Logic (Advancing / ForwardDodge vs Sprint Attack) ---
        // Outside attack range the answer is always the approach (or a sprint attack when
        // that wins). Previously the sprint band only fell back to plain Advancing, so a
        // ForwardDodge gap-close there was dropped, and once sprint attacks required a real
        // sprint the whole band returned nothing.
        const bool advancingValid =
            advancingResult.action == ActionType::Advancing || advancingResult.action == ActionType::ForwardDodge;
        if (sprintAttackResult.action == ActionType::SprintAttack) {
            // Sprint attack wins ties; risky sprints drop below the approach
            return advancingValid && advancingResult.priority > sprintAttackResult.priority ? advancingResult
                                                                                            : sprintAttackResult;
        }
        if (a_state.target.distance > maxAttackDistance && !a_state.combatContext.isInAttackRange) {
            return advancingValid ? advancingResult : result; // rejected (e.g. target swinging): None
        }

        // --- 4. Normal Attack Logic (Close Range) ---
        // Optimal range is slightly less than base reach, padded with target body radius
        float optimalAttackDistance =
            EffectiveAttackRange(a_state, reachDistance, 0.9f, config.GetDecisionMatrix().offenseReachSafetyMargin);

        // Use enhanced combat context: Range granularity
        // Only consider attacks when in attack range (use range category for more
        // precise check)
        if (a_state.combatContext.isInAttackRange || a_state.target.distance <= maxAttackDistance) {
            // In range but no attack: say which gate stopped it (0 Attack candidates in run 4)
            const bool debugOffense = a_actor && config.IsDebugTarget(a_actor->GetFormID());
            const auto skip = [&](const char *a_reason, float a_value = 0.0f) {
                if (debugOffense) {
                    LOG_DEBUG("Offense 0x{:08X}: in range (dist {:.0f}) but no attack - {} {:.2f}",
                              a_actor->GetFormID(), a_state.target.distance, a_reason, a_value);
                }
                return result;
            };

            // Line-of-sight gate: don't swing at a target on the far side of a wall,
            // railing, or pillar even when they're within reach. Skip the strike and
            // let flanking/evasion (evaluated in parallel) reposition to a clear angle.
            if (config.GetEnvSensor().enableLosGate && a_state.env.losChecked && !a_state.env.losToTarget) {
                return skip("line of sight blocked");
            }

            // Vanilla starts no plain attack from a sprint (run 27: 4 of 11 attacks turned down before its
            // checks came at sprint speed); the sprint attack is evaluated separately
            if (a_state.self.isSprinting) {
                return skip("sprinting");
            }
            if (Intents::WantsDualBlock(a_actor)) {
                return skip("holding a block"); // an attack would cancel it (run 34)
            }

            // Still equipping: run 28's bow/dagger bandit had 6 attacks refused within 0.7 s of drawing the dagger
            if (a_state.temporal.self.timeSinceWeaponSwitch < 1.0f) {
                return skip("weapon just switched, s", a_state.temporal.self.timeSinceWeaponSwitch);
            }

            // Vanilla's hit cone refuses a target well off to the side; let it turn first. 75 deg: the cone passed attacks
            // up to 82 deg off and failed some at 5-36 (runs 27-28), and at 50 deg this gate blocked 38 in-reach attacks
            // of NPCs circling the target (run 43)
            RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
            RE::NiPoint3 forward = a_state.self.forwardVector;
            toTarget.z = forward.z = 0.0f;
            if (toTarget.Unitize() > 1.0f && forward.Unitize() > 0.01f) {
                constexpr float kMaxAttackAngleCos = 0.259f; // cos 75 deg
            // A draugr's (falmer's, skeleton's) swing runs ~2 s with its attack state reading idle; a new attack
            // inside that is refused by its graph (run 58: 34 of 36 attack events 0.5-2 s after a start)
            if (ActorUtils::IsHumanoidCreature(a_actor) && Intents::SinceSwing(a_actor) < config.GetDecisionMatrix().creatureSwingSeconds) {
                return skip("creature still swinging, s", Intents::SinceSwing(a_actor));
            }
            if (!AttackReady(a_actor)) {
                return skip("graph not attack-ready (IsAttackReady false)");
            }
                const float facing = forward.Dot(toTarget);
                if (facing < kMaxAttackAngleCos) {
                    return skip("not facing the target, dot", facing);
                }
            }

            // TACTICAL CONSIDERATIONS: Don't attack blindly - need good opening

            // Don't attack if target is actively attacking (too risky - prefer
            // evasion/strafe) Exception: if target is in attack recovery
            // (kFollowThrough), we can attack during their recovery window
            // A bow or crossbow being drawn also reads as "attacking"; up close that is the opening, not a threat.
            const DamageRace race = Race(a_state);
            // A swing aimed at someone else is an opening, not a threat (1 vs many)
            if ((a_state.target.isAttacking || a_state.target.isPowerAttacking) && !a_state.target.isInAttackRecovery &&
                !a_state.target.isRanged && TargetAimedAtUs(a_actor, a_state)) {
                // Trading blows: one roll per normal swing of the target, twice the chance while winning the race
                const float trade = (std::min)(1.0f, config.GetDecisionMatrix().tradeBlowsChance * (race.winning ? 2.0f : 1.0f) *
                                                         Personality::RiskScale(Personality::Effective(a_actor).risk));
                const auto swing =
                    (static_cast<std::uint64_t>(a_actor->GetFormID()) << 32) | a_state.temporal.target.swingCount;
                // A counter (we just blocked its blow) goes in even if it chains the next swing (run 42 lost counters
                // here); otherwise one trade roll per swing
                if (!a_state.self.isGuardCounterActive && (a_state.target.isPowerAttacking || !RollOnce(swing, trade))) {
                    return skip("target is attacking"); // Let evasion handle this - strafe/dodge instead
                }
                if (debugOffense) {
                    LOG_DEBUG("Offense 0x{:08X}: trades blows with the target's swing (chance {:.2f})",
                              a_actor->GetFormID(), trade);
                }
            }

            // Use temporal state: Prevent action spam - don't attack too quickly after
            // last action
            float timeSinceLastAction = a_state.temporal.self.timeSinceLastAction;
            if (timeSinceLastAction < 0.3f) {
                // Just executed an action < 0.3s ago - too soon, prevent spam
                return skip("last action too recent, s", timeSinceLastAction);
            }

            // Don't attack if we just finished an attack (should reposition first)
            // Exception: if target is staggered/vulnerable or in attack recovery, we
            // can chain attacks
            bool targetIsVulnerable = (a_state.target.knockState != RE::KNOCK_STATE_ENUM::kNormal) ||
                                      a_state.target.isCasting || a_state.target.isDrawingBow ||
                                      a_state.target.isInAttackRecovery;
            if (a_state.self.attackState == RE::ATTACK_STATE_ENUM::kFollowThrough && !targetIsVulnerable) {
                return skip("own swing recovering"); // Prefer strafe/repositioning after attack (unless target
                               // is vulnerable)
            }

            // Use temporal state: Detect vulnerable windows based on target timing
            float targetTimeSinceLastAttack = a_state.temporal.target.timeSinceLastAttack;
            float targetIdleDuration = a_state.temporal.target.idleDuration;
            // Just finished a swing: out of it and idle for under 0.4 s. timeSinceLastAttack restarts when a swing
            // STARTS, so the old "0.1-0.5 s since the last attack" test read the target's wind-up as an opening.
            const bool targetJustFinished =
                !a_state.target.isAttacking && targetTimeSinceLastAttack < 2.0f && targetIdleDuration < 0.4f;

            // Boost priority if target just finished attacking (recovery window)
            if (targetJustFinished) {
                targetIsVulnerable = true; // Target just finished attack - recovery window
            }

            // Boost priority if target has been idle for a while (good attack
            // opportunity)
            if (targetIdleDuration > 1.0f) {
                targetIsVulnerable = true; // Target idle for > 1s - vulnerable window
            }

            // Use enhanced combat context: Threat level
            // Higher threat level = more defensive, lower threat level = more
            // aggressive
            float priorityModifier = 0.0f;
            ThreatLevel attackThreatLevel = a_state.combatContext.threatLevel;

            if (attackThreatLevel == ThreatLevel::Critical) {
                // Critical threat (5+ enemies) - very defensive
                priorityModifier = -0.7f;
            } else if (attackThreatLevel == ThreatLevel::High) {
                // High threat (3-4 enemies) - defensive
                priorityModifier = -0.5f;
            } else if (attackThreatLevel == ThreatLevel::Moderate) {
                // Moderate threat (2 enemies) - slightly defensive
                priorityModifier = -0.2f;
            } else if (attackThreatLevel == ThreatLevel::Low &&
                       a_state.combatContext.allyCount > a_state.combatContext.enemyCount) {
                // Low threat (1 enemy) and we have allies - can be aggressive
                priorityModifier = 0.2f;
            }

            // Also consider enemies targeting us specifically (more accurate threat
            // assessment)
            if (a_state.combatContext.enemiesTargetingUs > 2) {
                priorityModifier -= 0.3f; // Multiple enemies targeting us - very defensive
            } else if (a_state.combatContext.enemiesTargetingUs == 1 && a_state.combatContext.allyCount > 0) {
                priorityModifier += 0.1f; // Only one enemy targeting us and we have allies - safer
            }

            // Flanking bonus: if we're flanking (behind/side of target), attacks are
            // more effective
            float flankingBonus = 0.0f;
            if (a_state.target.orientationDot < 0.3f) {
                // We're behind or to the side of target - excellent flanking position
                flankingBonus = config.GetScoringWeights().flankingAttackBonus;
                if (a_state.combatContext.hasNearbyAlly) {
                    flankingBonus = 0.4f; // Even better if ally is engaging target
                }
            }

            // Health-based modifier: Lower health = more cautious, but also consider
            // relative health
            float healthModifier = 0.0f;
            if (a_state.self.healthPercent < 0.4f) {
                healthModifier = -0.3f; // Low health - be more defensive
            } else if (a_state.self.healthPercent > 0.7f) {
                // High health - can be more aggressive
                healthModifier = 0.1f;
            }

            // Relative health advantage: if we have significantly more health, be more
            // aggressive
            if (a_state.target.isValid) {
                float healthDifference = a_state.self.healthPercent - a_state.target.healthPercent;
                if (healthDifference > 0.2f) {
                    // We have a significant health advantage, be more aggressive
                    healthModifier += 0.2f;
                } else if (healthDifference < -0.2f) {
                    // We have a significant health disadvantage, be more cautious
                    healthModifier -= 0.2f;
                }
            }

            // Attack creates opening consideration: attacks don't consume stamina but
            // create openings for target This makes actors more cautious about
            // attacking when target is ready to counter-attack
            float openingRiskModifier = 0.0f;

            // Check if target is ready/alert (can counter-attack effectively)
            // Target is NOT ready if: attacking, in recovery, casting, drawing,
            // staggered, or fleeing
            bool targetIsReady =
                (!a_state.target.isAttacking && !a_state.target.isPowerAttacking && !a_state.target.isCasting &&
                 !a_state.target.isDrawingBow && !a_state.target.isInAttackRecovery && !a_state.target.isFleeing &&
                 a_state.target.knockState == RE::KNOCK_STATE_ENUM::kNormal);

            // Check if target is facing us (more dangerous - can counter-attack)
            bool targetFacingUs = (a_state.target.orientationDot > 0.7f);

            if (targetIsReady && targetFacingUs) {
                // Target is ready and facing us - attacking creates a significant opening
                openingRiskModifier = -config.GetScoringWeights().openingRiskPenalty; // Risky to attack

                // Even riskier when outnumbered (multiple enemies can counter)
                if (a_state.combatContext.enemyCount > a_state.combatContext.allyCount + 1) {
                    openingRiskModifier = -0.6f; // Very risky when outnumbered
                }
            } else if (targetIsReady && !targetFacingUs) {
                // Target is ready but not facing us - moderate risk
                openingRiskModifier = -0.2f;
            }

            // Safer to attack when we have allies (they can cover us)
            if (a_state.combatContext.hasNearbyAlly && a_state.combatContext.allyCount >= 1) {
                openingRiskModifier += config.GetScoringWeights().allyCoverBonus; // Allies can cover
                if (a_state.combatContext.allyCount >= 2) {
                    openingRiskModifier += 0.1f; // Multiple allies - even safer
                }
            }

            // Target is vulnerable - attacking is safer (smaller opening)
            if (!targetIsReady || a_state.target.knockState != RE::KNOCK_STATE_ENUM::kNormal ||
                a_state.target.isCasting || a_state.target.isDrawingBow || a_state.target.isInAttackRecovery ||
                a_state.target.isFleeing) {
                openingRiskModifier += 0.3f; // Target vulnerable - safer to attack
            }

            // Target state modifiers - boost priority when opportunities exist
            float targetStateModifier = 0.0f;
            bool hasGoodOpening = false;

            // Excellent opportunities - significant boosts
            if (a_state.target.knockState != RE::KNOCK_STATE_ENUM::kNormal) {
                targetStateModifier += config.GetScoringWeights().targetStaggeredBonus; // Target staggered
                hasGoodOpening = true;
            }
            if (a_state.target.isCasting || a_state.target.isDrawingBow) {
                targetStateModifier += config.GetScoringWeights().targetCastingBonus; // Target casting/drawing
                hasGoodOpening = true;
            }

            // (A blocking target is no opening for a normal attack: it bounces off. Bash, feint and power attacks
            // have their own rules for a guard.)

            // Target is idle/not ready - good opportunity, especially when close
            // Note: We don't have target idle state, but we can infer from not
            // attacking/blocking
            bool targetIsIdle =
                (!a_state.target.isAttacking && !a_state.target.isBlocking && !a_state.target.isPowerAttacking &&
                 !a_state.target.isCasting && !a_state.target.isDrawingBow);

            // Target in attack recovery is also a good opportunity
            // Use temporal state: Detect recovery window timing (reuse variable
            // declared above)
            if (a_state.target.isInAttackRecovery || targetJustFinished) {
                targetStateModifier += config.GetScoringWeights().targetRecoveryBonus; // Target recovering
                hasGoodOpening = true;

                // Extra bonus if target just finished attack (immediate recovery window)
                if (targetJustFinished && targetIdleDuration < 0.25f) {
                    targetStateModifier += 0.2f; // Just finished attack - perfect timing
                }
            }

            // Target fleeing is a good opportunity for pursuit/finishing
            if (a_state.target.isFleeing) {
                targetStateModifier += config.GetScoringWeights().targetFleeingBonus; // Target fleeing
                hasGoodOpening = true;

                // Extra bonus if target is low health (finishing move opportunity)
                if (a_state.target.healthPercent < 0.3f) {
                    targetStateModifier += 0.3f; // Target low health and fleeing - finish them
                }
            }

            // Target low health - finishing move opportunity
            if (a_state.target.healthPercent < 0.2f) {
                targetStateModifier += config.GetScoringWeights().targetLowHealthFinisherBonus; // Very low health
                hasGoodOpening = true;
            } else if (a_state.target.healthPercent < 0.4f) {
                targetStateModifier += 0.2f; // Target low health - pressure them
            }

            // Target low stamina - easier to pressure
            if (a_state.target.staminaPercent < 0.2f) {
                targetStateModifier += 0.2f; // Target low stamina - easier to pressure
            }

            // Use temporal state: Detect idle target vulnerability (reuse variable
            // declared above)
            if (targetIsIdle || targetIdleDuration > 0.5f) {
                // Idle target is a good opportunity, especially when very close
                float idleBonus = 0.2f; // Base bonus for idle target

                // Boost based on how long target has been idle (longer = more vulnerable)
                if (targetIdleDuration > 1.5f) {
                    idleBonus += 0.3f; // Target idle for > 1.5s - very vulnerable
                } else if (targetIdleDuration > 1.0f) {
                    idleBonus += 0.2f; // Target idle for > 1s - vulnerable
                }

                // Boost significantly when very close to idle target (perfect
                // opportunity)
                if (a_state.target.distance <= optimalAttackDistance) {
                    idleBonus += 0.2f; // Much better opportunity when close
                } else if (a_state.target.distance <= optimalAttackDistance * 1.2f) {
                    idleBonus += 0.1f; // Good opportunity when moderately close
                }

                targetStateModifier += idleBonus;
                hasGoodOpening = true; // Idle target counts as good opening
            }

            // Orientation bonus: Higher priority if target is not facing us directly
            if (a_state.target.orientationDot < 0.5f) {
                targetStateModifier += 0.2f; // Target not facing us - good opportunity
                hasGoodOpening = true;
            }

            // Coordination bonus: if allies are engaging the target, attacks are safer
            float coordinationBonus = 0.0f;
            if (a_state.combatContext.hasNearbyAlly && a_state.combatContext.allyCount >= 1) {
                coordinationBonus = 0.15f; // Allies present - safer to attack
                if (a_state.combatContext.allyCount >= 2) {
                    coordinationBonus = 0.25f; // Multiple allies - even safer
                }
            }

            // Outnumbering bonus: when we outnumber the target, attacks are safer and
            // more effective
            float outnumberingBonus = 0.0f;
            bool outnumberingTarget = (a_state.combatContext.allyCount > a_state.combatContext.enemyCount);
            if (outnumberingTarget) {
                outnumberingBonus = 0.3f; // Significant bonus when outnumbering
                if (a_state.combatContext.allyCount >= a_state.combatContext.enemyCount + 2) {
                    outnumberingBonus = 0.5f; // Even higher bonus when significantly outnumbering
                }
                hasGoodOpening = true; // Outnumbering counts as good opening
            }

            // If no good opening, reduce priority (but less harsh when very close)
            if (!hasGoodOpening) {
                // Less penalty when very close - should still attack when right in face
                if (a_state.target.distance <= optimalAttackDistance * 0.8f) {
                    priorityModifier -= 0.2f; // Reduced penalty when very close
                } else {
                    priorityModifier -= 0.5f; // Full penalty when further away
                }
            }

            // Base priority - competitive but not overriding
            float basePriority = config.GetScoringWeights().attackBase + priorityModifier + healthModifier +
                                 openingRiskModifier + targetStateModifier +
                                 flankingBonus + coordinationBonus + outnumberingBonus;

            // Weapon type considerations: Different weapons have different optimal
            // ranges Two-handed weapons have longer reach, one-handed are shorter
            float weaponReachModifier = 0.0f;
            if (a_state.self.isTwoHanded) {
                // Two-handed weapons have longer reach - attacks are viable at longer
                // distances
                weaponReachModifier += 0.1f; // Bonus for reach advantage
            } else if (a_state.self.weaponType == WeaponType::OneHandedDagger) {
                // Daggers have short reach - need to be very close
                weaponReachModifier -= 0.15f; // Penalty for short reach
            }

            // Matchup considerations: Reach advantage matters
            if (a_state.self.isTwoHanded && a_state.target.isOneHanded) {
                weaponReachModifier += 0.15f; // Reach advantage against one-handed
            } else if (a_state.self.isOneHanded && a_state.target.isTwoHanded) {
                weaponReachModifier -= 0.2f; // Reach disadvantage - need to get closer
            }

            // Use enhanced combat context: Range granularity
            // Use range category for more precise distance-based decision-making
            float distanceModifier = 0.0f;
            RangeCategory rangeCategory = a_state.combatContext.rangeCategory;

            switch (rangeCategory) {
            case RangeCategory::CloseRange:
                // Very close - maximum priority boost
                distanceModifier = 0.7f; // Significant boost for close range
                break;
            case RangeCategory::OptimalRange:
                // Optimal range - good priority boost
                distanceModifier = 0.4f; // Good boost for optimal range
                break;
            case RangeCategory::MaxRange:
                // Max range - moderate penalty
                distanceModifier = -0.2f; // Slight penalty for max range
                break;
            case RangeCategory::OutOfRange:
                // Out of range - significant penalty (but still allow if very close to
                // max range)
                if (a_state.target.distance <= maxAttackDistance * 1.1f) {
                    distanceModifier = -0.3f; // Slightly out of range but close
                } else {
                    distanceModifier = -0.5f; // Significant penalty for out of range
                }
                break;
            }

            // Apply weapon reach modifier to distance modifier
            distanceModifier += weaponReachModifier;

            // Final priority calculation (includes weapon reach modifier)
            float finalPriority = basePriority + distanceModifier;

            // Attack defense feedback: If attacks are frequently parried/timed blocked,
            // reduce attack priority This encourages NPCs to use feints or vary timing
            // instead of direct attacks
            float defenseFeedbackPenalty = 0.0f;
            if (a_state.temporal.self.totalDefenseRate > 0.3f) {
                // High defense rate (>30%) - significantly reduce attack priority
                defenseFeedbackPenalty = -0.4f - (a_state.temporal.self.totalDefenseRate - 0.3f) *
                                                     0.8f; // Up to -0.96f for 100% defense rate
            } else if (a_state.temporal.self.totalDefenseRate > 0.15f) {
                // Moderate defense rate (15-30%) - moderate penalty
                defenseFeedbackPenalty = -0.2f;
            } else if (a_state.temporal.self.lastAttackParried || a_state.temporal.self.lastAttackTimedBlocked) {
                // Recent parry/timed block - immediate penalty
                defenseFeedbackPenalty = -0.3f;
            }
            finalPriority += defenseFeedbackPenalty;

            // Hit/miss feedback: Adjust attack priority based on recent hit success
            // If last attack hit, encourage follow-up attacks (combos)
            // If last attack missed, reduce priority slightly (need better
            // timing/positioning)
            float hitMissFeedback = 0.0f;
            if (a_state.temporal.self.lastAttackHit && a_state.temporal.self.timeSinceLastHitAttack < 1.0f) {
                // Recent hit - encourage follow-up attacks (combo opportunity)
                hitMissFeedback = 0.2f;
            } else if (a_state.temporal.self.lastAttackMissed &&
                       a_state.temporal.self.timeSinceLastMissedAttack < 1.0f) {
                // Recent miss - slight penalty (need better timing/positioning)
                hitMissFeedback = -0.15f;
            } else if (a_state.temporal.self.missRate > 0.5f && a_state.temporal.self.totalAttackCount >= 5) {
                // High miss rate (>50%) with enough data - reduce attack priority
                hitMissFeedback =
                    -0.2f - (a_state.temporal.self.missRate - 0.5f) * 0.4f; // Up to -0.4f for 100% miss rate
            } else if (a_state.temporal.self.hitRate > 0.6f && a_state.temporal.self.totalAttackCount >= 5) {
                // High hit rate (>60%) with enough data - encourage attacks
                hitMissFeedback = 0.15f;
            }
            finalPriority += hitMissFeedback;

            // Damage momentum: press the advantage when our recent hits chunk the target,
            // turn cautious (let evasion/backoff win) when we only chip. Neutral until we
            // have real data so combat never opens defensively. Penalty is capped so a
            // chipping NPC still attacks periodically (anti-turtle for tanky targets).
            const auto &dm = config.GetDamageMomentum();
            if (dm.enableDamageMomentum && a_state.temporal.self.hasMomentumData) {
                float frac = a_state.temporal.self.recentDamageFraction;
                float momentum; // -1 (chip) .. +1 (big hits)
                if (frac >= dm.bigHitThreshold) {
                    momentum = 1.0f;
                } else if (frac <= dm.chipThreshold) {
                    momentum = -1.0f;
                } else {
                    // linear map [chip, big] -> [-1, +1]
                    float t = (frac - dm.chipThreshold) / (dm.bigHitThreshold - dm.chipThreshold);
                    momentum = t * 2.0f - 1.0f;
                }
                if (momentum >= 0.0f) {
                    finalPriority += momentum * dm.offensiveBonusMax;
                } else {
                    finalPriority -= (-momentum) * dm.defensivePenaltyMax;
                }
            }

            // Winning the damage race: keep pressing
            if (race.winning) {
                finalPriority += config.GetDamageMomentum().racePressBonus;
            }

            // Additional weapon type considerations for attack priority
            // Two-handed weapons are slower but hit harder - prefer when target is
            // vulnerable One-handed weapons are faster - prefer when target is ready
            // (can react faster)
            if (a_state.self.isTwoHanded && targetIsVulnerable) {
                finalPriority += 0.15f; // Two-handed excels against vulnerable targets
            } else if (a_state.self.isOneHanded && targetIsReady) {
                finalPriority += 0.1f; // One-handed can react faster to ready targets
            }

            // Matchup advantage: Two-handed vs one-handed
            if (a_state.self.isTwoHanded && a_state.target.isOneHanded) {
                finalPriority += 0.1f; // Reach and power advantage
            } else if (a_state.self.isOneHanded && a_state.target.isTwoHanded) {
                finalPriority -= 0.1f; // Reach disadvantage - need better positioning
            }

            // Only proceed if priority is reasonable (don't attack with very low
            // priority)
            if (finalPriority < 0.5f) {
                return skip("priority too low", finalPriority); // Priority too low - prefer other actions
            }

            // Power Attack vs Normal Attack decision
            // Power attacks consume stamina but create bigger openings and are more
            // effective Choose power attack when we have good opportunities, allies to
            // cover us, and enough stamina (if enabled)
            bool shouldPowerAttack = false;
            float powerAttackBonus = 0.0f;

            // Guard Counter: the window after this NPC blocked a hit (0.20.0, no mod needed) or EldenCounter's;
            // strongly prefer a power attack (a normal one when stamina is short, below)
            if (a_state.self.isGuardCounterActive) {
                // Guard counter window is active - this is a perfect opportunity for
                // power attack
                powerAttackBonus += 2.0f; // Very high priority for guard counter
                shouldPowerAttack = true; // Always prefer power attack during guard counter
            }

            // Weapon type considerations: Two-handed weapons are better for power
            // attacks One-handed weapons are faster, so normal attacks might be better
            // An enraged boss (or a brave desperate one) hits harder
            if (const auto phase = Boss::CurrentPhase(a_actor);
                phase == Boss::Phase::Enraged || (phase == Boss::Phase::Desperate && Boss::Brave(a_actor))) {
                powerAttackBonus += 0.3f;
                shouldPowerAttack = true;
            }
            if (a_state.self.isTwoHanded) {
                powerAttackBonus += 0.2f; // Two-handed weapons excel at power attacks
                shouldPowerAttack = true; // Prefer power attacks with two-handed
            } else if (a_state.self.weaponType == WeaponType::OneHandedDagger) {
                powerAttackBonus -= 0.2f; // Daggers are fast, prefer normal attacks
            }

            // Matchup considerations: Power attacks are better against certain weapon
            // types
            if (a_state.target.isOneHanded && a_state.self.isTwoHanded) {
                powerAttackBonus += 0.15f; // Two-handed vs one-handed: power attack advantage
            } else if (a_state.target.isTwoHanded && a_state.self.isOneHanded) {
                powerAttackBonus -= 0.1f; // One-handed vs two-handed: prefer faster normal attacks
            }

            // Use temporal state: Space out power attacks appropriately
            float timeSinceLastPowerAttack = a_state.temporal.self.timeSinceLastPowerAttack;
            if (timeSinceLastPowerAttack < 2.0f) {
                // Just used power attack < 2s ago - space them out, prefer normal attack
                result.action = ActionType::Attack;
                result.priority = finalPriority;
                result.intensity = 0.6f; // Moderate intensity for normal attack
                return result;
            }

            // Power attacks require stamina - check actual stamina value against cost
            // (only if stamina check is enabled)
            if (config.GetDecisionMatrix().enablePowerAttackStaminaCheck) {
                float powerAttackCost = config.GetDecisionMatrix().powerAttackStaminaCost;
                if (a_state.self.currentStamina < powerAttackCost) {
                    // Not enough stamina for power attack - use normal attack instead
                    result.action = ActionType::Attack;
                    result.priority = finalPriority;
                    result.intensity = 0.6f; // Moderate intensity for normal attack
                    return result;
                }
            }

            // Power attacks are better when we have good openings
            if (hasGoodOpening) {
                powerAttackBonus += 0.3f; // was "= 0.3f", which wiped the guard counter's +2.0 and the weapon bonuses
                shouldPowerAttack = true;
            }

            // Power attacks are safer when we have allies (they can cover the bigger
            // opening)
            if (a_state.combatContext.hasNearbyAlly && a_state.combatContext.allyCount >= 1) {
                powerAttackBonus += 0.2f; // Allies can cover - safer for power attack
                shouldPowerAttack = true;
            }

            // Use enhanced combat context: Threat level
            // Power attacks are riskier when high threat level and target is ready
            ThreatLevel powerThreatLevel = a_state.combatContext.threatLevel;
            if (targetIsReady && targetFacingUs && powerThreatLevel >= ThreatLevel::High) {
                powerAttackBonus -= 0.4f;  // Very risky when high threat and target ready
                shouldPowerAttack = false; // Prefer normal attack instead
            } else if (targetIsReady && targetFacingUs && powerThreatLevel == ThreatLevel::Moderate) {
                powerAttackBonus -= 0.2f; // Risky when moderate threat and target ready
            }

            // More enemies targeting us = less likely to power attack
            if (a_state.combatContext.enemiesTargetingUs > 1) {
                powerAttackBonus -= 0.3f; // Multiple enemies targeting us - power attack is very risky
                if (a_state.combatContext.enemiesTargetingUs > 2) {
                    shouldPowerAttack = false; // Too many enemies targeting us - prefer normal attack
                }
            }

            // Stamina consideration: reduce power attack priority if stamina is low
            // (only if stamina check is enabled) Power attacks consume stamina, so we
            // need to consider if we have enough left
            float powerAttackStaminaModifier = 0.0f;
            if (config.GetDecisionMatrix().enablePowerAttackStaminaCheck) {
                float currentStamina = a_state.self.currentStamina;
                float powerAttackCost = config.GetDecisionMatrix().powerAttackStaminaCost;

                // Reduce priority if stamina is critically low relative to cost
                if (currentStamina < powerAttackCost * 1.2f) {
                    // Stamina is barely enough - reduce priority
                    float staminaRatio = currentStamina / (powerAttackCost * 1.2f); // 0.0 to 1.0
                    powerAttackStaminaModifier =
                        -0.2f * (1.0f - staminaRatio); // Reduce by up to 20% when low stamina
                }
            }

            if (shouldPowerAttack) {
                result.action = ActionType::PowerAttack;

                // Power attacks are especially effective against blocking targets
                if (a_state.target.isBlocking && !a_state.target.isAttacking) {
                    powerAttackBonus += 0.3f; // Power attacks break blocks better

                    // Two-handed weapons break blocks even better
                    if (a_state.self.isTwoHanded) {
                        powerAttackBonus += 0.2f; // Two-handed power attacks devastate blocks
                    }
                }

                // Power attacks are good for finishing (when target is
                // staggered/vulnerable)
                if (a_state.target.knockState != RE::KNOCK_STATE_ENUM::kNormal) {
                    powerAttackBonus += 0.2f; // Finish staggered target with power attack
                }

                // Power attacks are excellent when target is in attack recovery
                if (a_state.target.isInAttackRecovery) {
                    powerAttackBonus += 0.3f; // Target recovering - perfect power attack opportunity
                }

                // Power attacks are excellent for finishing fleeing/low-health targets
                if (a_state.target.isFleeing) {
                    powerAttackBonus += 0.3f; // Target fleeing - finish them with power attack
                }
                if (a_state.target.healthPercent < 0.2f) {
                    powerAttackBonus += 0.3f; // Target very low health - finish with power attack
                } else if (a_state.target.healthPercent < 0.4f) {
                    powerAttackBonus += 0.2f; // Target low health - pressure with power attack
                }

                // Power attacks are better when flanking (harder to block)
                if (flankingBonus > 0.0f) {
                    powerAttackBonus += 0.2f; // Flanking power attack is devastating
                }

                // Only when it scores above the normal attack: a power attack was picked even with a net negative
                // bonus (e.g. two-handed +0.2, two enemies on us -0.3), below what a normal attack would score
                const float powerExtra = powerAttackBonus + powerAttackStaminaModifier;
                if (powerExtra > 0.0f) {
                    result.priority = finalPriority + powerExtra;
                    result.intensity = 0.8f; // High intensity for committed power attack
                } else {
                    result.action = ActionType::Attack;
                    result.priority = finalPriority;
                    result.intensity = 0.6f;
                }
            } else {
                result.action = ActionType::Attack;
                result.priority = finalPriority;
                // Normal attack - moderate intensity
                result.intensity = 0.6f; // Moderate intensity for normal attack
            }
        }

        return result;
    }

    DecisionResult DecisionMatrix::EvaluateBackoff(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        // Check if target is valid
        if (!a_state.target.isValid) {
            return result;
        }

        auto &config = Config::GetInstance();
        const float sprintAttackMaxDist = config.GetDecisionMatrix().sprintAttackMaxDistance;
        const float sprintAttackMinDist = config.GetDecisionMatrix().sprintAttackMinDistance;

        // dont backoff if target is too far away and not facing me
        if (a_state.target.distance > sprintAttackMaxDist && a_state.target.orientationDot < 0.7f) {
            return result;
        }

        // Check if target is casting magic OR drawing bow OR attacking aggressively
        bool shouldBackoff = false;
        float urgencyModifier = 0.0f;

        // Only a threat aimed at this NPC: a cast, shot or swing at someone else is no reason to back off
        const bool aimedAtUs = TargetAimedAtUs(a_actor, a_state);

        // Primary triggers: Target casting or drawing bow (vulnerable to ranged
        // attacks)
        if ((a_state.target.isCasting || a_state.target.isDrawingBow) && aimedAtUs) {
            shouldBackoff = true;
            urgencyModifier = 0.3f; // High urgency for ranged threats
        }

        // Secondary trigger: Target attacking aggressively (power attack or rapid
        // attacks) Only backoff if we're in close range and target is being very
        // aggressive
        // Not once the blow has landed: the target's recovery is the opening, not the threat
        const bool swingThreat = !a_state.target.isInAttackRecovery && aimedAtUs;
        if (swingThreat && a_state.target.isPowerAttacking) {
            shouldBackoff = true;
            urgencyModifier = (std::max)(urgencyModifier,
                                         0.4f); // Very high urgency for power attacks
        } else if (swingThreat && a_state.target.isAttacking && a_state.target.distance <= sprintAttackMinDist) {
            // Target is attacking and we're very close - backoff to create space
            shouldBackoff = true;
            urgencyModifier = (std::max)(urgencyModifier,
                                         0.2f); // Moderate urgency for normal attacks in close range
        }

        // After its own swing's hit (next-attack window or follow-through) step out of the counter instead of
        // standing in reach, unless the target is open to a follow-up (recovering or staggered). Offense and
        // evasion stay silent until the attack ends (they need attackState kNone). Run 14: NPC swings went
        // hit -> next attack -> none, with follow-through in 1 of 968 snapshots, so both windows count.
        const bool afterHit = a_state.self.attackState == RE::ATTACK_STATE_ENUM::kNextAttack ||
                              a_state.self.attackState == RE::ATTACK_STATE_ENUM::kFollowThrough;
        if (!shouldBackoff && config.GetDecisionMatrix().backoffAfterSwing && afterHit && !a_state.target.isInAttackRecovery &&
            a_state.target.knockState == RE::KNOCK_STATE_ENUM::kNormal &&
            a_state.target.distance <= EffectiveAttackRange(a_state, a_state.weaponReach,
                                                            config.GetDecisionMatrix().offenseReachMultiplier)) {
            shouldBackoff = true;
        }

        // Additional consideration: Low health = more urgent backoff
        if (a_state.self.healthPercent < 0.4f) {
            urgencyModifier += 0.2f; // More urgent when low on health
        }

        if (shouldBackoff) {
            result.action = ActionType::Backoff;

            // Base priority (between Evasion and Survival)
            float basePriority = config.GetScoringWeights().backoffBase;

            // Apply urgency modifier
            basePriority += urgencyModifier;

            // Multiple enemies = more urgent backoff
            if (a_state.combatContext.enemyCount > 1) {
                basePriority += 0.4f; // Higher priority when outnumbered
            }

            // Low stamina = more urgent backoff (conserve stamina), only when the config says stamina matters
            if (Config::GetInstance().GetDecisionMatrix().staminaAffectsRetreat && a_state.self.staminaPercent < 0.3f) {
                basePriority += 0.2f;
            }

            result.priority = basePriority;

            // Calculate backoff direction (away from target)
            RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
            toTarget.z = 0.0f; // Keep on horizontal plane
            toTarget.Unitize();
            result.direction = RE::NiPoint3(-toTarget.x, -toTarget.y, 0.0f); // Opposite direction

            // Set intensity based on distance and urgency - closer = faster backoff
            float distance = a_state.target.distance;
            float baseIntensity = 0.5f;
            if (distance <= sprintAttackMinDist) {
                baseIntensity = 1.0f; // Fast backoff if very close
            } else if (distance >= sprintAttackMinDist && distance <= sprintAttackMaxDist) {
                baseIntensity = 0.7f; // Medium backoff
            }

            // Increase intensity based on urgency
            result.intensity = (std::min)(1.0f, baseIntensity + urgencyModifier * 0.3f);
        }

        return result;
    }

    RE::NiPoint3 DecisionMatrix::CalculateStrafeDirection(const ActorStateData &a_state)
    {
        if (!a_state.target.isValid) {
            return RE::NiPoint3(1.0f, 0.0f, 0.0f); // Default right
        }

        // Calculate direction to target
        RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
        toTarget.z = 0.0f; // Keep on horizontal plane
        toTarget.Unitize();

        // Calculate perpendicular (strafe) direction
        // Rotate 90 degrees: (x, y) -> (-y, x) for right, (y, -x) for left
        // We'll use right strafe by default
        RE::NiPoint3 strafeDir(-toTarget.y, toTarget.x, 0.0f);
        strafeDir.Unitize();

        // Group Tactics: Wolf Pack
        // If we have an ally nearby, check if strafing right moves us closer to them
        // If so, strafe left instead to spread out/encircle the target
        if (a_state.combatContext.hasNearbyAlly && a_state.combatContext.closestAllyDistance > 0.0f) {
            RE::NiPoint3 toAlly = a_state.combatContext.closestAllyPosition - a_state.self.position;
            toAlly.z = 0.0f;
            toAlly.Unitize();

            // specific debug logging for development
            // float dot = strafeDir.Dot(toAlly);

            // Dot > 0 means vectors point in similar direction (angle < 90)
            // If strafing right points towards ally, strafe left instead
            if (strafeDir.Dot(toAlly) > 0.2f) { // 0.2 buffer to prevent jitter when ally is perpendicular
                // Switch to left strafe: (y, -x)
                strafeDir = RE::NiPoint3(toTarget.y, -toTarget.x, 0.0f);
                strafeDir.Unitize();
            }
        }

        return strafeDir;
    }

    DecisionResult DecisionMatrix::EvaluateFlanking(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        // Flanking only makes sense when:
        // 1. We have allies nearby
        // 2. Target is valid
        // 3. We're in melee range or close to it
        if (!a_state.target.isValid || !a_state.combatContext.hasNearbyAlly) {
            return result;
        }

        // Only consider flanking if we have at least one ally
        if (a_state.combatContext.allyCount < 1) {
            return result;
        }

        // Flanking is most effective when:
        // - Target is engaged with another actor (ally)
        // - We can position ourselves to the side/back of target
        // - We're in melee range or approaching it
        // - We outnumber the target (more allies = higher priority)

        // An ally that is known and within ~3000 units. This used to test the ally's world position (|pos|^2 > 1e7),
        // which outdoors (coordinates ~70000) always failed, so flanking never ran in exteriors.
        RE::NiPoint3 allyPos = a_state.combatContext.closestAllyPosition;
        const float allyDist = a_state.combatContext.closestAllyDistance;
        if (allyDist <= 0.0f || allyDist > 3000.0f) {
            return result;
        }

        // Check if target is facing towards an ally (engaged with ally)
        RE::NiPoint3 targetToAlly = allyPos - a_state.target.position;
        float targetToAllyDistSq =
            targetToAlly.x * targetToAlly.x + targetToAlly.y * targetToAlly.y + targetToAlly.z * targetToAlly.z;
        if (targetToAllyDistSq < 0.01f) { // Too close, avoid division by zero
            return result;
        }
        float targetToAllyDist = std::sqrt(targetToAllyDistSq);
        targetToAlly.x /= targetToAllyDist;
        targetToAlly.y /= targetToAllyDist;
        targetToAlly.z /= targetToAllyDist;

        // Calculate angle between target's forward vector and direction to ally
        float targetAllyDot = a_state.target.forwardVector.Dot(targetToAlly);

        // Relaxed condition: target doesn't need to face directly at ally
        // If target is facing somewhat towards ally (dot > 0.0) or ally is close,
        // consider them engaged Also check if ally is in reasonable combat range
        bool targetEngagedWithAlly = (targetAllyDot > 0.0f && targetToAllyDist < 1200.0f) ||
                                     (targetToAllyDist < 600.0f); // If ally is very close, assume engagement

        // Calculate our position relative to target
        RE::NiPoint3 targetToSelf = a_state.self.position - a_state.target.position;
        float targetToSelfDistSq =
            targetToSelf.x * targetToSelf.x + targetToSelf.y * targetToSelf.y + targetToSelf.z * targetToSelf.z;
        if (targetToSelfDistSq < 0.01f) { // Too close, avoid division by zero
            return result;
        }
        float targetToSelfDist = std::sqrt(targetToSelfDistSq);
        targetToSelf.x /= targetToSelfDist;
        targetToSelf.y /= targetToSelfDist;
        targetToSelf.z /= targetToSelfDist;
        float targetSelfDot = a_state.target.forwardVector.Dot(targetToSelf);

        // Optimal flanking position: to the side or behind target (dot < 0.3). Already there: no flanking, attack from
        // here (offense adds its flanking bonus). Run 42: with flanking finally running outdoors, the "maintain the
        // position" rules kept NPCs circling: Flanking 1662 of ~1960 executed actions, Attack 46.
        bool isInFlankingPosition = (targetSelfDot < 0.3f);
        if (isInFlankingPosition) {
            return result;
        }

        // Flanking is a short arc near the target, not the approach. Run 53: Flanking 950 of 1633 actions, Attack 6;
        // it won from up to 1500 units (priority 1.4-2.2 against Advancing's 1.0-1.4) and circled instead of closing.
        // Beyond 2.5x the attack range it walks in; and when the target is facing it, it is the target's focus, so
        // going around is pointless: it fights (offense / defense decide).
        const auto &dm = Config::GetInstance().GetDecisionMatrix();
        const float reach = EffectiveAttackRange(a_state, a_state.weaponReach > 0.0f ? a_state.weaponReach : 150.0f,
                                                 dm.offenseReachMultiplier, dm.offenseReachSafetyMargin);
        if (targetToSelfDist > reach * dm.flankingMaxReach || TargetAimedAtUs(a_actor, a_state)) {
            return result;
        }

        // Calculate outnumbering bonus (more allies = higher priority)
        float outnumberBonus = 0.0f;
        if (a_state.combatContext.allyCount > a_state.combatContext.enemyCount) {
            // Outnumbering: add bonus based on ratio
            float ratio = static_cast<float>(a_state.combatContext.allyCount) /
                          (static_cast<float>(a_state.combatContext.enemyCount) + 1.0f);
            outnumberBonus = (ratio - 1.0f) * 0.2f;                           // 0.2 per extra ally
            outnumberBonus = (outnumberBonus > 0.6f) ? 0.6f : outnumberBonus; // Cap at 0.6 bonus
        }

        // More lenient conditions for flanking:
        // 1. Target is engaged with ally OR we significantly outnumber them
        // 2. We're within reasonable range (increased from 1000 to 1500)
        // 3. We're not already perfectly flanking (but allow if we want to
        // maintain/improve)
        // 4. Target is vulnerable (casting/drawing) - excellent flanking opportunity
        // 5. Multiple allies engaged with target - better flanking opportunity

        bool shouldFlank = false;
        float basePriority = Config::GetInstance().GetScoringWeights().flankingBase;
        float situationBonus = 0.0f;

        // Use enhanced combat context: Target facing relative to allies
        // If target is facing away from ally, we can flank from behind (excellent
        // opportunity)
        if (a_state.combatContext.targetFacingAwayFromAlly) {
            basePriority += 0.3f;   // Target facing away from ally - perfect flanking opportunity
            situationBonus += 0.2f; // Significant bonus for coordinated flanking
        } else if (a_state.combatContext.targetFacingTowardAlly) {
            basePriority += 0.2f; // Target facing toward ally - we can flank from behind
        }

        // Use enhanced combat context: Range granularity
        // Flanking is more effective when in optimal range
        if (a_state.combatContext.isInOptimalRange) {
            basePriority += 0.15f; // In optimal range - better flanking position
        } else if (a_state.combatContext.rangeCategory == RangeCategory::OutOfRange) {
            basePriority -= 0.2f; // Out of range - flanking less effective
        }

        // Check if target is vulnerable (casting/drawing) - excellent flanking
        // opportunity
        bool targetIsVulnerable = a_state.target.isCasting || a_state.target.isDrawingBow ||
                                  a_state.target.knockState != RE::KNOCK_STATE_ENUM::kNormal;

        // Check if multiple allies are engaged (better flanking opportunity)
        bool multipleAlliesEngaged = (a_state.combatContext.allyCount >= 2);


        if (targetEngagedWithAlly && targetToSelfDist < 1500.0f) {
            // If target is engaged with ally, flanking is always useful
            shouldFlank = true;
            basePriority = 1.5f; // Higher base priority when target is engaged

            // Bonus for multiple allies engaged
            if (multipleAlliesEngaged) {
                situationBonus += 0.3f; // Multiple allies = better flanking opportunity
            }

            // Bonus if target is vulnerable
            if (targetIsVulnerable) {
                situationBonus += 0.4f; // Target vulnerable = perfect flanking opportunity
            }
        } else if (a_state.combatContext.allyCount >= 2 && targetToSelfDist < 1500.0f) {
            // If we have multiple allies, flanking is tactically sound even if target
            // isn't directly engaged
            shouldFlank = true;
            basePriority = 1.4f;

            // Bonus if target is vulnerable
            if (targetIsVulnerable) {
                situationBonus += 0.3f; // Target vulnerable = good flanking opportunity
            }
        }


        // If target is vulnerable and we have allies, flanking is excellent
        if (targetIsVulnerable && a_state.combatContext.allyCount >= 1 && targetToSelfDist < 1500.0f && !shouldFlank) {
            shouldFlank = true;
            basePriority = 1.5f;    // High priority when target is vulnerable
            situationBonus += 0.3f; // Vulnerable target = excellent opportunity
        }


        // Follower pincer: the player is holding the target's attention, go around it
        if (shouldFlank && targetEngagedWithAlly && a_state.combatContext.allyIsPlayer) {
            situationBonus += Config::GetInstance().GetTeamwork().followerPincerBonus;
        }

        if (shouldFlank) {
            result.action = ActionType::Flanking;
            result.priority = basePriority + outnumberBonus + situationBonus; // Add all bonuses
            // In or near reach a real attack outranks it (attacks with an opening score ~1.5-3; run 42 flanked at 1.65
            // in reach, run 43 at up to 2.05 just outside it); flanking then only wins when the attack has nothing
            if (a_state.combatContext.isInAttackRange || a_state.target.distance <= reach * 1.5f) {
                result.priority = (std::min)(result.priority, 1.4f);
            }

            // Calculate optimal flanking direction (perpendicular to target, away from
            // ally) This creates a pincer movement
            RE::NiPoint3 flankingDir = CalculateFlankingDirection(a_state);
            result.direction = flankingDir;

            // Adjust intensity based on distance, outnumbering, and situation
            float intensity = 1.0f;
            if (targetToSelfDist > 800.0f) {
                intensity = 0.8f; // Less intensity when further away
            }
            if (outnumberBonus > 0.3f) {
                intensity = (intensity + 0.1f > 1.0f)
                                ? 1.0f
                                : (intensity + 0.1f); // More intensity when significantly outnumbering
            }
            if (targetIsVulnerable) {
                intensity =
                    (intensity + 0.1f > 1.0f) ? 1.0f : (intensity + 0.1f); // More intensity when target is vulnerable
            }
            result.intensity = intensity;
        }

        return result;
    }

    DecisionResult DecisionMatrix::EvaluateFeinting(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        DecisionResult result;

        // Feinting conditions:
        // 1. Target is blocking or being defensive
        // 2. We're in melee range or approaching it
        // 3. Target is not currently attacking (safe to feint)
        // 4. We have stamina to perform feint + follow-up

        if (!a_state.target.isValid) {
            return result;
        }

        // Don't feint if target is actively attacking (too risky)
        if (a_state.target.isAttacking || a_state.target.isPowerAttacking) {
            return result;
        }

        // A feint is a bash, and our Block tree never bashes with a bow or crossbow (run 21: 0 of 23 taken)
        if (a_state.self.isRanged || !BlockRules::CanRunBlockTree(a_actor) || Intents::DefendUnavailable(a_actor) ||
            Intents::WantsDualBlock(a_actor) ||
            a_state.self.currentStamina < BashStaminaCost()) {
            return result;
        }

        // Need sufficient stamina for feint + potential follow-up (reduced threshold)
        if (a_state.self.staminaPercent < 0.25f) {
            return result;
        }

        // Feinting is most effective when target is blocking (perfect counter)
        bool targetIsBlocking = a_state.target.isBlocking;
        // Only an actual guard: "facing us and not attacking" counted too, so NPCs feinted (a bash) at a target that
        // was just standing there
        bool targetIsDefensive = targetIsBlocking;

        // A feint is a bash, so only inside bash reach: at weapon reach x1.5 (~170) it fired at 143-164 units, where
        // the bash never starts and the feint was a bare sidestep (SE run, 2026-10-26)
        float reachDistance = BashReach() * a_actor->GetScale() + a_state.target.boundRadius;
        bool inFeintRange = (a_state.target.distance <= reachDistance);

        // Feinting is effective when:
        // - Target is blocking/defensive
        // - We're in feint range (approaching or in melee)
        // - We're not currently in the middle of an attack (but allow after attack
        // completes)
        // - We just attacked and target blocked (feint to break guard)
        // - Target is defensive but not blocking (feint to bait)

        // Check if we can feint (idle OR just finished attack)
        bool canFeint = (a_state.self.attackState == RE::ATTACK_STATE_ENUM::kNone);
        bool justFinishedAttack = (a_state.self.attackState == RE::ATTACK_STATE_ENUM::kFollowThrough);
        bool readyToFeint = canFeint || justFinishedAttack;

        // Feinting is especially effective after we attacked and target blocked
        // OR when target is defensive and we're ready
        // Use temporal state: Wait for target to commit to blocking before feinting
        bool goodFeintOpportunity = false;
        float blockingDuration = a_state.temporal.target.blockingDuration;
        bool targetFrequentlyBlocks = a_state.temporal.target.recentBlockCommitments >= 1.5f;

        // Weapon type considerations: Feinting is more useful for one-handed weapons
        // Two-handed weapons can break blocks easier, so feinting less needed
        bool feintIsUseful = true;
        if (a_state.self.isTwoHanded) {
            feintIsUseful = false; // Two-handed can break blocks easier, feinting less needed
        }

        if (targetIsBlocking && justFinishedAttack) {
            // We attacked, they blocked - feint to break guard
            // But wait for them to commit to blocking (at least 0.3s) for better timing
            if ((blockingDuration > 0.3f || targetFrequentlyBlocks) && feintIsUseful) {
                goodFeintOpportunity = true;
            }
        } else if (targetIsBlocking && readyToFeint) {
            // Target is blocking - wait for them to commit (at least 0.5s) before
            // feinting
            if ((blockingDuration > 0.5f || targetFrequentlyBlocks) && feintIsUseful) {
                goodFeintOpportunity = true; // Target committed to blocking - feint to break guard
            }
        } else if (targetIsDefensive && readyToFeint && feintIsUseful) {
            goodFeintOpportunity = true; // Target is defensive - feint to bait
        } else if (targetFrequentlyBlocks && readyToFeint && feintIsUseful) {
            goodFeintOpportunity = true; // Repeated recent blocks make a feint a useful bait
        }

        // Personality: feinting is patience; one roll per 2 s, 40% (impatient) to 100% (patient)
        if (goodFeintOpportunity && inFeintRange) {
            const auto bucket = static_cast<std::uint32_t>(
                std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count() / 2.0f);
            goodFeintOpportunity = RollOnce((static_cast<std::uint64_t>(a_actor->GetFormID()) << 32) | (1ull << 59) | bucket,
                                            0.4f + 0.6f * Personality::Effective(a_actor).patience);
        }
        if (goodFeintOpportunity && inFeintRange) {
            result.action = ActionType::Feint;

            // Higher priority when target is blocking (feinting is the counter to
            // blocking)
            float basePriority = 1.3f;
            if (targetIsBlocking) {
                basePriority = 1.6f; // Much higher priority when target is blocking

                // Weapon type considerations: Feinting is more valuable for one-handed
                // weapons
                if (a_state.self.isOneHanded) {
                    basePriority += 0.2f; // One-handed weapons benefit more from feinting
                } else if (a_state.self.isTwoHanded) {
                    basePriority -= 0.3f; // Two-handed can break blocks easier, feinting less needed
                }

                // Target weapon type: Feinting is more effective against one-handed
                // blockers
                if (a_state.target.isOneHanded) {
                    basePriority += 0.1f; // Easier to feint one-handed blockers
                }

                // Use temporal state: Higher priority if target has been blocking for a
                // while (committed to defense)
                float targetBlockingDurationFeint = a_state.temporal.target.blockingDuration;
                if (targetBlockingDurationFeint > 1.0f) {
                    basePriority += 0.3f; // Target committed to blocking for > 1s -
                                          // excellent feint opportunity
                } else if (targetBlockingDurationFeint > 0.5f) {
                    basePriority += 0.15f; // Target committed to blocking for > 0.5s - good
                                           // feint opportunity
                }
                if (targetFrequentlyBlocks) {
                    basePriority += 0.15f; // Repeated recent blocks make another feint more promising
                }

                // Extra bonus if we just attacked and they blocked (perfect feint
                // opportunity)
                if (justFinishedAttack) {
                    basePriority += 0.3f; // We attacked, they blocked - feint to break guard
                }
            } else if (targetFrequentlyBlocks) {
                basePriority = 1.45f; // Bait a target that has committed to blocking repeatedly
            } else if (a_state.target.orientationDot > 0.7f) {
                basePriority = 1.4f; // Higher priority when target is facing us defensively
            }

            // Boost priority if we're close to target (better feint opportunity)
            float distanceRatio = a_state.target.distance / reachDistance;
            float distanceBonus = (1.0f - distanceRatio) * 0.2f; // Up to +0.2f when closer

            // Boost priority if we have good stamina (can follow up effectively)
            float staminaBonus = 0.0f;
            if (a_state.self.staminaPercent > 0.6f) {
                staminaBonus = 0.1f; // Bonus for having stamina to follow up
            }

            // Boost if target is defensive but not blocking (feint to bait defensive
            // response)
            float defensiveBonus = 0.0f;
            if (!targetIsBlocking && targetIsDefensive) {
                defensiveBonus = 0.2f; // Target is defensive - feint to bait
            }

            // Attack defense feedback: If attacks are frequently parried/timed blocked,
            // prioritize feints
            float defenseFeedbackBonus = 0.0f;
            if (a_state.temporal.self.totalDefenseRate > 0.3f) {
                // High defense rate (>30%) - significantly boost feint priority
                defenseFeedbackBonus =
                    0.5f + (a_state.temporal.self.totalDefenseRate - 0.3f) * 1.0f; // Up to +1.2f for 100% defense rate
            } else if (a_state.temporal.self.totalDefenseRate > 0.15f) {
                // Moderate defense rate (15-30%) - moderate boost
                defenseFeedbackBonus = 0.3f;
            } else if (a_state.temporal.self.lastAttackParried || a_state.temporal.self.lastAttackTimedBlocked) {
                // Recent parry/timed block - immediate boost
                defenseFeedbackBonus = 0.4f;
            }

            result.priority = basePriority + distanceBonus + staminaBonus + defensiveBonus + defenseFeedbackBonus;

            // Feint direction: slight forward movement to appear aggressive
            RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
            float toTargetLenSq = toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z;
            if (toTargetLenSq > 0.01f) {
                float toTargetLen = std::sqrt(toTargetLenSq);
                toTarget.x /= toTargetLen;
                toTarget.y /= toTargetLen;
                toTarget.z /= toTargetLen;
            }
            result.direction = toTarget;

            // Higher intensity when target is blocking (more aggressive feint)
            if (targetIsBlocking) {
                result.intensity = 0.8f; // Higher intensity to break guard

                // Even higher intensity if we just attacked and they blocked
                if (justFinishedAttack) {
                    result.intensity = 0.9f; // Very aggressive feint after blocked attack
                }
            } else {
                result.intensity = 0.6f; // Moderate intensity for defensive feint
            }
        }

        return result;
    }

    RE::NiPoint3 DecisionMatrix::CalculateFlankingDirection(const ActorStateData &a_state)
    {
        if (!a_state.target.isValid || !a_state.combatContext.hasNearbyAlly) {
            // Fallback to normal strafe direction
            return CalculateStrafeDirection(a_state);
        }

        // An ally that is known and within ~3000 units, as EvaluateFlanking checks it. This used to test the ally's
        // world position (|pos|^2 > 1e7), which outdoors always failed, so exteriors never got the pincer side.
        RE::NiPoint3 allyPos = a_state.combatContext.closestAllyPosition;
        const float allyDist = a_state.combatContext.closestAllyDistance;
        if (allyDist <= 0.0f || allyDist > 3000.0f) {
            // Fallback to normal strafe direction
            return CalculateStrafeDirection(a_state);
        }

        // Calculate direction perpendicular to target's forward vector
        // Prefer direction away from ally to create pincer movement

        // Get target's right vector (perpendicular to forward)
        RE::NiPoint3 targetRight = RE::NiPoint3(-a_state.target.forwardVector.y, a_state.target.forwardVector.x, 0.0f);
        float targetRightLenSq = targetRight.x * targetRight.x + targetRight.y * targetRight.y;
        if (targetRightLenSq < 0.01f) {
            // Fallback to normal strafe direction
            return CalculateStrafeDirection(a_state);
        }
        float targetRightLen = std::sqrt(targetRightLenSq);
        targetRight.x /= targetRightLen;
        targetRight.y /= targetRightLen;

        // Calculate direction from target to ally
        RE::NiPoint3 targetToAlly = allyPos - a_state.target.position;
        float targetToAllyLenSq =
            targetToAlly.x * targetToAlly.x + targetToAlly.y * targetToAlly.y + targetToAlly.z * targetToAlly.z;
        if (targetToAllyLenSq < 0.01f) {
            // Fallback to normal strafe direction
            return CalculateStrafeDirection(a_state);
        }
        float targetToAllyLen = std::sqrt(targetToAllyLenSq);
        targetToAlly.x /= targetToAllyLen;
        targetToAlly.y /= targetToAllyLen;
        targetToAlly.z /= targetToAllyLen;

        // Choose flanking direction opposite to ally (create pincer)
        // Dot product tells us which side ally is on
        float allySideDot = targetRight.Dot(targetToAlly);

        // If ally is on right side (positive dot), we go left (negative)
        // If ally is on left side (negative dot), we go right (positive)
        RE::NiPoint3 flankingDir = (allySideDot > 0.0f) ? -targetRight : targetRight;

        // Also add slight forward component to maintain pressure
        RE::NiPoint3 toTarget = a_state.target.position - a_state.self.position;
        float toTargetLenSq = toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z;
        if (toTargetLenSq < 0.01f) {
            // Too close, just return flanking direction
            return flankingDir;
        }
        float toTargetLen = std::sqrt(toTargetLenSq);
        toTarget.x /= toTargetLen;
        toTarget.y /= toTargetLen;
        toTarget.z /= toTargetLen;

        flankingDir = flankingDir * 0.7f + toTarget * 0.3f;
        float flankingLenSq =
            flankingDir.x * flankingDir.x + flankingDir.y * flankingDir.y + flankingDir.z * flankingDir.z;
        if (flankingLenSq < 0.01f) {
            // Fallback to normal strafe direction
            return CalculateStrafeDirection(a_state);
        }
        float flankingLen = std::sqrt(flankingLenSq);
        flankingDir.x /= flankingLen;
        flankingDir.y /= flankingLen;
        flankingDir.z /= flankingLen;

        return flankingDir;
    }

    void DecisionMatrix::LogActorState(RE::Actor *a_actor, const ActorStateData &a_state)
    {
        auto &config = Config::GetInstance();
        if (!config.GetGeneral().enableDebugLog) {
            return;
        }

        auto selectedRef = RE::Console::GetSelectedRef();
        if (!selectedRef || a_actor != selectedRef.get()) {
            return;
        }

        // Get actor FormID for logging
        auto formIDOpt = ActorUtils::SafeGetFormID(a_actor);
        std::uint32_t formID = formIDOpt.has_value() ? static_cast<std::uint32_t>(formIDOpt.value()) : 0;

        LOG_DEBUG("--- ActorState FormID=0x{:08X} ---", formID);

        // Self State
        LOG_DEBUG("Self: Health={:.1f}% Stamina={:.1f}% AttackState={} Blocking={} "
                  "Sprinting={} Walking={} Idle={}",
                  a_state.self.healthPercent * 100.0f, a_state.self.staminaPercent * 100.0f,
                  static_cast<int>(a_state.self.attackState), a_state.self.isBlocking, a_state.self.isSprinting,
                  a_state.self.isWalking, a_state.self.isIdle);
        LOG_DEBUG("Self: WeaponType={} 1H={} 2H={} Ranged={} Melee={}", static_cast<int>(a_state.self.weaponType),
                  a_state.self.isOneHanded, a_state.self.isTwoHanded, a_state.self.isRanged, a_state.self.isMelee);
        LOG_DEBUG("Self: Pos=({:.1f},{:.1f},{:.1f}) Fwd=({:.2f},{:.2f},{:.2f})", a_state.self.position.x,
                  a_state.self.position.y, a_state.self.position.z, a_state.self.forwardVector.x,
                  a_state.self.forwardVector.y, a_state.self.forwardVector.z);

        // Target State
        if (a_state.target.isValid) {
            std::uint32_t targetWeaponFormID =
                a_state.target.equippedRightHand ? a_state.target.equippedRightHand->GetFormID() : 0;
            LOG_DEBUG("Target: Valid=true Player={} Detected={} DetectionLevel={} Dist={:.1f} Health={:.1f}% "
                      "Stamina={:.1f}%",
                      a_state.target.isPlayer, a_state.target.isDetected, a_state.target.detectionLevel,
                      a_state.target.distance, a_state.target.healthPercent * 100.0f,
                      a_state.target.staminaPercent * 100.0f);
            LOG_DEBUG("Target: Atk={} PAtk={} Blk={} Cast={} Draw={} Flee={} "
                      "InAtkRecov={} Knock={}",
                      a_state.target.isAttacking, a_state.target.isPowerAttacking, a_state.target.isBlocking,
                      a_state.target.isCasting, a_state.target.isDrawingBow, a_state.target.isFleeing,
                      a_state.target.isInAttackRecovery, static_cast<int>(a_state.target.knockState));
            LOG_DEBUG("Target: Sprint={} Walk={}", a_state.target.isSprinting, a_state.target.isWalking);
            LOG_DEBUG("Target: OrientDot={:.2f} WeaponType={} WeaponFormID=0x{:08X}", a_state.target.orientationDot,
                      static_cast<int>(a_state.target.weaponType), targetWeaponFormID);
            LOG_DEBUG("Target: 1H={} 2H={} Ranged={} Melee={}", a_state.target.isOneHanded, a_state.target.isTwoHanded,
                      a_state.target.isRanged, a_state.target.isMelee);
            LOG_DEBUG("Target: Pos=({:.1f},{:.1f},{:.1f}) Fwd=({:.2f},{:.2f},{:.2f})", a_state.target.position.x,
                      a_state.target.position.y, a_state.target.position.z, a_state.target.forwardVector.x,
                      a_state.target.forwardVector.y, a_state.target.forwardVector.z);
        } else {
            LOG_DEBUG("Target: Valid=false");
        }

        // Combat Context
        LOG_DEBUG("Combat: Enemies={} Allies={} ThreatLvl={} TargetingUs={}", a_state.combatContext.enemyCount,
                  a_state.combatContext.allyCount, static_cast<int>(a_state.combatContext.threatLevel),
                  a_state.combatContext.enemiesTargetingUs);
        LOG_DEBUG("Combat: ClosestEnemyDist={:.1f} Ally: HasNearby={} "
                  "ClosestDist={:.1f} ClosestPos=({:.1f},{:.1f},{:.1f})",
                  a_state.combatContext.closestEnemyDistance, a_state.combatContext.hasNearbyAlly,
                  a_state.combatContext.closestAllyDistance, a_state.combatContext.closestAllyPosition.x,
                  a_state.combatContext.closestAllyPosition.y, a_state.combatContext.closestAllyPosition.z);
        LOG_DEBUG("Combat: Ally: TargetFacingDot={:.2f} TargetFacingAway={} "
                  "TargetFacingToward={}",
                  a_state.combatContext.targetFacingAllyDot, a_state.combatContext.targetFacingAwayFromAlly,
                  a_state.combatContext.targetFacingTowardAlly);
        LOG_DEBUG("Combat: RangeCat={} InAtkRange={} InOptRange={} InCloseRange={}",
                  static_cast<int>(a_state.combatContext.rangeCategory), a_state.combatContext.isInAttackRange,
                  a_state.combatContext.isInOptimalRange, a_state.combatContext.isInCloseRange);

        // Temporal State - Self
        LOG_DEBUG("TemporalSelf: LastAtk={:.2f}s LastPAtk={:.2f}s LastSAtk={:.2f}s "
                  "LastDodge={:.2f}s LastBash={:.2f}s LastFeint={:.2f}s LastAction={:.2f}s",
                  a_state.temporal.self.timeSinceLastAttack, a_state.temporal.self.timeSinceLastPowerAttack,
                  a_state.temporal.self.timeSinceLastSprintAttack, a_state.temporal.self.timeSinceLastDodge,
                  a_state.temporal.self.timeSinceLastBash, a_state.temporal.self.timeSinceLastFeint,
                  a_state.temporal.self.timeSinceLastAction);
        LOG_DEBUG("Self: ArmorClass={} Combat: AllyIsPlayer={} WeakestEnemyHp={:.0f}% WeakestEnemyDist={:.0f}",
                  static_cast<int>(a_state.self.armorClass), a_state.combatContext.allyIsPlayer,
                  a_state.combatContext.weakestEnemyHealth * 100.0f, a_state.combatContext.weakestEnemyDistance);
        LOG_DEBUG("TemporalSelf: BlockDur={:.2f}s AtkDur={:.2f}s IdleDur={:.2f}s",
                  a_state.temporal.self.blockingDuration, a_state.temporal.self.attackingDuration,
                  a_state.temporal.self.idleDuration);

        // Parry Feedback
        LOG_DEBUG("ParryFeedback: LastSuccess={} Attempts={} Successes={} "
                  "TimeSinceLastAttempt={:.2f}s EstDur={:.2f}s",
                  a_state.temporal.self.lastParrySuccess, a_state.temporal.self.parryAttemptCount,
                  a_state.temporal.self.parrySuccessCount, a_state.temporal.self.timeSinceLastParryAttempt,
                  a_state.temporal.self.lastParryEstimatedDuration);

        // Timed Block Feedback
        LOG_DEBUG("TimedBlockFeedback: LastSuccess={} Attempts={} Successes={} "
                  "TimeSinceLastAttempt={:.2f}s EstDur={:.2f}s",
                  a_state.temporal.self.lastTimedBlockSuccess, a_state.temporal.self.timedBlockAttemptCount,
                  a_state.temporal.self.timedBlockSuccessCount, a_state.temporal.self.timeSinceLastTimedBlockAttempt,
                  a_state.temporal.self.lastTimedBlockEstimatedDuration);

        // Attack Defense Feedback (when NPC's attacks are parried/timed
        // blocked/hit/missed)
        LOG_DEBUG("AtkDefFeedback: LastParried={} LastTBlocked={} LastHit={} LastMissed={} "
                  "TotalAtks={} Parried={} TBlocked={} Hit={} Missed={}",
                  a_state.temporal.self.lastAttackParried, a_state.temporal.self.lastAttackTimedBlocked,
                  a_state.temporal.self.lastAttackHit, a_state.temporal.self.lastAttackMissed,
                  a_state.temporal.self.totalAttackCount, a_state.temporal.self.parriedAttackCount,
                  a_state.temporal.self.timedBlockedAttackCount, a_state.temporal.self.hitAttackCount,
                  a_state.temporal.self.missedAttackCount);
        LOG_DEBUG("AtkDefFeedback: ParryRate={:.1f}% TBlockRate={:.1f}% "
                  "HitRate={:.1f}% MissRate={:.1f}% TotalDefRate={:.1f}%",
                  a_state.temporal.self.parryRate * 100.0f, a_state.temporal.self.timedBlockRate * 100.0f,
                  a_state.temporal.self.hitRate * 100.0f, a_state.temporal.self.missRate * 100.0f,
                  a_state.temporal.self.totalDefenseRate * 100.0f);
        LOG_DEBUG("Momentum: RecentDamageFrac={:.3f} HasData={}", a_state.temporal.self.recentDamageFraction,
                  a_state.temporal.self.hasMomentumData);
        const DamageRace race = Race(a_state);
        LOG_DEBUG("Race: OurHit={:.3f} TheirHit={:.3f} ToKill={:.1f} ToDie={:.1f} Winning={} Losing={} Retreated={:.1f}s",
                  a_state.temporal.self.ourAverageHit, a_state.temporal.self.theirAverageHit, race.toKill, race.toDie,
                  race.winning, race.losing, a_state.temporal.self.retreatTime);
        LOG_DEBUG("AtkDefFeedback: TimeSince: LastParriedAtk={:.2f}s "
                  "LastTBlockedAtk={:.2f}s LastHitAtk={:.2f}s LastMissedAtk={:.2f}s",
                  a_state.temporal.self.timeSinceLastParriedAttack,
                  a_state.temporal.self.timeSinceLastTimedBlockedAttack, a_state.temporal.self.timeSinceLastHitAttack,
                  a_state.temporal.self.timeSinceLastMissedAttack);

        // Temporal State - Target
        if (a_state.target.isValid) {
            LOG_DEBUG("TemporalTarget: LastAtk={:.2f}s LastPAtk={:.2f}s RecentBlockCommits={:.2f}",
                      a_state.temporal.target.timeSinceLastAttack, a_state.temporal.target.timeSinceLastPowerAttack,
                      a_state.temporal.target.recentBlockCommitments);
            LOG_DEBUG("TemporalTarget: AtkDur={:.2f}s BlockDur={:.2f}s CastDur={:.2f}s "
                      "DrawDur={:.2f}s IdleDur={:.2f}s",
                      a_state.temporal.target.attackingDuration, a_state.temporal.target.blockingDuration,
                      a_state.temporal.target.castingDuration, a_state.temporal.target.drawingDuration,
                      a_state.temporal.target.idleDuration);

            if (a_state.temporal.target.timeUntilAttackHits < 999.0f) {
                LOG_DEBUG("TemporalTarget: TimeUntilAtkHits={:.2f}s EstAtkDur={:.2f}s "
                          "AtkStartTime={:.2f}s",
                          a_state.temporal.target.timeUntilAttackHits, a_state.temporal.target.estimatedAttackDuration,
                          a_state.temporal.target.attackStartTime);
            } else {
                LOG_DEBUG("TemporalTarget: TimeUntilAtkHits=N/A EstAtkDur={:.2f}s "
                          "AtkStartTime={:.2f}s",
                          a_state.temporal.target.estimatedAttackDuration, a_state.temporal.target.attackStartTime);
            }
        }

        // Misc
        LOG_DEBUG("Misc: WeaponReach={:.1f} TargetBoundRadius={:.1f} SelfBoundRadius={:.1f} DeltaTime={:.4f}s",
                  a_state.weaponReach, a_state.target.boundRadius, a_state.self.boundRadius, a_state.deltaTime);
    }
} // namespace CombatAI

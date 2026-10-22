#include "Config.h"
#include "Logger.h"
#include "pch.h"
#include <filesystem>
#include <set>
#include <vector>

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
    bool Config::Load(const std::string &a_filePath)
    {
        CSimpleIniA ini;
        ini.SetUnicode();

        // Try to load the INI file
        SI_Error rc = ini.LoadFile(a_filePath.c_str());
        if (rc < 0) {
            LOG_WARN("Failed to load config file: {}. Using defaults.", a_filePath);
            return false;
        }

        LOG_INFO("Loading configuration from: {}", a_filePath);

        // Read all settings sections
        ReadGeneralSettings(ini);
        ReadHumanizerSettings(ini);
        ReadDodgeSystemSettings(ini);
        ReadDecisionMatrixSettings(ini);
        ReadScoringWeightsSettings(ini);
        ReadPerformanceSettings(ini);
        ReadModIntegrationSettings(ini);
        ReadParrySettings(ini);
        ReadTimedBlockSettings(ini);
        ReadCombatPacingSettings(ini);
        ReadDamageMomentumSettings(ini);
        ReadEnvSensorSettings(ini);
        ReadTargetSwitchingSettings(ini);
        ReadArmorStyleSettings(ini);
        ReadTeamworkSettings(ini);
        ReadVanillaAttackSettings(ini);
        ReadProjectileAwarenessSettings(ini);

        LOG_INFO("Configuration loaded successfully");
        LOG_INFO("Jump evasion: {} (chance {:.2f}, aimed bow/crossbow at {:.0f}-{:.0f} units, height {:.0f}, stamina {:.0f}); skill at level 1 {:.2f}",
                 m_decisionMatrix.enableJumpEvasion ? "on" : "OFF", m_decisionMatrix.evasionJumpChance,
                 m_decisionMatrix.jumpEvasionDistanceMin, m_decisionMatrix.jumpEvasionDistanceMax,
                 m_decisionMatrix.jumpHeight, m_decisionMatrix.jumpStaminaCost, m_humanizer.skillBase);
        LOG_INFO("Stamina: retreat/backoff {}; power attacks, sprint attacks and power bashes need {:.0f}% stamina",
                 m_decisionMatrix.staminaAffectsRetreat ? "also on low stamina" : "on health only",
                 m_decisionMatrix.powerAttackMinStamina * 100.0f);
        return true;
    }

    bool Config::Save(const std::string &a_filePath)
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        // Load the existing file first so comments and unknown keys are preserved.
        ini.LoadFile(a_filePath.c_str());

        // Every key written is recorded; keys the file still has but this version no longer
        // uses (renamed/removed settings) are pruned before saving.
        std::set<std::pair<std::string, std::string>> written;
        auto B = [&](const char *sec, const char *key, bool val) {
            ini.SetBoolValue(sec, key, val);
            written.emplace(sec, key);
        };
        auto D = [&](const char *sec, const char *key, float val) {
            ini.SetDoubleValue(sec, key, static_cast<double>(val));
            written.emplace(sec, key);
        };
        auto L = [&](const char *sec, const char *key, long val) {
            ini.SetLongValue(sec, key, val);
            written.emplace(sec, key);
        };
        auto S = [&](const char *sec, const char *key, const std::string &val) {
            ini.SetValue(sec, key, val.c_str());
            written.emplace(sec, key);
        };

        // General
        B("General", "EnablePlugin", m_general.enablePlugin);
        B("General", "EnableDebugLog", m_general.enableDebugLog);
        D("General", "ProcessingInterval", m_general.processingInterval);

        // Humanizer
        D("Humanizer", "BaseReactionDelayMs", m_humanizer.baseReactionDelayMs);
        D("Humanizer", "ReactionVarianceMs", m_humanizer.reactionVarianceMs);
        D("Humanizer", "ReactionDelayReductionPerLevelMs", m_humanizer.reactionDelayReductionPerLevelMs);
        D("Humanizer", "MinReactionDelayMs", m_humanizer.minReactionDelayMs);
        B("Humanizer", "EnableSkillRolls", m_humanizer.enableSkillRolls);
        D("Humanizer", "SkillBase", m_humanizer.skillBase);
        D("Humanizer", "SkillPerLevel", m_humanizer.skillPerLevel);
        D("Humanizer", "SkillMin", m_humanizer.skillMin);
        D("Humanizer", "SkillMax", m_humanizer.skillMax);
        D("Humanizer", "SkillRerollSeconds", m_humanizer.skillRerollSeconds);
        B("Humanizer", "EnableFatigue", m_humanizer.enableFatigue);
        D("Humanizer", "FatigueOnsetSeconds", m_humanizer.fatigueOnsetSeconds);
        D("Humanizer", "FatigueFullSeconds", m_humanizer.fatigueFullSeconds);
        D("Humanizer", "FatigueStaminaThreshold", m_humanizer.fatigueStaminaThreshold);
        D("Humanizer", "FatigueSkillPenalty", m_humanizer.fatigueSkillPenalty);
        B("Humanizer", "EnablePanic", m_humanizer.enablePanic);
        D("Humanizer", "PanicHealthThreshold", m_humanizer.panicHealthThreshold);
        D("Humanizer", "PanicSkillPenalty", m_humanizer.panicSkillPenalty);

        // Dodge system
        D("DodgeSystem", "DodgeStaminaCost", m_dodgeSystem.dodgeStaminaCost);
        D("DodgeSystem", "IFrameDuration", m_dodgeSystem.iFrameDuration);
        B("DodgeSystem", "EnableStepDodge", m_dodgeSystem.enableStepDodge);
        B("DodgeSystem", "EnableDodgeAttackCancel", m_dodgeSystem.enableDodgeAttackCancel);
        D("DodgeSystem", "ForwardDodgeMaxGap", m_dodgeSystem.forwardDodgeMaxGap);
        D("DodgeSystem", "ForwardDodgeCounterWindowSeconds", m_dodgeSystem.forwardDodgeCounterWindowSeconds);
        S("DodgeSystem", "DodgeEventForward", m_dodgeSystem.eventForward);
        S("DodgeSystem", "DodgeEventBack", m_dodgeSystem.eventBack);
        S("DodgeSystem", "DodgeEventLeft", m_dodgeSystem.eventLeft);
        S("DodgeSystem", "DodgeEventRight", m_dodgeSystem.eventRight);
        S("DodgeSystem", "DodgeEventFallback", m_dodgeSystem.eventFallback);
        D("DodgeSystem", "MinDodgeStamina", m_dodgeSystem.minDodgeStamina);

        // Decision matrix
        D("DecisionMatrix", "InterruptMaxDistance", m_decisionMatrix.interruptMaxDistance);
        D("DecisionMatrix", "InterruptReachMultiplier", m_decisionMatrix.interruptReachMultiplier);
        B("DecisionMatrix", "EnableEvasionDodge", m_decisionMatrix.enableEvasionDodge);
        D("DecisionMatrix", "EvasionMinDistance", m_decisionMatrix.evasionMinDistance);
        B("DecisionMatrix", "EnableJumpEvasion", m_decisionMatrix.enableJumpEvasion);
        D("DecisionMatrix", "JumpEvasionDistanceMin", m_decisionMatrix.jumpEvasionDistanceMin);
        D("DecisionMatrix", "JumpEvasionDistanceMax", m_decisionMatrix.jumpEvasionDistanceMax);
        D("DecisionMatrix", "EvasionJumpChance", m_decisionMatrix.evasionJumpChance);
        D("DecisionMatrix", "JumpHeight", m_decisionMatrix.jumpHeight);
        D("DecisionMatrix", "JumpStaminaCost", m_decisionMatrix.jumpStaminaCost);
        D("DecisionMatrix", "StaminaThreshold", m_decisionMatrix.staminaThreshold);
        B("DecisionMatrix", "StaminaAffectsRetreat", m_decisionMatrix.staminaAffectsRetreat);
        B("DecisionMatrix", "BrainBackoffFallback", m_decisionMatrix.brainBackoffFallback);
        D("DecisionMatrix", "BackoffStepDistance", m_decisionMatrix.backoffStepDistance);
        D("DecisionMatrix", "PowerAttackMinStamina", m_decisionMatrix.powerAttackMinStamina);
        D("DecisionMatrix", "HealthThreshold", m_decisionMatrix.healthThreshold);
        B("DecisionMatrix", "EnableSurvivalRetreat", m_decisionMatrix.enableSurvivalRetreat);
        B("DecisionMatrix", "EnableOffense", m_decisionMatrix.enableOffense);
        D("DecisionMatrix", "OffenseReachMultiplier", m_decisionMatrix.offenseReachMultiplier);
        D("DecisionMatrix", "OffenseReachSafetyMargin", m_decisionMatrix.offenseReachSafetyMargin);
        B("DecisionMatrix", "EnableSprintAttack", m_decisionMatrix.enableSprintAttack);
        B("DecisionMatrix", "EnableSprintCharge", m_decisionMatrix.enableSprintCharge);
        D("DecisionMatrix", "SprintAttackLaunchDistance", m_decisionMatrix.sprintAttackLaunchDistance);
        D("DecisionMatrix", "SprintAttackMinDistance", m_decisionMatrix.sprintAttackMinDistance);
        D("DecisionMatrix", "SprintAttackMaxDistance", m_decisionMatrix.sprintAttackMaxDistance);
        D("DecisionMatrix", "AttackStaminaCost", m_decisionMatrix.attackStaminaCost);
        D("DecisionMatrix", "PowerAttackStaminaCost", m_decisionMatrix.powerAttackStaminaCost);
        D("DecisionMatrix", "SprintAttackStaminaCost", m_decisionMatrix.sprintAttackStaminaCost);
        B("DecisionMatrix", "EnablePowerAttackStaminaCheck", m_decisionMatrix.enablePowerAttackStaminaCheck);
        B("DecisionMatrix", "EnableSprintAttackStaminaCheck", m_decisionMatrix.enableSprintAttackStaminaCheck);

        // Scoring weights
        const auto &w = m_scoringWeights;
        D("ScoringWeights", "InterruptPowerAttackBase", w.interruptPowerAttackBase);
        D("ScoringWeights", "EvasionDodgeBase", w.evasionDodgeBase);
        D("ScoringWeights", "AdvancingBase", w.advancingBase);
        D("ScoringWeights", "SprintAttackBase", w.sprintAttackBase);
        D("ScoringWeights", "AttackBase", w.attackBase);
        D("ScoringWeights", "BackoffBase", w.backoffBase);
        D("ScoringWeights", "FlankingBase", w.flankingBase);
        D("ScoringWeights", "TargetStaggeredBonus", w.targetStaggeredBonus);
        D("ScoringWeights", "TargetCastingBonus", w.targetCastingBonus);
        D("ScoringWeights", "TargetRecoveryBonus", w.targetRecoveryBonus);
        D("ScoringWeights", "TargetFleeingBonus", w.targetFleeingBonus);
        D("ScoringWeights", "TargetLowHealthFinisherBonus", w.targetLowHealthFinisherBonus);
        D("ScoringWeights", "OpeningRiskPenalty", w.openingRiskPenalty);
        D("ScoringWeights", "FlankingAttackBonus", w.flankingAttackBonus);
        D("ScoringWeights", "AllyCoverBonus", w.allyCoverBonus);

        // Mod integrations
        B("ModIntegrations", "EnableCPRIntegration", m_modIntegrations.enableCPRIntegration);
        B("ModIntegrations", "EnablePrecisionIntegration", m_modIntegrations.enablePrecisionIntegration);
        B("ModIntegrations", "EnableTKDodgeIntegration", m_modIntegrations.enableTKDodgeIntegration);
        B("ModIntegrations", "EnableSCARIntegration", m_modIntegrations.enableSCARIntegration);
        D("ModIntegrations", "SCARComboWindowSeconds", m_modIntegrations.scarComboWindowSeconds);
        D("ModIntegrations", "SCARStallSeconds", m_modIntegrations.scarStallSeconds);

        // Performance
        D("Performance", "CleanupInterval", m_performance.cleanupInterval);
        L("Performance", "MaxActorsPerFrame", static_cast<long>(m_performance.maxActorsPerFrame));
        D("Performance", "DistanceNear", m_performance.distanceNear);
        D("Performance", "DistanceMid", m_performance.distanceMid);
        D("Performance", "ProcessingIntervalMid", m_performance.processingIntervalMid);
        D("Performance", "ProcessingIntervalFar", m_performance.processingIntervalFar);

        // Parry
        B("Parry", "EnableParry", m_parry.enableParry);
        D("Parry", "ParryWindowStart", m_parry.parryWindowStart);
        D("Parry", "ParryWindowEnd", m_parry.parryWindowEnd);
        D("Parry", "ParryMinDistance", m_parry.parryMinDistance);
        D("Parry", "ParryMaxDistance", m_parry.parryMaxDistance);
        D("Parry", "ParryBasePriority", m_parry.parryBasePriority);
        D("Parry", "TimingBonusMax", m_parry.timingBonusMax);
        D("Parry", "EarlyBashPenalty", m_parry.earlyBashPenalty);
        D("Parry", "LateBashPenalty", m_parry.lateBashPenalty);

        // Timed block
        B("TimedBlock", "EnableTimedBlock", m_timedBlock.enableTimedBlock);
        D("TimedBlock", "TimedBlockWindowStart", m_timedBlock.timedBlockWindowStart);
        D("TimedBlock", "TimedBlockWindowEnd", m_timedBlock.timedBlockWindowEnd);
        D("TimedBlock", "TimedBlockMinDistance", m_timedBlock.timedBlockMinDistance);
        D("TimedBlock", "TimedBlockMaxDistance", m_timedBlock.timedBlockMaxDistance);
        D("TimedBlock", "TimedBlockBasePriority", m_timedBlock.timedBlockBasePriority);
        D("TimedBlock", "TimedBlockTimingBonusMax", m_timedBlock.timedBlockTimingBonusMax);
        D("TimedBlock", "TimedBlockEarlyPenalty", m_timedBlock.timedBlockEarlyPenalty);
        D("TimedBlock", "TimedBlockLatePenalty", m_timedBlock.timedBlockLatePenalty);

        // Combat pacing (Wait Your Turn)
        B("CombatPacing", "EnableCombatPacing", m_combatPacing.enableCombatPacing);
        L("CombatPacing", "MaxSimultaneousAttackers", static_cast<long>(m_combatPacing.maxSimultaneousAttackers));
        D("CombatPacing", "SlotWindowMinSeconds", m_combatPacing.slotWindowMinSeconds);
        D("CombatPacing", "SlotWindowMaxSeconds", m_combatPacing.slotWindowMaxSeconds);
        B("CombatPacing", "PaceTargetPlayerOnly", m_combatPacing.paceTargetPlayerOnly);
        D("CombatPacing", "EngageMargin", m_combatPacing.engageMargin);
        D("CombatPacing", "WaitDistanceMargin", m_combatPacing.waitDistanceMargin);
        D("CombatPacing", "RetaliateMinSeconds", m_combatPacing.retaliateMinSeconds);
        D("CombatPacing", "RetaliateMaxSeconds", m_combatPacing.retaliateMaxSeconds);

        // Damage momentum
        B("DamageMomentum", "EnableDamageMomentum", m_damageMomentum.enableDamageMomentum);
        D("DamageMomentum", "BigHitThreshold", m_damageMomentum.bigHitThreshold);
        D("DamageMomentum", "ChipThreshold", m_damageMomentum.chipThreshold);
        D("DamageMomentum", "OffensiveBonusMax", m_damageMomentum.offensiveBonusMax);
        D("DamageMomentum", "DefensivePenaltyMax", m_damageMomentum.defensivePenaltyMax);
        D("DamageMomentum", "MomentumDecaySeconds", m_damageMomentum.momentumDecaySeconds);
        D("DamageMomentum", "EwmaAlpha", m_damageMomentum.ewmaAlpha);

        B("EnvSensor", "EnableLosGate", m_envSensor.enableLosGate);
        D("EnvSensor", "LosEyeHeight", m_envSensor.losEyeHeight);
        B("EnvSensor", "EnableCliffSafeDodge", m_envSensor.enableCliffSafeDodge);
        D("EnvSensor", "DodgeLedgeProbeDistance", m_envSensor.dodgeLedgeProbeDistance);
        D("EnvSensor", "DodgeMaxDrop", m_envSensor.dodgeMaxDrop);

        B("TargetSwitching", "EnableTargetSwitching", m_targetSwitching.enableTargetSwitching);
        D("TargetSwitching", "SwitchCooldownSeconds", m_targetSwitching.switchCooldownSeconds);
        D("TargetSwitching", "RetaliateWindowSeconds", m_targetSwitching.retaliateWindowSeconds);
        D("TargetSwitching", "RetaliateChance", m_targetSwitching.retaliateChance);
        D("TargetSwitching", "RetaliateMaxDistance", m_targetSwitching.retaliateMaxDistance);
        D("TargetSwitching", "FinisherHealthThreshold", m_targetSwitching.finisherHealthThreshold);
        D("TargetSwitching", "FinisherMaxDistance", m_targetSwitching.finisherMaxDistance);
        D("TargetSwitching", "FinisherChance", m_targetSwitching.finisherChance);

        B("ArmorStyle", "EnableArmorStyle", m_armorStyle.enableArmorStyle);
        D("ArmorStyle", "LightEvasionBonus", m_armorStyle.lightEvasionBonus);
        D("ArmorStyle", "HeavyEvasionPenalty", m_armorStyle.heavyEvasionPenalty);
        D("ArmorStyle", "HeavyGuardBonus", m_armorStyle.heavyGuardBonus);

        B("Teamwork", "EnableFollowerPincer", m_teamwork.enableFollowerPincer);
        D("Teamwork", "FollowerPincerBonus", m_teamwork.followerPincerBonus);

        // Vanilla attacks
        B("VanillaAttacks", "EnableVanillaAttackShaping", m_vanillaAttacks.enable);
        D("VanillaAttacks", "OpeningPowerMult", m_vanillaAttacks.openingPowerMult);
        D("VanillaAttacks", "OpeningNormalMult", m_vanillaAttacks.openingNormalMult);
        D("VanillaAttacks", "BlockedPowerMult", m_vanillaAttacks.blockedPowerMult);
        D("VanillaAttacks", "BlockedNormalMult", m_vanillaAttacks.blockedNormalMult);

        // Projectile awareness
        B("ProjectileAwareness", "EnableProjectileDodge", m_projectileAwareness.enableProjectileDodge);
        D("ProjectileAwareness", "ReactMinSeconds", m_projectileAwareness.reactMinSeconds);
        D("ProjectileAwareness", "ReactMaxSeconds", m_projectileAwareness.reactMaxSeconds);
        D("ProjectileAwareness", "DodgeChance", m_projectileAwareness.dodgeChance);
        D("ProjectileAwareness", "HitMargin", m_projectileAwareness.hitMargin);
        D("ProjectileAwareness", "DodgePriority", m_projectileAwareness.dodgePriority);

        std::vector<std::pair<std::string, std::string>> stale;
        CSimpleIniA::TNamesDepend sections;
        ini.GetAllSections(sections);
        for (const auto &section : sections) {
            CSimpleIniA::TNamesDepend keys;
            ini.GetAllKeys(section.pItem, keys);
            for (const auto &key : keys) {
                if (!written.contains({section.pItem, key.pItem})) {
                    stale.emplace_back(section.pItem, key.pItem);
                }
            }
        }
        for (const auto &[section, key] : stale) {
            ini.Delete(section.c_str(), key.c_str(), true);
        }
        if (!stale.empty()) {
            LOG_INFO("Removed {} obsolete setting(s) from the INI", stale.size());
        }

        SI_Error rc = ini.SaveFile(a_filePath.c_str());
        if (rc < 0) {
            LOG_WARN("Failed to save config file: {}", a_filePath);
            return false;
        }
        LOG_INFO("Configuration saved to: {}", a_filePath);
        return true;
    }

    void Config::ReadGeneralSettings(CSimpleIniA &a_ini)
    {
        m_general.enablePlugin = a_ini.GetBoolValue("General", "EnablePlugin", m_general.enablePlugin);
        m_general.enableDebugLog = a_ini.GetBoolValue("General", "EnableDebugLog", m_general.enableDebugLog);
        m_general.processingInterval =
            static_cast<float>(a_ini.GetDoubleValue("General", "ProcessingInterval", m_general.processingInterval));

        // Clamp processing interval to reasonable values
        m_general.processingInterval = ClampValue(m_general.processingInterval, 0.01f, 1.0f);
    }

    void Config::ReadHumanizerSettings(CSimpleIniA &a_ini)
    {
        m_humanizer.baseReactionDelayMs = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "BaseReactionDelayMs", m_humanizer.baseReactionDelayMs));
        m_humanizer.reactionDelayReductionPerLevelMs = static_cast<float>(a_ini.GetDoubleValue(
            "Humanizer", "ReactionDelayReductionPerLevelMs", m_humanizer.reactionDelayReductionPerLevelMs));
        m_humanizer.minReactionDelayMs =
            static_cast<float>(a_ini.GetDoubleValue("Humanizer", "MinReactionDelayMs", m_humanizer.minReactionDelayMs));
        m_humanizer.reactionVarianceMs =
            static_cast<float>(a_ini.GetDoubleValue("Humanizer", "ReactionVarianceMs", m_humanizer.reactionVarianceMs));

        auto H = [&](const char *a_key, float &a_value, float a_min, float a_max) {
            a_value =
                ClampValue(static_cast<float>(a_ini.GetDoubleValue("Humanizer", a_key, a_value)), a_min, a_max);
        };
        m_humanizer.enableSkillRolls =
            a_ini.GetBoolValue("Humanizer", "EnableSkillRolls", m_humanizer.enableSkillRolls);
        H("SkillBase", m_humanizer.skillBase, 0.0f, 1.0f);
        H("SkillPerLevel", m_humanizer.skillPerLevel, 0.0f, 0.1f);
        H("SkillMin", m_humanizer.skillMin, 0.0f, 1.0f);
        H("SkillMax", m_humanizer.skillMax, 0.0f, 1.0f);
        H("SkillRerollSeconds", m_humanizer.skillRerollSeconds, 0.1f, 10.0f);
        m_humanizer.skillMax = (std::max)(m_humanizer.skillMax, m_humanizer.skillMin);


        // Clamp values to valid ranges
        m_humanizer.baseReactionDelayMs = (std::max)(0.0f, m_humanizer.baseReactionDelayMs);
        m_humanizer.reactionDelayReductionPerLevelMs = (std::max)(0.0f, m_humanizer.reactionDelayReductionPerLevelMs);
        m_humanizer.minReactionDelayMs = (std::max)(0.0f, m_humanizer.minReactionDelayMs);
        m_humanizer.reactionVarianceMs = (std::max)(0.0f, m_humanizer.reactionVarianceMs);



        m_humanizer.enableFatigue = a_ini.GetBoolValue("Humanizer", "EnableFatigue", m_humanizer.enableFatigue);
        m_humanizer.fatigueOnsetSeconds = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "FatigueOnsetSeconds", m_humanizer.fatigueOnsetSeconds));
        m_humanizer.fatigueFullSeconds = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "FatigueFullSeconds", m_humanizer.fatigueFullSeconds));
        m_humanizer.fatigueStaminaThreshold = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "FatigueStaminaThreshold", m_humanizer.fatigueStaminaThreshold));
        m_humanizer.fatigueSkillPenalty = static_cast<float>(a_ini.GetDoubleValue(
            "Humanizer", "FatigueSkillPenalty",
            a_ini.GetDoubleValue("Humanizer", "FatigueMistakeBonus", m_humanizer.fatigueSkillPenalty)));
        m_humanizer.enablePanic = a_ini.GetBoolValue("Humanizer", "EnablePanic", m_humanizer.enablePanic);
        m_humanizer.panicHealthThreshold = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "PanicHealthThreshold", m_humanizer.panicHealthThreshold));
        m_humanizer.panicSkillPenalty = static_cast<float>(a_ini.GetDoubleValue(
            "Humanizer", "PanicSkillPenalty",
            a_ini.GetDoubleValue("Humanizer", "PanicMistakeBonus", m_humanizer.panicSkillPenalty)));

        m_humanizer.fatigueOnsetSeconds = (std::max)(0.0f, m_humanizer.fatigueOnsetSeconds);
        m_humanizer.fatigueFullSeconds = (std::max)(m_humanizer.fatigueOnsetSeconds + 1.0f, m_humanizer.fatigueFullSeconds);
        m_humanizer.fatigueStaminaThreshold = ClampValue(m_humanizer.fatigueStaminaThreshold, 0.0f, 1.0f);
        m_humanizer.fatigueSkillPenalty = ClampValue(m_humanizer.fatigueSkillPenalty, 0.0f, 3.0f);
        m_humanizer.panicHealthThreshold = ClampValue(m_humanizer.panicHealthThreshold, 0.0f, 1.0f);
        m_humanizer.panicSkillPenalty = ClampValue(m_humanizer.panicSkillPenalty, 0.0f, 3.0f);
    }

    void Config::ReadDodgeSystemSettings(CSimpleIniA &a_ini)
    {
        m_dodgeSystem.dodgeStaminaCost =
            static_cast<float>(a_ini.GetDoubleValue("DodgeSystem", "DodgeStaminaCost", m_dodgeSystem.dodgeStaminaCost));
        m_dodgeSystem.iFrameDuration =
            static_cast<float>(a_ini.GetDoubleValue("DodgeSystem", "IFrameDuration", m_dodgeSystem.iFrameDuration));
        m_dodgeSystem.enableStepDodge =
            a_ini.GetBoolValue("DodgeSystem", "EnableStepDodge", m_dodgeSystem.enableStepDodge);
        m_dodgeSystem.enableDodgeAttackCancel =
            a_ini.GetBoolValue("DodgeSystem", "EnableDodgeAttackCancel", m_dodgeSystem.enableDodgeAttackCancel);

        m_dodgeSystem.forwardDodgeMaxGap = (std::clamp)(
            static_cast<float>(
                a_ini.GetDoubleValue("DodgeSystem", "ForwardDodgeMaxGap", m_dodgeSystem.forwardDodgeMaxGap)),
            0.0f, 1000.0f);
        m_dodgeSystem.forwardDodgeCounterWindowSeconds = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("DodgeSystem", "ForwardDodgeCounterWindowSeconds",
                                                    m_dodgeSystem.forwardDodgeCounterWindowSeconds)),
            0.0f, 5.0f);

        // Empty direction names keep the default; an empty fallback means no second try.
        auto event = [&](const char *a_key, std::string &a_value) {
            const std::string value = a_ini.GetValue("DodgeSystem", a_key, a_value.c_str());
            if (!value.empty() || a_value.empty()) {
                a_value = value;
            }
        };
        event("DodgeEventForward", m_dodgeSystem.eventForward);
        event("DodgeEventBack", m_dodgeSystem.eventBack);
        event("DodgeEventLeft", m_dodgeSystem.eventLeft);
        event("DodgeEventRight", m_dodgeSystem.eventRight);
        event("DodgeEventFallback", m_dodgeSystem.eventFallback);

        m_dodgeSystem.minDodgeStamina = (std::max)(
            0.0f, static_cast<float>(a_ini.GetDoubleValue("DodgeSystem", "MinDodgeStamina", m_dodgeSystem.minDodgeStamina)));

        // Clamp values
        m_dodgeSystem.dodgeStaminaCost = (std::max)(0.0f, m_dodgeSystem.dodgeStaminaCost);
        m_dodgeSystem.iFrameDuration = (std::max)(0.0f, m_dodgeSystem.iFrameDuration);
    }

    void Config::ReadDecisionMatrixSettings(CSimpleIniA &a_ini)
    {
        m_decisionMatrix.interruptMaxDistance = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "InterruptMaxDistance", m_decisionMatrix.interruptMaxDistance));
        m_decisionMatrix.interruptReachMultiplier = static_cast<float>(a_ini.GetDoubleValue(
            "DecisionMatrix", "InterruptReachMultiplier", m_decisionMatrix.interruptReachMultiplier));
        m_decisionMatrix.enableEvasionDodge =
            a_ini.GetBoolValue("DecisionMatrix", "EnableEvasionDodge", m_decisionMatrix.enableEvasionDodge);
        m_decisionMatrix.evasionMinDistance = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "EvasionMinDistance", m_decisionMatrix.evasionMinDistance));
        m_decisionMatrix.enableJumpEvasion =
            a_ini.GetBoolValue("DecisionMatrix", "EnableJumpEvasion", m_decisionMatrix.enableJumpEvasion);
        m_decisionMatrix.jumpEvasionDistanceMin = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "JumpEvasionDistanceMin", m_decisionMatrix.jumpEvasionDistanceMin));
        m_decisionMatrix.jumpEvasionDistanceMax = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "JumpEvasionDistanceMax", m_decisionMatrix.jumpEvasionDistanceMax));
        m_decisionMatrix.evasionJumpChance = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "EvasionJumpChance", m_decisionMatrix.evasionJumpChance));
        m_decisionMatrix.jumpHeight =
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "JumpHeight", m_decisionMatrix.jumpHeight));
        m_decisionMatrix.jumpStaminaCost = (std::max)(
            0.0f, static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "JumpStaminaCost", m_decisionMatrix.jumpStaminaCost)));
        m_decisionMatrix.staminaThreshold = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "StaminaThreshold", m_decisionMatrix.staminaThreshold));
        m_decisionMatrix.staminaAffectsRetreat =
            a_ini.GetBoolValue("DecisionMatrix", "StaminaAffectsRetreat", m_decisionMatrix.staminaAffectsRetreat);
        m_decisionMatrix.brainBackoffFallback =
            a_ini.GetBoolValue("DecisionMatrix", "BrainBackoffFallback", m_decisionMatrix.brainBackoffFallback);
        m_decisionMatrix.backoffStepDistance = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "BackoffStepDistance", m_decisionMatrix.backoffStepDistance)),
            50.0f, 400.0f);
        m_decisionMatrix.powerAttackMinStamina = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "PowerAttackMinStamina", m_decisionMatrix.powerAttackMinStamina)),
            0.0f, 1.0f);
        m_decisionMatrix.healthThreshold = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "HealthThreshold", m_decisionMatrix.healthThreshold));
        m_decisionMatrix.enableSurvivalRetreat =
            a_ini.GetBoolValue("DecisionMatrix", "EnableSurvivalRetreat", m_decisionMatrix.enableSurvivalRetreat);
        m_decisionMatrix.enableOffense =
            a_ini.GetBoolValue("DecisionMatrix", "EnableOffense", m_decisionMatrix.enableOffense);
        m_decisionMatrix.offenseReachMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "OffenseReachMultiplier", m_decisionMatrix.offenseReachMultiplier));
        m_decisionMatrix.offenseReachSafetyMargin = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "OffenseReachSafetyMargin",
                                                    m_decisionMatrix.offenseReachSafetyMargin)),
            0.0f, 250.0f);
        m_decisionMatrix.enableSprintAttack =
            a_ini.GetBoolValue("DecisionMatrix", "EnableSprintAttack", m_decisionMatrix.enableSprintAttack);
        m_decisionMatrix.enableSprintCharge =
            a_ini.GetBoolValue("DecisionMatrix", "EnableSprintCharge", m_decisionMatrix.enableSprintCharge);
        m_decisionMatrix.sprintAttackLaunchDistance = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "SprintAttackLaunchDistance",
                                                    m_decisionMatrix.sprintAttackLaunchDistance)),
            0.0f, 1000.0f);
        m_decisionMatrix.sprintAttackMinDistance = static_cast<float>(a_ini.GetDoubleValue(
            "DecisionMatrix", "SprintAttackMinDistance", m_decisionMatrix.sprintAttackMinDistance));
        m_decisionMatrix.sprintAttackMaxDistance = static_cast<float>(a_ini.GetDoubleValue(
            "DecisionMatrix", "SprintAttackMaxDistance", m_decisionMatrix.sprintAttackMaxDistance));
        m_decisionMatrix.attackStaminaCost = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "AttackStaminaCost", m_decisionMatrix.attackStaminaCost));
        m_decisionMatrix.powerAttackStaminaCost = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "PowerAttackStaminaCost", m_decisionMatrix.powerAttackStaminaCost));
        m_decisionMatrix.sprintAttackStaminaCost = static_cast<float>(a_ini.GetDoubleValue(
            "DecisionMatrix", "SprintAttackStaminaCost", m_decisionMatrix.sprintAttackStaminaCost));
        m_decisionMatrix.enablePowerAttackStaminaCheck = a_ini.GetBoolValue(
            "DecisionMatrix", "EnablePowerAttackStaminaCheck", m_decisionMatrix.enablePowerAttackStaminaCheck);
        m_decisionMatrix.enableSprintAttackStaminaCheck = a_ini.GetBoolValue(
            "DecisionMatrix", "EnableSprintAttackStaminaCheck", m_decisionMatrix.enableSprintAttackStaminaCheck);

        // Clamp values
        m_decisionMatrix.interruptMaxDistance = (std::max)(0.0f, m_decisionMatrix.interruptMaxDistance);
        m_decisionMatrix.interruptReachMultiplier = ClampValue(m_decisionMatrix.interruptReachMultiplier, 0.1f, 5.0f);
        m_decisionMatrix.jumpEvasionDistanceMin = (std::max)(0.0f, m_decisionMatrix.jumpEvasionDistanceMin);
        m_decisionMatrix.jumpEvasionDistanceMax = (std::max)(0.0f, m_decisionMatrix.jumpEvasionDistanceMax);
        m_decisionMatrix.evasionJumpChance = ClampValue(m_decisionMatrix.evasionJumpChance, 0.0f, 1.0f);
        m_decisionMatrix.jumpHeight = ClampValue(m_decisionMatrix.jumpHeight, 20.0f, 300.0f);
        m_decisionMatrix.staminaThreshold = ClampValue(m_decisionMatrix.staminaThreshold, 0.0f, 1.0f);
        m_decisionMatrix.healthThreshold = ClampValue(m_decisionMatrix.healthThreshold, 0.0f, 1.0f);
        m_decisionMatrix.offenseReachMultiplier = ClampValue(m_decisionMatrix.offenseReachMultiplier, 0.1f, 5.0f);
        m_decisionMatrix.sprintAttackMinDistance = (std::max)(0.0f, m_decisionMatrix.sprintAttackMinDistance);
        m_decisionMatrix.sprintAttackMaxDistance = (std::max)(0.0f, m_decisionMatrix.sprintAttackMaxDistance);
        m_decisionMatrix.attackStaminaCost = (std::max)(0.0f, m_decisionMatrix.attackStaminaCost);
        m_decisionMatrix.powerAttackStaminaCost = (std::max)(0.0f, m_decisionMatrix.powerAttackStaminaCost);
        m_decisionMatrix.sprintAttackStaminaCost = (std::max)(0.0f, m_decisionMatrix.sprintAttackStaminaCost);
    }

    void Config::ReadScoringWeightsSettings(CSimpleIniA &a_ini)
    {
        auto readWeight = [&](const char *key, float current) {
            return static_cast<float>(a_ini.GetDoubleValue("ScoringWeights", key, current));
        };

        // Per-action base priorities
        m_scoringWeights.interruptPowerAttackBase =
            readWeight("InterruptPowerAttackBase", m_scoringWeights.interruptPowerAttackBase);
        m_scoringWeights.evasionDodgeBase = readWeight("EvasionDodgeBase", m_scoringWeights.evasionDodgeBase);
        m_scoringWeights.advancingBase = readWeight("AdvancingBase", m_scoringWeights.advancingBase);
        m_scoringWeights.sprintAttackBase = readWeight("SprintAttackBase", m_scoringWeights.sprintAttackBase);
        m_scoringWeights.attackBase = readWeight("AttackBase", m_scoringWeights.attackBase);
        m_scoringWeights.backoffBase = readWeight("BackoffBase", m_scoringWeights.backoffBase);
        m_scoringWeights.flankingBase = readWeight("FlankingBase", m_scoringWeights.flankingBase);

        // High-impact offense modifiers
        m_scoringWeights.targetStaggeredBonus =
            readWeight("TargetStaggeredBonus", m_scoringWeights.targetStaggeredBonus);
        m_scoringWeights.targetCastingBonus = readWeight("TargetCastingBonus", m_scoringWeights.targetCastingBonus);
        m_scoringWeights.targetRecoveryBonus = readWeight("TargetRecoveryBonus", m_scoringWeights.targetRecoveryBonus);
        m_scoringWeights.targetFleeingBonus = readWeight("TargetFleeingBonus", m_scoringWeights.targetFleeingBonus);
        m_scoringWeights.targetLowHealthFinisherBonus =
            readWeight("TargetLowHealthFinisherBonus", m_scoringWeights.targetLowHealthFinisherBonus);
        m_scoringWeights.openingRiskPenalty = readWeight("OpeningRiskPenalty", m_scoringWeights.openingRiskPenalty);
        m_scoringWeights.flankingAttackBonus = readWeight("FlankingAttackBonus", m_scoringWeights.flankingAttackBonus);
        m_scoringWeights.allyCoverBonus = readWeight("AllyCoverBonus", m_scoringWeights.allyCoverBonus);

        // Clamp to sane ranges (priorities positive, modifiers bounded)
        auto &w = m_scoringWeights;
        for (float *base : {&w.interruptPowerAttackBase, &w.evasionDodgeBase, &w.advancingBase, &w.sprintAttackBase,
                            &w.attackBase, &w.backoffBase, &w.flankingBase}) {
            *base = ClampValue(*base, 0.0f, 5.0f);
        }
        for (float *mod : {&w.targetStaggeredBonus, &w.targetCastingBonus, &w.targetRecoveryBonus, &w.targetFleeingBonus,
                           &w.targetLowHealthFinisherBonus, &w.openingRiskPenalty, &w.flankingAttackBonus,
                           &w.allyCoverBonus}) {
            *mod = ClampValue(*mod, 0.0f, 3.0f);
        }
    }

    void Config::ReadPerformanceSettings(CSimpleIniA &a_ini)
    {
        m_performance.cleanupInterval =
            static_cast<float>(a_ini.GetDoubleValue("Performance", "CleanupInterval", m_performance.cleanupInterval));
        m_performance.maxActorsPerFrame = static_cast<std::uint32_t>(
            a_ini.GetLongValue("Performance", "MaxActorsPerFrame", m_performance.maxActorsPerFrame));

        m_performance.processingIntervalMid = static_cast<float>(
            a_ini.GetDoubleValue("Performance", "ProcessingIntervalMid", m_performance.processingIntervalMid));
        m_performance.processingIntervalFar = static_cast<float>(
            a_ini.GetDoubleValue("Performance", "ProcessingIntervalFar", m_performance.processingIntervalFar));
        m_performance.distanceNear =
            static_cast<float>(a_ini.GetDoubleValue("Performance", "DistanceNear", m_performance.distanceNear));
        m_performance.distanceMid =
            static_cast<float>(a_ini.GetDoubleValue("Performance", "DistanceMid", m_performance.distanceMid));

        // Clamp values
        m_performance.cleanupInterval = (std::max)(0.1f, m_performance.cleanupInterval);
        m_performance.processingIntervalMid = (std::max)(0.01f, m_performance.processingIntervalMid);
        m_performance.processingIntervalFar = (std::max)(0.01f, m_performance.processingIntervalFar);
        m_performance.distanceNear = (std::max)(0.0f, m_performance.distanceNear);
        m_performance.distanceMid = (std::max)(m_performance.distanceNear, m_performance.distanceMid);
    }

    void Config::ReadModIntegrationSettings(CSimpleIniA &a_ini)
    {
        m_modIntegrations.enableCPRIntegration =
            a_ini.GetBoolValue("ModIntegrations", "EnableCPRIntegration", m_modIntegrations.enableCPRIntegration);
        m_modIntegrations.enablePrecisionIntegration = a_ini.GetBoolValue(
            "ModIntegrations", "EnablePrecisionIntegration", m_modIntegrations.enablePrecisionIntegration);
        m_modIntegrations.enableTKDodgeIntegration = a_ini.GetBoolValue("ModIntegrations", "EnableTKDodgeIntegration",
                                                                        m_modIntegrations.enableTKDodgeIntegration);
        m_modIntegrations.enableSCARIntegration =
            a_ini.GetBoolValue("ModIntegrations", "EnableSCARIntegration", m_modIntegrations.enableSCARIntegration);
        m_modIntegrations.scarComboWindowSeconds = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("ModIntegrations", "SCARComboWindowSeconds",
                                                    m_modIntegrations.scarComboWindowSeconds)),
            1.0f, 300.0f);
        m_modIntegrations.scarStallSeconds = (std::clamp)(
            static_cast<float>(
                a_ini.GetDoubleValue("ModIntegrations", "SCARStallSeconds", m_modIntegrations.scarStallSeconds)),
            0.5f, 30.0f);
    }

    void Config::ReadParrySettings(CSimpleIniA &a_ini)
    {
        m_parry.enableParry = a_ini.GetBoolValue("Parry", "EnableParry", m_parry.enableParry);
        m_parry.parryWindowStart =
            static_cast<float>(a_ini.GetDoubleValue("Parry", "ParryWindowStart", m_parry.parryWindowStart));
        m_parry.parryWindowEnd =
            static_cast<float>(a_ini.GetDoubleValue("Parry", "ParryWindowEnd", m_parry.parryWindowEnd));
        m_parry.parryMinDistance =
            static_cast<float>(a_ini.GetDoubleValue("Parry", "ParryMinDistance", m_parry.parryMinDistance));
        m_parry.parryMaxDistance =
            static_cast<float>(a_ini.GetDoubleValue("Parry", "ParryMaxDistance", m_parry.parryMaxDistance));
        m_parry.parryBasePriority =
            static_cast<float>(a_ini.GetDoubleValue("Parry", "ParryBasePriority", m_parry.parryBasePriority));
        m_parry.timingBonusMax =
            static_cast<float>(a_ini.GetDoubleValue("Parry", "TimingBonusMax", m_parry.timingBonusMax));
        m_parry.earlyBashPenalty =
            static_cast<float>(a_ini.GetDoubleValue("Parry", "EarlyBashPenalty", m_parry.earlyBashPenalty));
        m_parry.lateBashPenalty =
            static_cast<float>(a_ini.GetDoubleValue("Parry", "LateBashPenalty", m_parry.lateBashPenalty));

        // Clamp values
        m_parry.parryWindowStart = (std::max)(0.0f, m_parry.parryWindowStart);
        m_parry.parryWindowEnd = (std::max)(m_parry.parryWindowStart, m_parry.parryWindowEnd);
        m_parry.parryMinDistance = (std::max)(0.0f, m_parry.parryMinDistance);
        m_parry.parryMaxDistance = (std::max)(m_parry.parryMinDistance, m_parry.parryMaxDistance);
        m_parry.parryBasePriority = (std::max)(0.0f, m_parry.parryBasePriority);
        m_parry.timingBonusMax = ClampValue(m_parry.timingBonusMax, 0.0f, 1.0f);
        m_parry.earlyBashPenalty = ClampValue(m_parry.earlyBashPenalty, 0.0f, 1.0f);
        m_parry.lateBashPenalty = ClampValue(m_parry.lateBashPenalty, 0.0f, 1.0f);
    }

    void Config::ReadTimedBlockSettings(CSimpleIniA &a_ini)
    {
        m_timedBlock.enableTimedBlock =
            a_ini.GetBoolValue("TimedBlock", "EnableTimedBlock", m_timedBlock.enableTimedBlock);
        m_timedBlock.timedBlockWindowStart = static_cast<float>(
            a_ini.GetDoubleValue("TimedBlock", "TimedBlockWindowStart", m_timedBlock.timedBlockWindowStart));
        m_timedBlock.timedBlockWindowEnd = static_cast<float>(
            a_ini.GetDoubleValue("TimedBlock", "TimedBlockWindowEnd", m_timedBlock.timedBlockWindowEnd));
        m_timedBlock.timedBlockMinDistance = static_cast<float>(
            a_ini.GetDoubleValue("TimedBlock", "TimedBlockMinDistance", m_timedBlock.timedBlockMinDistance));
        m_timedBlock.timedBlockMaxDistance = static_cast<float>(
            a_ini.GetDoubleValue("TimedBlock", "TimedBlockMaxDistance", m_timedBlock.timedBlockMaxDistance));
        m_timedBlock.timedBlockBasePriority = static_cast<float>(
            a_ini.GetDoubleValue("TimedBlock", "TimedBlockBasePriority", m_timedBlock.timedBlockBasePriority));
        m_timedBlock.timedBlockTimingBonusMax = static_cast<float>(
            a_ini.GetDoubleValue("TimedBlock", "TimedBlockTimingBonusMax", m_timedBlock.timedBlockTimingBonusMax));
        m_timedBlock.timedBlockEarlyPenalty = static_cast<float>(
            a_ini.GetDoubleValue("TimedBlock", "TimedBlockEarlyPenalty", m_timedBlock.timedBlockEarlyPenalty));
        m_timedBlock.timedBlockLatePenalty = static_cast<float>(
            a_ini.GetDoubleValue("TimedBlock", "TimedBlockLatePenalty", m_timedBlock.timedBlockLatePenalty));

        // Clamp values
        m_timedBlock.timedBlockWindowStart = (std::max)(0.0f, m_timedBlock.timedBlockWindowStart);
        m_timedBlock.timedBlockWindowEnd =
            (std::max)(m_timedBlock.timedBlockWindowStart, m_timedBlock.timedBlockWindowEnd);
        m_timedBlock.timedBlockMinDistance = (std::max)(0.0f, m_timedBlock.timedBlockMinDistance);
        m_timedBlock.timedBlockMaxDistance =
            (std::max)(m_timedBlock.timedBlockMinDistance, m_timedBlock.timedBlockMaxDistance);
        m_timedBlock.timedBlockBasePriority = (std::max)(0.0f, m_timedBlock.timedBlockBasePriority);
        m_timedBlock.timedBlockTimingBonusMax = ClampValue(m_timedBlock.timedBlockTimingBonusMax, 0.0f, 1.0f);
        m_timedBlock.timedBlockEarlyPenalty = ClampValue(m_timedBlock.timedBlockEarlyPenalty, 0.0f, 1.0f);
        m_timedBlock.timedBlockLatePenalty = ClampValue(m_timedBlock.timedBlockLatePenalty, 0.0f, 1.0f);
    }

    void Config::ReadCombatPacingSettings(CSimpleIniA &a_ini)
    {
        m_combatPacing.enableCombatPacing =
            a_ini.GetBoolValue("CombatPacing", "EnableCombatPacing", m_combatPacing.enableCombatPacing);
        m_combatPacing.maxSimultaneousAttackers = static_cast<int>(
            a_ini.GetLongValue("CombatPacing", "MaxSimultaneousAttackers", m_combatPacing.maxSimultaneousAttackers));
        m_combatPacing.slotWindowMinSeconds = static_cast<float>(
            a_ini.GetDoubleValue("CombatPacing", "SlotWindowMinSeconds", m_combatPacing.slotWindowMinSeconds));
        m_combatPacing.slotWindowMaxSeconds = static_cast<float>(
            a_ini.GetDoubleValue("CombatPacing", "SlotWindowMaxSeconds", m_combatPacing.slotWindowMaxSeconds));
        m_combatPacing.paceTargetPlayerOnly =
            a_ini.GetBoolValue("CombatPacing", "PaceTargetPlayerOnly", m_combatPacing.paceTargetPlayerOnly);
        auto F = [&](const char *a_key, float &a_value, float a_min, float a_max) {
            a_value = (std::clamp)(static_cast<float>(a_ini.GetDoubleValue("CombatPacing", a_key, a_value)), a_min,
                                   a_max);
        };
        F("EngageMargin", m_combatPacing.engageMargin, 0.0f, 1000.0f);
        F("WaitDistanceMargin", m_combatPacing.waitDistanceMargin, 0.0f, 1000.0f);
        F("RetaliateMinSeconds", m_combatPacing.retaliateMinSeconds, 0.0f, 10.0f);
        F("RetaliateMaxSeconds", m_combatPacing.retaliateMaxSeconds, 0.0f, 10.0f);
        m_combatPacing.retaliateMaxSeconds =
            (std::max)(m_combatPacing.retaliateMinSeconds, m_combatPacing.retaliateMaxSeconds);

        // Clamp
        m_combatPacing.maxSimultaneousAttackers = (std::max)(0, m_combatPacing.maxSimultaneousAttackers);
        m_combatPacing.slotWindowMinSeconds = (std::max)(0.1f, m_combatPacing.slotWindowMinSeconds);
        m_combatPacing.slotWindowMaxSeconds =
            (std::max)(m_combatPacing.slotWindowMinSeconds, m_combatPacing.slotWindowMaxSeconds);
    }

    void Config::ReadDamageMomentumSettings(CSimpleIniA &a_ini)
    {
        m_damageMomentum.enableDamageMomentum =
            a_ini.GetBoolValue("DamageMomentum", "EnableDamageMomentum", m_damageMomentum.enableDamageMomentum);
        m_damageMomentum.bigHitThreshold = static_cast<float>(
            a_ini.GetDoubleValue("DamageMomentum", "BigHitThreshold", m_damageMomentum.bigHitThreshold));
        m_damageMomentum.chipThreshold = static_cast<float>(
            a_ini.GetDoubleValue("DamageMomentum", "ChipThreshold", m_damageMomentum.chipThreshold));
        m_damageMomentum.offensiveBonusMax = static_cast<float>(
            a_ini.GetDoubleValue("DamageMomentum", "OffensiveBonusMax", m_damageMomentum.offensiveBonusMax));
        m_damageMomentum.defensivePenaltyMax = static_cast<float>(
            a_ini.GetDoubleValue("DamageMomentum", "DefensivePenaltyMax", m_damageMomentum.defensivePenaltyMax));
        m_damageMomentum.momentumDecaySeconds = static_cast<float>(
            a_ini.GetDoubleValue("DamageMomentum", "MomentumDecaySeconds", m_damageMomentum.momentumDecaySeconds));
        m_damageMomentum.ewmaAlpha =
            static_cast<float>(a_ini.GetDoubleValue("DamageMomentum", "EwmaAlpha", m_damageMomentum.ewmaAlpha));

        // Clamp
        m_damageMomentum.chipThreshold = (std::max)(0.0f, m_damageMomentum.chipThreshold);
        m_damageMomentum.bigHitThreshold = (std::max)(m_damageMomentum.chipThreshold + 0.001f, m_damageMomentum.bigHitThreshold);
        m_damageMomentum.offensiveBonusMax = ClampValue(m_damageMomentum.offensiveBonusMax, 0.0f, 2.0f);
        m_damageMomentum.defensivePenaltyMax = ClampValue(m_damageMomentum.defensivePenaltyMax, 0.0f, 2.0f);
        m_damageMomentum.momentumDecaySeconds = (std::max)(0.5f, m_damageMomentum.momentumDecaySeconds);
        m_damageMomentum.ewmaAlpha = ClampValue(m_damageMomentum.ewmaAlpha, 0.05f, 1.0f);
    }

    void Config::ReadEnvSensorSettings(CSimpleIniA &a_ini)
    {
        m_envSensor.enableLosGate = a_ini.GetBoolValue("EnvSensor", "EnableLosGate", m_envSensor.enableLosGate);
        m_envSensor.losEyeHeight =
            static_cast<float>(a_ini.GetDoubleValue("EnvSensor", "LosEyeHeight", m_envSensor.losEyeHeight));
        m_envSensor.enableCliffSafeDodge =
            a_ini.GetBoolValue("EnvSensor", "EnableCliffSafeDodge", m_envSensor.enableCliffSafeDodge);
        m_envSensor.dodgeLedgeProbeDistance = static_cast<float>(
            a_ini.GetDoubleValue("EnvSensor", "DodgeLedgeProbeDistance", m_envSensor.dodgeLedgeProbeDistance));
        m_envSensor.dodgeMaxDrop =
            static_cast<float>(a_ini.GetDoubleValue("EnvSensor", "DodgeMaxDrop", m_envSensor.dodgeMaxDrop));

        // Clamp
        m_envSensor.losEyeHeight = ClampValue(m_envSensor.losEyeHeight, 0.0f, 300.0f);
        m_envSensor.dodgeLedgeProbeDistance = (std::max)(0.0f, m_envSensor.dodgeLedgeProbeDistance);
        m_envSensor.dodgeMaxDrop = (std::max)(0.0f, m_envSensor.dodgeMaxDrop);
    }

    void Config::ReadTargetSwitchingSettings(CSimpleIniA &a_ini)
    {
        auto &t = m_targetSwitching;
        auto readFloat = [&](const char *key, float current) {
            return static_cast<float>(a_ini.GetDoubleValue("TargetSwitching", key, current));
        };
        t.enableTargetSwitching = a_ini.GetBoolValue("TargetSwitching", "EnableTargetSwitching", t.enableTargetSwitching);
        t.switchCooldownSeconds = (std::max)(0.5f, readFloat("SwitchCooldownSeconds", t.switchCooldownSeconds));
        t.retaliateWindowSeconds = (std::max)(0.1f, readFloat("RetaliateWindowSeconds", t.retaliateWindowSeconds));
        t.retaliateChance = ClampValue(readFloat("RetaliateChance", t.retaliateChance), 0.0f, 1.0f);
        t.retaliateMaxDistance = (std::max)(0.0f, readFloat("RetaliateMaxDistance", t.retaliateMaxDistance));
        t.finisherHealthThreshold =
            ClampValue(readFloat("FinisherHealthThreshold", t.finisherHealthThreshold), 0.0f, 1.0f);
        t.finisherMaxDistance = (std::max)(0.0f, readFloat("FinisherMaxDistance", t.finisherMaxDistance));
        t.finisherChance = ClampValue(readFloat("FinisherChance", t.finisherChance), 0.0f, 1.0f);
    }

    void Config::ReadArmorStyleSettings(CSimpleIniA &a_ini)
    {
        auto &a = m_armorStyle;
        auto readFloat = [&](const char *key, float current) {
            return ClampValue(static_cast<float>(a_ini.GetDoubleValue("ArmorStyle", key, current)), 0.0f, 1.0f);
        };
        a.enableArmorStyle = a_ini.GetBoolValue("ArmorStyle", "EnableArmorStyle", a.enableArmorStyle);
        a.lightEvasionBonus = readFloat("LightEvasionBonus", a.lightEvasionBonus);
        a.heavyEvasionPenalty = readFloat("HeavyEvasionPenalty", a.heavyEvasionPenalty);
        a.heavyGuardBonus = readFloat("HeavyGuardBonus", a.heavyGuardBonus);
    }

    void Config::ReadTeamworkSettings(CSimpleIniA &a_ini)
    {
        m_teamwork.enableFollowerPincer =
            a_ini.GetBoolValue("Teamwork", "EnableFollowerPincer", m_teamwork.enableFollowerPincer);
        m_teamwork.followerPincerBonus = ClampValue(
            static_cast<float>(a_ini.GetDoubleValue("Teamwork", "FollowerPincerBonus", m_teamwork.followerPincerBonus)),
            0.0f, 1.0f);
    }

    void Config::ReadProjectileAwarenessSettings(CSimpleIniA &a_ini)
    {
        auto &s = m_projectileAwareness;
        auto F = [&](const char *a_key, float &a_value, float a_min, float a_max) {
            a_value = ClampValue(static_cast<float>(a_ini.GetDoubleValue("ProjectileAwareness", a_key, a_value)),
                                 a_min, a_max);
        };
        s.enableProjectileDodge =
            a_ini.GetBoolValue("ProjectileAwareness", "EnableProjectileDodge", s.enableProjectileDodge);
        F("ReactMinSeconds", s.reactMinSeconds, 0.0f, 2.0f);
        F("ReactMaxSeconds", s.reactMaxSeconds, 0.05f, 3.0f);
        F("DodgeChance", s.dodgeChance, 0.0f, 1.0f);
        F("HitMargin", s.hitMargin, 0.0f, 200.0f);
        F("DodgePriority", s.dodgePriority, 0.0f, 5.0f);
        s.reactMaxSeconds = (std::max)(s.reactMaxSeconds, s.reactMinSeconds);
    }

    void Config::ReadVanillaAttackSettings(CSimpleIniA &a_ini)
    {
        auto &s = m_vanillaAttacks;
        auto F = [&](const char *a_key, float &a_value, float a_min, float a_max) {
            a_value = ClampValue(static_cast<float>(a_ini.GetDoubleValue("VanillaAttacks", a_key, a_value)), a_min,
                                 a_max);
        };
        s.enable = a_ini.GetBoolValue("VanillaAttacks", "EnableVanillaAttackShaping", s.enable);
        F("OpeningPowerMult", s.openingPowerMult, 0.0f, 5.0f);
        F("OpeningNormalMult", s.openingNormalMult, 0.0f, 5.0f);
        F("BlockedPowerMult", s.blockedPowerMult, 0.0f, 5.0f);
        F("BlockedNormalMult", s.blockedNormalMult, 0.0f, 5.0f);
    }

    void Config::ApplyPreset(Preset a_preset)
    {
        // Every preset starts from the shipped defaults so results don't depend on
        // whatever was tuned before.
        m_humanizer = {};
        m_dodgeSystem = {};
        m_decisionMatrix = {};
        m_scoringWeights = {};
        m_parry = {};
        m_timedBlock = {};
        m_combatPacing = {};
        m_damageMomentum = {};
        m_envSensor = {};
        m_targetSwitching = {};
        m_armorStyle = {};
        m_teamwork = {};
        m_vanillaAttacks = {};
        m_projectileAwareness = {};

        switch (a_preset) {
        case Preset::Default:
            break;
        case Preset::VanillaPlus:
            // Subtle: slower, sloppier, one attacker at a time, little jumping.
            m_humanizer.baseReactionDelayMs = 300.0f;
            m_humanizer.skillBase = 0.5f;
            m_humanizer.skillPerLevel = 0.006f;
            m_decisionMatrix.evasionJumpChance = 0.2f;
            m_scoringWeights.evasionDodgeBase = 1.1f;
            m_scoringWeights.flankingBase = 1.1f;
            m_combatPacing.maxSimultaneousAttackers = 1;
            m_targetSwitching.retaliateChance = 0.3f;
            m_targetSwitching.finisherChance = 0.15f;
            break;
        case Preset::Soulslike:
            // Sharp duelists: fast reactions, punish openings, respect the player's guard.
            m_humanizer.baseReactionDelayMs = 120.0f;
            m_humanizer.skillBase = 0.85f;
            m_humanizer.skillPerLevel = 0.01f;
            m_parry.parryBasePriority = 1.8f;
            m_timedBlock.timedBlockBasePriority = 1.8f;
            m_scoringWeights.targetRecoveryBonus = 0.8f;
            m_scoringWeights.targetStaggeredBonus = 0.8f;
            m_scoringWeights.openingRiskPenalty = 0.6f;
            m_damageMomentum.offensiveBonusMax = 0.6f;
            m_vanillaAttacks.openingPowerMult = 3.0f;
            m_vanillaAttacks.blockedNormalMult = 0.1f;
            m_projectileAwareness.dodgeChance = 0.8f;
            break;
        case Preset::Chaos:
            // Everyone swings at once and never stops moving.
            m_humanizer.baseReactionDelayMs = 80.0f;
            m_humanizer.skillBase = 0.8f;
            m_humanizer.enableFatigue = false;
            m_decisionMatrix.evasionJumpChance = 0.7f;
            m_scoringWeights.attackBase = 1.3f;
            m_scoringWeights.sprintAttackBase = 1.6f;
            m_combatPacing.enableCombatPacing = false;
            m_damageMomentum.offensiveBonusMax = 0.8f;
            m_targetSwitching.retaliateChance = 0.8f;
            m_targetSwitching.finisherChance = 0.6f;
            m_targetSwitching.switchCooldownSeconds = 3.0f;
            break;
        }
        MarkHumanizerDirty();
    }
} // namespace CombatAI

#include "Config.h"
#include "Logger.h"
#include "pch.h"
#include <filesystem>

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

        LOG_INFO("Configuration loaded successfully");
        return true;
    }

    bool Config::Save(const std::string &a_filePath)
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        // Load the existing file first so comments and unknown keys are preserved.
        ini.LoadFile(a_filePath.c_str());

        auto B = [&](const char *sec, const char *key, bool val) { ini.SetBoolValue(sec, key, val); };
        auto D = [&](const char *sec, const char *key, float val) {
            ini.SetDoubleValue(sec, key, static_cast<double>(val));
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
        D("Humanizer", "BaseMistakeChance", m_humanizer.baseMistakeChance);
        D("Humanizer", "MistakeChanceReductionPerLevel", m_humanizer.mistakeChanceReductionPerLevel);
        D("Humanizer", "MinMistakeChance", m_humanizer.minMistakeChance);
        D("Humanizer", "BashCooldownSeconds", m_humanizer.bashCooldownSeconds);
        D("Humanizer", "DodgeCooldownSeconds", m_humanizer.dodgeCooldownSeconds);
        D("Humanizer", "JumpCooldownSeconds", m_humanizer.jumpCooldownSeconds);
        D("Humanizer", "BashMistakeMultiplier", m_humanizer.bashMistakeMultiplier);
        D("Humanizer", "DodgeMistakeMultiplier", m_humanizer.dodgeMistakeMultiplier);
        D("Humanizer", "JumpMistakeMultiplier", m_humanizer.jumpMistakeMultiplier);
        D("Humanizer", "StrafeMistakeMultiplier", m_humanizer.strafeMistakeMultiplier);
        D("Humanizer", "PowerAttackMistakeMultiplier", m_humanizer.powerAttackMistakeMultiplier);
        D("Humanizer", "AttackMistakeMultiplier", m_humanizer.attackMistakeMultiplier);
        D("Humanizer", "SprintAttackMistakeMultiplier", m_humanizer.sprintAttackMistakeMultiplier);
        D("Humanizer", "RetreatMistakeMultiplier", m_humanizer.retreatMistakeMultiplier);
        D("Humanizer", "BackoffMistakeMultiplier", m_humanizer.backoffMistakeMultiplier);
        D("Humanizer", "AdvancingMistakeMultiplier", m_humanizer.advancingMistakeMultiplier);
        D("Humanizer", "FlankingMistakeMultiplier", m_humanizer.flankingMistakeMultiplier);

        // Dodge system
        D("DodgeSystem", "DodgeStaminaCost", m_dodgeSystem.dodgeStaminaCost);
        D("DodgeSystem", "IFrameDuration", m_dodgeSystem.iFrameDuration);
        B("DodgeSystem", "EnableStepDodge", m_dodgeSystem.enableStepDodge);
        B("DodgeSystem", "EnableDodgeAttackCancel", m_dodgeSystem.enableDodgeAttackCancel);

        // Decision matrix
        D("DecisionMatrix", "InterruptMaxDistance", m_decisionMatrix.interruptMaxDistance);
        D("DecisionMatrix", "InterruptReachMultiplier", m_decisionMatrix.interruptReachMultiplier);
        B("DecisionMatrix", "EnableEvasionDodge", m_decisionMatrix.enableEvasionDodge);
        D("DecisionMatrix", "EvasionMinDistance", m_decisionMatrix.evasionMinDistance);
        B("DecisionMatrix", "EnableJumpEvasion", m_decisionMatrix.enableJumpEvasion);
        D("DecisionMatrix", "JumpEvasionDistanceMin", m_decisionMatrix.jumpEvasionDistanceMin);
        D("DecisionMatrix", "JumpEvasionDistanceMax", m_decisionMatrix.jumpEvasionDistanceMax);
        D("DecisionMatrix", "EvasionJumpChance", m_decisionMatrix.evasionJumpChance);
        D("DecisionMatrix", "StaminaThreshold", m_decisionMatrix.staminaThreshold);
        D("DecisionMatrix", "HealthThreshold", m_decisionMatrix.healthThreshold);
        B("DecisionMatrix", "EnableSurvivalRetreat", m_decisionMatrix.enableSurvivalRetreat);
        B("DecisionMatrix", "EnableOffense", m_decisionMatrix.enableOffense);
        D("DecisionMatrix", "OffenseReachMultiplier", m_decisionMatrix.offenseReachMultiplier);
        D("DecisionMatrix", "OffenseReachSafetyMargin", m_decisionMatrix.offenseReachSafetyMargin);
        B("DecisionMatrix", "EnableSprintAttack", m_decisionMatrix.enableSprintAttack);
        B("DecisionMatrix", "EnableSprintCharge", m_decisionMatrix.enableSprintCharge);
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
        B("ModIntegrations", "EnableBFCOIntegration", m_modIntegrations.enableBFCOIntegration);
        B("ModIntegrations", "EnablePrecisionIntegration", m_modIntegrations.enablePrecisionIntegration);
        B("ModIntegrations", "EnableTKDodgeIntegration", m_modIntegrations.enableTKDodgeIntegration);

        // Performance
        B("Performance", "OnlyProcessCombatActors", m_performance.onlyProcessCombatActors);
        D("Performance", "CleanupInterval", m_performance.cleanupInterval);
        ini.SetLongValue("Performance", "MaxActorsPerFrame", static_cast<long>(m_performance.maxActorsPerFrame));
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
        ini.SetLongValue("CombatPacing", "MaxSimultaneousAttackers",
                         static_cast<long>(m_combatPacing.maxSimultaneousAttackers));
        D("CombatPacing", "SlotWindowMinSeconds", m_combatPacing.slotWindowMinSeconds);
        D("CombatPacing", "SlotWindowMaxSeconds", m_combatPacing.slotWindowMaxSeconds);
        B("CombatPacing", "PaceTargetPlayerOnly", m_combatPacing.paceTargetPlayerOnly);

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

        m_humanizer.baseMistakeChance =
            static_cast<float>(a_ini.GetDoubleValue("Humanizer", "BaseMistakeChance", m_humanizer.baseMistakeChance));
        m_humanizer.mistakeChanceReductionPerLevel = static_cast<float>(a_ini.GetDoubleValue(
            "Humanizer", "MistakeChanceReductionPerLevel", m_humanizer.mistakeChanceReductionPerLevel));
        m_humanizer.minMistakeChance =
            static_cast<float>(a_ini.GetDoubleValue("Humanizer", "MinMistakeChance", m_humanizer.minMistakeChance));
        m_humanizer.bashCooldownSeconds = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "BashCooldownSeconds", m_humanizer.bashCooldownSeconds));
        m_humanizer.dodgeCooldownSeconds = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "DodgeCooldownSeconds", m_humanizer.dodgeCooldownSeconds));
        m_humanizer.jumpCooldownSeconds = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "JumpCooldownSeconds", m_humanizer.jumpCooldownSeconds));

        // Action-specific mistake chance multipliers
        m_humanizer.bashMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "BashMistakeMultiplier", m_humanizer.bashMistakeMultiplier));
        m_humanizer.dodgeMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "DodgeMistakeMultiplier", m_humanizer.dodgeMistakeMultiplier));
        m_humanizer.jumpMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "JumpMistakeMultiplier", m_humanizer.jumpMistakeMultiplier));
        m_humanizer.strafeMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "StrafeMistakeMultiplier", m_humanizer.strafeMistakeMultiplier));
        m_humanizer.powerAttackMistakeMultiplier = static_cast<float>(a_ini.GetDoubleValue(
            "Humanizer", "PowerAttackMistakeMultiplier", m_humanizer.powerAttackMistakeMultiplier));
        m_humanizer.attackMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "AttackMistakeMultiplier", m_humanizer.attackMistakeMultiplier));
        m_humanizer.sprintAttackMistakeMultiplier = static_cast<float>(a_ini.GetDoubleValue(
            "Humanizer", "SprintAttackMistakeMultiplier", m_humanizer.sprintAttackMistakeMultiplier));
        m_humanizer.retreatMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "RetreatMistakeMultiplier", m_humanizer.retreatMistakeMultiplier));
        m_humanizer.backoffMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "BackoffMistakeMultiplier", m_humanizer.backoffMistakeMultiplier));
        m_humanizer.advancingMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "AdvancingMistakeMultiplier", m_humanizer.advancingMistakeMultiplier));
        m_humanizer.flankingMistakeMultiplier = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "FlankingMistakeMultiplier", m_humanizer.flankingMistakeMultiplier));

        // Clamp values to valid ranges
        m_humanizer.baseReactionDelayMs = (std::max)(0.0f, m_humanizer.baseReactionDelayMs);
        m_humanizer.reactionDelayReductionPerLevelMs = (std::max)(0.0f, m_humanizer.reactionDelayReductionPerLevelMs);
        m_humanizer.minReactionDelayMs = (std::max)(0.0f, m_humanizer.minReactionDelayMs);
        m_humanizer.reactionVarianceMs = (std::max)(0.0f, m_humanizer.reactionVarianceMs);

        m_humanizer.baseMistakeChance = ClampValue(m_humanizer.baseMistakeChance, 0.0f, 1.0f);
        m_humanizer.mistakeChanceReductionPerLevel = (std::max)(0.0f, m_humanizer.mistakeChanceReductionPerLevel);
        m_humanizer.minMistakeChance = ClampValue(m_humanizer.minMistakeChance, 0.0f, 1.0f);
        m_humanizer.bashCooldownSeconds = (std::max)(0.0f, m_humanizer.bashCooldownSeconds);
        m_humanizer.dodgeCooldownSeconds = (std::max)(0.0f, m_humanizer.dodgeCooldownSeconds);
        m_humanizer.jumpCooldownSeconds = (std::max)(0.0f, m_humanizer.jumpCooldownSeconds);

        // Clamp mistake multipliers to reasonable range (0.0 to 3.0)
        m_humanizer.bashMistakeMultiplier = ClampValue(m_humanizer.bashMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.dodgeMistakeMultiplier = ClampValue(m_humanizer.dodgeMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.jumpMistakeMultiplier = ClampValue(m_humanizer.jumpMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.strafeMistakeMultiplier = ClampValue(m_humanizer.strafeMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.powerAttackMistakeMultiplier = ClampValue(m_humanizer.powerAttackMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.attackMistakeMultiplier = ClampValue(m_humanizer.attackMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.sprintAttackMistakeMultiplier = ClampValue(m_humanizer.sprintAttackMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.retreatMistakeMultiplier = ClampValue(m_humanizer.retreatMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.backoffMistakeMultiplier = ClampValue(m_humanizer.backoffMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.advancingMistakeMultiplier = ClampValue(m_humanizer.advancingMistakeMultiplier, 0.0f, 3.0f);
        m_humanizer.flankingMistakeMultiplier = ClampValue(m_humanizer.flankingMistakeMultiplier, 0.0f, 3.0f);
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
        m_decisionMatrix.staminaThreshold = static_cast<float>(
            a_ini.GetDoubleValue("DecisionMatrix", "StaminaThreshold", m_decisionMatrix.staminaThreshold));
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
        m_performance.onlyProcessCombatActors =
            a_ini.GetBoolValue("Performance", "OnlyProcessCombatActors", m_performance.onlyProcessCombatActors);
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
        m_modIntegrations.enableBFCOIntegration =
            a_ini.GetBoolValue("ModIntegrations", "EnableBFCOIntegration", m_modIntegrations.enableBFCOIntegration);
        m_modIntegrations.enablePrecisionIntegration = a_ini.GetBoolValue(
            "ModIntegrations", "EnablePrecisionIntegration", m_modIntegrations.enablePrecisionIntegration);
        m_modIntegrations.enableTKDodgeIntegration = a_ini.GetBoolValue("ModIntegrations", "EnableTKDodgeIntegration",
                                                                        m_modIntegrations.enableTKDodgeIntegration);
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
} // namespace CombatAI

#include "Config.h"
#include "Logger.h"
#include "pch.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
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

        ReadAll(ini);

        LOG_INFO("Configuration loaded successfully");
        LOG_INFO("Jump evasion: {} (chance {:.2f}, aimed bow/crossbow at {:.0f}-{:.0f} units, height {:.0f}, stamina {:.0f}); skill at level 1 {:.2f}",
                 m_decisionMatrix.enableJumpEvasion ? "on" : "OFF", m_decisionMatrix.evasionJumpChance,
                 m_decisionMatrix.jumpEvasionDistanceMin, m_decisionMatrix.jumpEvasionDistanceMax,
                 m_decisionMatrix.jumpHeight, m_decisionMatrix.jumpStaminaCost, m_humanizer.skillBase);
        LOG_INFO("Stamina: retreat/backoff {}; power attacks, sprint attacks and power bashes need {:.0f}% stamina",
                 m_decisionMatrix.staminaAffectsRetreat ? "also on low stamina" : "on health only",
                 m_decisionMatrix.powerAttackMinStamina * 100.0f);
        LOG_INFO("Damage race: {} (retreat at <= {:.1f} hits from death up to {:.0f}% health, press +{:.2f}); trade blows {:.2f}; "
                 "close dodge {:.2f}; defend {:.2f}",
                 m_damageMomentum.enableDamageRace ? "on" : "OFF", m_damageMomentum.raceRetreatHits,
                 m_damageMomentum.raceRetreatMaxHealth * 100.0f, m_damageMomentum.racePressBonus,
                 m_decisionMatrix.tradeBlowsChance, m_decisionMatrix.closeDodgeChance, m_decisionMatrix.defendOnSwingChance);
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
        const auto written = WriteTo(ini);

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

    std::set<std::pair<std::string, std::string>> Config::WriteTo(CSimpleIniA &ini) const
    {
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
        D("DodgeSystem", "ForwardDodgeChance", m_dodgeSystem.forwardDodgeChance);
        D("DodgeSystem", "ForwardDodgeCooldownSeconds", m_dodgeSystem.forwardDodgeCooldownSeconds);
        D("DodgeSystem", "ForwardDodgeMinStamina", m_dodgeSystem.forwardDodgeMinStamina);
        S("DodgeSystem", "DodgeEventForward", m_dodgeSystem.eventForward);
        S("DodgeSystem", "DodgeEventBack", m_dodgeSystem.eventBack);
        S("DodgeSystem", "DodgeEventLeft", m_dodgeSystem.eventLeft);
        S("DodgeSystem", "DodgeEventRight", m_dodgeSystem.eventRight);
        S("DodgeSystem", "DodgeEventFallback", m_dodgeSystem.eventFallback);
        D("DodgeSystem", "MinDodgeStamina", m_dodgeSystem.minDodgeStamina);

        // Decision matrix
        D("DecisionMatrix", "InterruptMaxDistance", m_decisionMatrix.interruptMaxDistance);
        D("DecisionMatrix", "InterruptReachMultiplier", m_decisionMatrix.interruptReachMultiplier);
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
        B("DecisionMatrix", "BackoffAfterSwing", m_decisionMatrix.backoffAfterSwing);
        B("DecisionMatrix", "BrainAdvanceDistance", m_decisionMatrix.brainAdvanceDistance);
        D("DecisionMatrix", "BackoffOnSwingChance", m_decisionMatrix.backoffOnSwingChance);
        D("DecisionMatrix", "BackoffAfterSwingChance", m_decisionMatrix.backoffAfterSwingChance);
        D("DecisionMatrix", "TradeBlowsChance", m_decisionMatrix.tradeBlowsChance);
        B("DecisionMatrix", "EnablePersonality", m_decisionMatrix.enablePersonality);
        B("DecisionMatrix", "EnableCounter", m_decisionMatrix.enableCounter);
        B("DecisionMatrix", "EnableShoutTiming", m_decisionMatrix.enableShoutTiming);
        B("DecisionMatrix", "EnableSelfShouts", m_decisionMatrix.enableSelfShouts);
        B("DecisionMatrix", "SelfShoutSlowTime", m_decisionMatrix.selfShoutSlowTime);
        B("DecisionMatrix", "IncludeHumanoidCreatures", m_decisionMatrix.includeHumanoidCreatures);
        D("DecisionMatrix", "CreatureSwingSeconds", m_decisionMatrix.creatureSwingSeconds);
        D("DecisionMatrix", "PersonalityJitter", m_decisionMatrix.personalityJitter);
        D("DecisionMatrix", "TacticalRetreatMinCaution", m_decisionMatrix.tacticalRetreatMinCaution);
        B("DecisionMatrix", "EnableTargetDanger", m_decisionMatrix.enableTargetDanger);
        D("DecisionMatrix", "TargetDangerInfluence", m_decisionMatrix.targetDangerInfluence);
        D("DecisionMatrix", "TargetDangerStrengthWeight", m_decisionMatrix.targetDangerStrengthWeight);
        D("DecisionMatrix", "FlankingMaxReach", m_decisionMatrix.flankingMaxReach);
        D("DecisionMatrix", "ShotHoldMaxSeconds", m_decisionMatrix.shotHoldMaxSeconds);
        D("DecisionMatrix", "CloseDodgeChance", m_decisionMatrix.closeDodgeChance);
        D("DecisionMatrix", "DefendOnSwingChance", m_decisionMatrix.defendOnSwingChance);
        B("DecisionMatrix", "AllowDaggerBlock", m_decisionMatrix.allowDaggerBlock);
        B("DecisionMatrix", "EnableRangedPositioning", m_decisionMatrix.enableRangedPositioning);
        D("DecisionMatrix", "RangedKeepAwayDistance", m_decisionMatrix.rangedKeepAwayDistance);
        D("DecisionMatrix", "RangedMaxDistance", m_decisionMatrix.rangedMaxDistance);
        D("DecisionMatrix", "PowerAttackMinStamina", m_decisionMatrix.powerAttackMinStamina);
        D("DecisionMatrix", "HealthThreshold", m_decisionMatrix.healthThreshold);
        B("DecisionMatrix", "EnableSurvivalRetreat", m_decisionMatrix.enableSurvivalRetreat);
        B("DecisionMatrix", "EnableNPCPotions", m_decisionMatrix.enableNPCPotions);
        D("DecisionMatrix", "PotionHealthThreshold", m_decisionMatrix.potionHealthThreshold);
        D("DecisionMatrix", "PotionMagickaThreshold", m_decisionMatrix.potionMagickaThreshold);
        D("DecisionMatrix", "PotionStaminaThreshold", m_decisionMatrix.potionStaminaThreshold);
        D("DecisionMatrix", "PotionCooldownSeconds", m_decisionMatrix.potionCooldownSeconds);
        B("DecisionMatrix", "EnableBuffPotions", m_decisionMatrix.enableBuffPotions);
        B("DecisionMatrix", "EnableNPCPoisons", m_decisionMatrix.enableNPCPoisons);
        D("DecisionMatrix", "PoisonCooldownSeconds", m_decisionMatrix.poisonCooldownSeconds);
        D("DecisionMatrix", "PoisonParalysisCooldownSeconds", m_decisionMatrix.poisonParalysisCooldownSeconds);
        D("DecisionMatrix", "SafeDrinkDistance", m_decisionMatrix.safeDrinkDistance);
        D("DecisionMatrix", "SafeDrinkMaxWaitSeconds", m_decisionMatrix.safeDrinkMaxWaitSeconds);
        B("DecisionMatrix", "EnableOffense", m_decisionMatrix.enableOffense);
        D("DecisionMatrix", "OffenseReachMultiplier", m_decisionMatrix.offenseReachMultiplier);
        D("DecisionMatrix", "OffenseReachSafetyMargin", m_decisionMatrix.offenseReachSafetyMargin);
        B("DecisionMatrix", "EnableSprintAttack", m_decisionMatrix.enableSprintAttack);
        D("DecisionMatrix", "SprintAttackLaunchDistance", m_decisionMatrix.sprintAttackLaunchDistance);
        D("DecisionMatrix", "SprintAttackMinDistance", m_decisionMatrix.sprintAttackMinDistance);
        D("DecisionMatrix", "SprintAttackMaxDistance", m_decisionMatrix.sprintAttackMaxDistance);
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
        B("ModIntegrations", "EnablePrecisionIntegration", m_modIntegrations.enablePrecisionIntegration);
        B("ModIntegrations", "EnableTKDodgeIntegration", m_modIntegrations.enableTKDodgeIntegration);
        B("ModIntegrations", "EnableSCARIntegration", m_modIntegrations.enableSCARIntegration);
        D("ModIntegrations", "SCARComboWindowSeconds", m_modIntegrations.scarComboWindowSeconds);
        D("ModIntegrations", "SCARStallSeconds", m_modIntegrations.scarStallSeconds);
        B("ModIntegrations", "EnableComboFollowUp", m_modIntegrations.enableComboFollowUp);
        D("ModIntegrations", "ComboFollowUpChance", m_modIntegrations.comboFollowUpChance);
        D("ModIntegrations", "ComboPowerChance", m_modIntegrations.comboPowerChance);
        D("ModIntegrations", "ComboReachMult", m_modIntegrations.comboReachMult);

        // Performance
        D("Performance", "CleanupInterval", m_performance.cleanupInterval);
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

        // Timed block
        B("TimedBlock", "EnableTimedBlock", m_timedBlock.enableTimedBlock);
        D("TimedBlock", "TimedBlockWindowStart", m_timedBlock.timedBlockWindowStart);
        D("TimedBlock", "TimedBlockWindowEnd", m_timedBlock.timedBlockWindowEnd);
        D("TimedBlock", "TimedBlockMinDistance", m_timedBlock.timedBlockMinDistance);
        D("TimedBlock", "TimedBlockMaxDistance", m_timedBlock.timedBlockMaxDistance);

        // Combat pacing (Wait Your Turn)
        B("CombatPacing", "EnableCombatPacing", m_combatPacing.enableCombatPacing);
        L("CombatPacing", "MaxSimultaneousAttackers", static_cast<long>(m_combatPacing.maxSimultaneousAttackers));
        L("CombatPacing", "DangerExtraAttackers", static_cast<long>(m_combatPacing.dangerExtraAttackers));
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
        B("DamageMomentum", "EnableDamageRace", m_damageMomentum.enableDamageRace);
        D("DamageMomentum", "RaceRetreatHits", m_damageMomentum.raceRetreatHits);
        D("DamageMomentum", "RaceRetreatMaxHealth", m_damageMomentum.raceRetreatMaxHealth);
        D("DamageMomentum", "RacePressBonus", m_damageMomentum.racePressBonus);

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
        B("Boss", "EnableBossDetection", m_boss.enableBossDetection);
        S("Boss", "BossNames", m_boss.bossNames);
        D("Boss", "EnragedHealth", m_boss.enragedHealth);
        D("Boss", "DesperateHealth", m_boss.desperateHealth);
        D("Boss", "CautionFloor", m_boss.cautionFloor);
        D("Boss", "SkillBonus", m_boss.skillBonus);
        D("Boss", "PowerStaminaMin", m_boss.powerStaminaMin);
        D("Boss", "GuardBreakBlockSeconds", m_boss.guardBreakBlockSeconds);

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
        return written;
    }

    void Config::ReadAll(CSimpleIniA &a_ini)
    {
        ReadGeneralSettings(a_ini);
        ReadHumanizerSettings(a_ini);
        ReadDodgeSystemSettings(a_ini);
        ReadDecisionMatrixSettings(a_ini);
        ReadScoringWeightsSettings(a_ini);
        ReadPerformanceSettings(a_ini);
        ReadModIntegrationSettings(a_ini);
        ReadParrySettings(a_ini);
        ReadTimedBlockSettings(a_ini);
        ReadCombatPacingSettings(a_ini);
        ReadDamageMomentumSettings(a_ini);
        ReadEnvSensorSettings(a_ini);
        ReadTargetSwitchingSettings(a_ini);
        ReadArmorStyleSettings(a_ini);
        ReadTeamworkSettings(a_ini);
        ReadVanillaAttackSettings(a_ini);
        ReadProjectileAwarenessSettings(a_ini);
    }

    // ---- Presets (JSON files of INI overrides)
    namespace
    {
        // A preset reaches the tuning: these sections keep their values (except the two combo chances below)
        bool IsPresetSetting(const std::string &a_section, const std::string &a_key)
        {
            if (a_section == "ModIntegrations") {
                return a_key == "ComboFollowUpChance" || a_key == "ComboPowerChance";
            }
            return a_section != "General" && a_section != "Performance";
        }

        // A file name the user typed: letters, digits, space, - _ + . only, so it stays inside PresetDirectory
        bool ValidPresetName(const std::string &a_name)
        {
            return !a_name.empty() && a_name.size() <= 64 && a_name != "Default" && a_name.front() != '.' &&
                   std::ranges::all_of(a_name, [](unsigned char c) { return std::isalnum(c) || std::strchr(" -_+.", c); });
        }
    } // namespace

    std::vector<std::string> Config::ListPresets()
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(PresetDirectory, ec)) {
            if (entry.path().extension() == ".json") {
                names.push_back(entry.path().stem().string());
            }
        }
        std::ranges::sort(names);
        return names;
    }

    bool Config::ApplyPreset(const std::string &a_name)
    {
        // The file's values go into an in-memory INI, so the section readers parse and clamp them as INI lines
        CSimpleIniA preset;
        preset.SetUnicode();
        if (a_name != "Default") {
            const auto path = std::filesystem::path(PresetDirectory) / (a_name + ".json");
            nlohmann::json json;
            try {
                std::ifstream file(path);
                if (!file) {
                    LOG_WARN("Preset {}: can't open {}", a_name, path.string());
                    return false;
                }
                json = nlohmann::json::parse(file); // plain JSON, no comments
            } catch (const nlohmann::json::exception &e) {
                LOG_WARN("Preset {}: not valid JSON ({})", a_name, e.what());
                return false;
            }
            if (!json.is_object()) {
                LOG_WARN("Preset {}: expected {{\"Section\": {{\"Key\": value}}}}", a_name);
                return false;
            }
            CSimpleIniA names;
            const auto known = WriteTo(names);
            for (const auto &[section, values] : json.items()) {
                if (section == "Description" && values.is_string()) {
                    continue; // what the preset is for: JSON has no comments
                }
                if (!values.is_object()) {
                    LOG_WARN("Preset {}: [{}] is not an object of settings: ignored", a_name, section);
                    continue;
                }
                for (const auto &[key, value] : values.items()) {
                    if (!known.contains({section, key})) {
                        LOG_WARN("Preset {}: unknown setting {}.{} (names as in the INI, case included): ignored", a_name, section, key);
                    } else if (value.is_string()) {
                        preset.SetValue(section.c_str(), key.c_str(), value.get<std::string>().c_str());
                    } else if (value.is_number() || value.is_boolean()) {
                        preset.SetValue(section.c_str(), key.c_str(), value.dump().c_str());
                    } else {
                        LOG_WARN("Preset {}: {}.{} is not a number, true/false or text: ignored", a_name, section, key);
                    }
                }
            }
        }

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
        m_boss = {};
        m_vanillaAttacks = {};
        m_projectileAwareness = {};
        // Combo follow-up chances live with the integrations (not reset above), so every preset resets them
        const ModIntegrationSettings defaults{};
        m_modIntegrations.comboFollowUpChance = defaults.comboFollowUpChance;
        m_modIntegrations.comboPowerChance = defaults.comboPowerChance;

        ReadAll(preset);
        MarkHumanizerDirty();
        LOG_INFO("Preset {} applied", a_name);
        return true;
    }

    bool Config::SavePreset(const std::string &a_name) const
    {
        if (!ValidPresetName(a_name)) {
            LOG_WARN("Preset name \"{}\" not saved: use letters, digits, spaces, - _ + . (not \"Default\")", a_name);
            return false;
        }
        // Only what differs from the defaults: applying the file (reset, then these) gives back the current tuning
        CSimpleIniA current, shipped;
        const auto keys = WriteTo(current);
        Config{}.WriteTo(shipped);
        nlohmann::ordered_json json = nlohmann::ordered_json::object();
        int differing = 0;
        for (const auto &[section, key] : keys) {
            const std::string value = current.GetValue(section.c_str(), key.c_str(), "");
            if (!IsPresetSetting(section, key) || value == shipped.GetValue(section.c_str(), key.c_str(), "")) {
                continue;
            }
            // Back to JSON types: the INI holds bools as true/false and numbers as %f / integers
            auto &slot = json[section][key];
            ++differing;
            char *end = nullptr;
            const double number = std::strtod(value.c_str(), &end);
            if (value == "true" || value == "false") {
                slot = value == "true";
            } else if (!value.empty() && *end == '\0') {
                slot = value.find('.') == std::string::npos ? nlohmann::ordered_json(std::stol(value)) : nlohmann::ordered_json(number);
            } else {
                slot = value;
            }
        }
        std::error_code ec;
        std::filesystem::create_directories(PresetDirectory, ec);
        const auto path = std::filesystem::path(PresetDirectory) / (a_name + ".json");
        std::ofstream file(path, std::ios::trunc);
        if (!(file << json.dump(4) << '\n')) {
            LOG_WARN("Preset {}: can't write {}", a_name, path.string());
            return false;
        }
        LOG_INFO("Preset {} saved to {} ({} setting(s) differ from the defaults)", a_name, path.string(), differing);
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
        m_humanizer.fatigueSkillPenalty = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "FatigueSkillPenalty", m_humanizer.fatigueSkillPenalty));
        m_humanizer.enablePanic = a_ini.GetBoolValue("Humanizer", "EnablePanic", m_humanizer.enablePanic);
        m_humanizer.panicHealthThreshold = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "PanicHealthThreshold", m_humanizer.panicHealthThreshold));
        m_humanizer.panicSkillPenalty = static_cast<float>(
            a_ini.GetDoubleValue("Humanizer", "PanicSkillPenalty", m_humanizer.panicSkillPenalty));

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
        m_dodgeSystem.forwardDodgeChance = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("DodgeSystem", "ForwardDodgeChance", m_dodgeSystem.forwardDodgeChance)), 0.0f, 1.0f);
        m_dodgeSystem.forwardDodgeCooldownSeconds = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("DodgeSystem", "ForwardDodgeCooldownSeconds", m_dodgeSystem.forwardDodgeCooldownSeconds)),
            0.0f, 60.0f);
        m_dodgeSystem.forwardDodgeMinStamina = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("DodgeSystem", "ForwardDodgeMinStamina", m_dodgeSystem.forwardDodgeMinStamina)), 0.0f, 1.0f);

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
        m_decisionMatrix.backoffAfterSwing =
            a_ini.GetBoolValue("DecisionMatrix", "BackoffAfterSwing", m_decisionMatrix.backoffAfterSwing);
        m_decisionMatrix.brainAdvanceDistance =
            a_ini.GetBoolValue("DecisionMatrix", "BrainAdvanceDistance", m_decisionMatrix.brainAdvanceDistance);
        m_decisionMatrix.backoffOnSwingChance = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "BackoffOnSwingChance", m_decisionMatrix.backoffOnSwingChance)),
            0.0f, 1.0f);
        m_decisionMatrix.backoffAfterSwingChance = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "BackoffAfterSwingChance", m_decisionMatrix.backoffAfterSwingChance)),
            0.0f, 1.0f);
        m_decisionMatrix.enablePersonality =
            a_ini.GetBoolValue("DecisionMatrix", "EnablePersonality", m_decisionMatrix.enablePersonality);
        m_decisionMatrix.enableCounter = a_ini.GetBoolValue("DecisionMatrix", "EnableCounter", m_decisionMatrix.enableCounter);
        m_decisionMatrix.enableShoutTiming =
            a_ini.GetBoolValue("DecisionMatrix", "EnableShoutTiming", m_decisionMatrix.enableShoutTiming);
        m_decisionMatrix.enableSelfShouts = a_ini.GetBoolValue("DecisionMatrix", "EnableSelfShouts", m_decisionMatrix.enableSelfShouts);
        m_decisionMatrix.selfShoutSlowTime = a_ini.GetBoolValue("DecisionMatrix", "SelfShoutSlowTime", m_decisionMatrix.selfShoutSlowTime);
        m_decisionMatrix.includeHumanoidCreatures =
            a_ini.GetBoolValue("DecisionMatrix", "IncludeHumanoidCreatures", m_decisionMatrix.includeHumanoidCreatures);
        const auto dmFloat = [&](const char *a_key, float &a_value, float a_min, float a_max) {
            a_value = std::clamp(static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", a_key, a_value)), a_min, a_max);
        };
        dmFloat("CreatureSwingSeconds", m_decisionMatrix.creatureSwingSeconds, 0.0f, 10.0f);
        dmFloat("PersonalityJitter", m_decisionMatrix.personalityJitter, 0.0f, 0.5f);
        dmFloat("TacticalRetreatMinCaution", m_decisionMatrix.tacticalRetreatMinCaution, 0.0f, 1.0f);
        m_decisionMatrix.enableTargetDanger =
            a_ini.GetBoolValue("DecisionMatrix", "EnableTargetDanger", m_decisionMatrix.enableTargetDanger);
        dmFloat("TargetDangerInfluence", m_decisionMatrix.targetDangerInfluence, 0.0f, 1.0f);
        dmFloat("TargetDangerStrengthWeight", m_decisionMatrix.targetDangerStrengthWeight, 0.0f, 1.0f);
        dmFloat("FlankingMaxReach", m_decisionMatrix.flankingMaxReach, 1.0f, 10.0f);
        dmFloat("ShotHoldMaxSeconds", m_decisionMatrix.shotHoldMaxSeconds, 0.0f, 30.0f);
        m_decisionMatrix.enableNPCPotions = a_ini.GetBoolValue("DecisionMatrix", "EnableNPCPotions", m_decisionMatrix.enableNPCPotions);
        dmFloat("PotionHealthThreshold", m_decisionMatrix.potionHealthThreshold, 0.0f, 1.0f);
        dmFloat("PotionMagickaThreshold", m_decisionMatrix.potionMagickaThreshold, 0.0f, 1.0f);
        dmFloat("PotionStaminaThreshold", m_decisionMatrix.potionStaminaThreshold, 0.0f, 1.0f);
        dmFloat("PotionCooldownSeconds", m_decisionMatrix.potionCooldownSeconds, 0.0f, 60.0f);
        m_decisionMatrix.enableBuffPotions = a_ini.GetBoolValue("DecisionMatrix", "EnableBuffPotions", m_decisionMatrix.enableBuffPotions);
        m_decisionMatrix.enableNPCPoisons = a_ini.GetBoolValue("DecisionMatrix", "EnableNPCPoisons", m_decisionMatrix.enableNPCPoisons);
        dmFloat("PoisonCooldownSeconds", m_decisionMatrix.poisonCooldownSeconds, 0.0f, 120.0f);
        dmFloat("PoisonParalysisCooldownSeconds", m_decisionMatrix.poisonParalysisCooldownSeconds, 0.0f, 300.0f);
        dmFloat("SafeDrinkDistance", m_decisionMatrix.safeDrinkDistance, 0.0f, 2000.0f);
        dmFloat("SafeDrinkMaxWaitSeconds", m_decisionMatrix.safeDrinkMaxWaitSeconds, 0.0f, 30.0f);
        m_decisionMatrix.tradeBlowsChance = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "TradeBlowsChance", m_decisionMatrix.tradeBlowsChance)),
            0.0f, 1.0f);
        m_decisionMatrix.closeDodgeChance = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "CloseDodgeChance", m_decisionMatrix.closeDodgeChance)),
            0.0f, 1.0f);
        m_decisionMatrix.allowDaggerBlock =
            a_ini.GetBoolValue("DecisionMatrix", "AllowDaggerBlock", m_decisionMatrix.allowDaggerBlock);
        m_decisionMatrix.enableRangedPositioning =
            a_ini.GetBoolValue("DecisionMatrix", "EnableRangedPositioning", m_decisionMatrix.enableRangedPositioning);
        m_decisionMatrix.rangedKeepAwayDistance = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "RangedKeepAwayDistance", m_decisionMatrix.rangedKeepAwayDistance)),
            100.0f, 2000.0f);
        m_decisionMatrix.rangedMaxDistance = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "RangedMaxDistance", m_decisionMatrix.rangedMaxDistance)),
            m_decisionMatrix.rangedKeepAwayDistance + 200.0f, 8000.0f);
        m_decisionMatrix.defendOnSwingChance = std::clamp(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "DefendOnSwingChance", m_decisionMatrix.defendOnSwingChance)),
            0.0f, 1.0f);
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
        m_decisionMatrix.sprintAttackLaunchDistance = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("DecisionMatrix", "SprintAttackLaunchDistance",
                                                    m_decisionMatrix.sprintAttackLaunchDistance)),
            0.0f, 1000.0f);
        m_decisionMatrix.sprintAttackMinDistance = static_cast<float>(a_ini.GetDoubleValue(
            "DecisionMatrix", "SprintAttackMinDistance", m_decisionMatrix.sprintAttackMinDistance));
        m_decisionMatrix.sprintAttackMaxDistance = static_cast<float>(a_ini.GetDoubleValue(
            "DecisionMatrix", "SprintAttackMaxDistance", m_decisionMatrix.sprintAttackMaxDistance));
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
        m_modIntegrations.enableComboFollowUp =
            a_ini.GetBoolValue("ModIntegrations", "EnableComboFollowUp", m_modIntegrations.enableComboFollowUp);
        m_modIntegrations.comboFollowUpChance = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("ModIntegrations", "ComboFollowUpChance", m_modIntegrations.comboFollowUpChance)),
            0.0f, 1.0f);
        m_modIntegrations.comboReachMult = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("ModIntegrations", "ComboReachMult", m_modIntegrations.comboReachMult)),
            1.0f, 3.0f);
        m_modIntegrations.comboPowerChance = (std::clamp)(
            static_cast<float>(a_ini.GetDoubleValue("ModIntegrations", "ComboPowerChance", m_modIntegrations.comboPowerChance)),
            0.0f, 1.0f);
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

        // Clamp values
        m_parry.parryWindowStart = (std::max)(0.0f, m_parry.parryWindowStart);
        m_parry.parryWindowEnd = (std::max)(m_parry.parryWindowStart, m_parry.parryWindowEnd);
        m_parry.parryMinDistance = (std::max)(0.0f, m_parry.parryMinDistance);
        m_parry.parryMaxDistance = (std::max)(m_parry.parryMinDistance, m_parry.parryMaxDistance);
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

        // Clamp values
        m_timedBlock.timedBlockWindowStart = (std::max)(0.0f, m_timedBlock.timedBlockWindowStart);
        m_timedBlock.timedBlockWindowEnd =
            (std::max)(m_timedBlock.timedBlockWindowStart, m_timedBlock.timedBlockWindowEnd);
        m_timedBlock.timedBlockMinDistance = (std::max)(0.0f, m_timedBlock.timedBlockMinDistance);
        m_timedBlock.timedBlockMaxDistance =
            (std::max)(m_timedBlock.timedBlockMinDistance, m_timedBlock.timedBlockMaxDistance);
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
        m_combatPacing.dangerExtraAttackers = std::clamp(
            static_cast<int>(a_ini.GetLongValue("CombatPacing", "DangerExtraAttackers", m_combatPacing.dangerExtraAttackers)), 0, 6);
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
        m_damageMomentum.enableDamageRace =
            a_ini.GetBoolValue("DamageMomentum", "EnableDamageRace", m_damageMomentum.enableDamageRace);
        m_damageMomentum.raceRetreatHits = ClampValue(
            static_cast<float>(a_ini.GetDoubleValue("DamageMomentum", "RaceRetreatHits", m_damageMomentum.raceRetreatHits)),
            0.0f, 10.0f);
        m_damageMomentum.raceRetreatMaxHealth = ClampValue(
            static_cast<float>(a_ini.GetDoubleValue("DamageMomentum", "RaceRetreatMaxHealth", m_damageMomentum.raceRetreatMaxHealth)),
            0.0f, 1.0f);
        m_damageMomentum.racePressBonus = ClampValue(
            static_cast<float>(a_ini.GetDoubleValue("DamageMomentum", "RacePressBonus", m_damageMomentum.racePressBonus)),
            0.0f, 2.0f);
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
        // [Boss] is read here too (no separate reader for two keys)
        m_boss.enableBossDetection = a_ini.GetBoolValue("Boss", "EnableBossDetection", m_boss.enableBossDetection);
        m_boss.bossNames = a_ini.GetValue("Boss", "BossNames", m_boss.bossNames.c_str());
        const auto bossFloat = [&](const char *a_key, float &a_value, float a_min, float a_max) {
            a_value = ClampValue(static_cast<float>(a_ini.GetDoubleValue("Boss", a_key, a_value)), a_min, a_max);
        };
        bossFloat("EnragedHealth", m_boss.enragedHealth, 0.0f, 1.0f);
        bossFloat("DesperateHealth", m_boss.desperateHealth, 0.0f, 1.0f);
        bossFloat("CautionFloor", m_boss.cautionFloor, 0.0f, 1.0f);
        bossFloat("SkillBonus", m_boss.skillBonus, 0.0f, 1.0f);
        bossFloat("PowerStaminaMin", m_boss.powerStaminaMin, 0.0f, 1.0f);
        bossFloat("GuardBreakBlockSeconds", m_boss.guardBreakBlockSeconds, 0.0f, 10.0f);
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
} // namespace CombatAI

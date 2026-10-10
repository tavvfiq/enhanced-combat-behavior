#include "ConfigMenu.h"
#include "CombatDirector.h"
#include "Config.h"
#include "Logger.h"
#include "pch.h"

// The vendored SKSE Menu Framework header uses <codecvt> (deprecated in C++17)
// and has a few enum/struct-tag quirks. Silence those third-party warnings locally.
#pragma warning(push)
#pragma warning(disable : 4996 4099 5054)
#include "API/SKSEMenuFramework.h"
#pragma warning(pop)

namespace CombatAI
{
    void ConfigMenu::Register()
    {
        if (!SKSEMenuFramework::IsInstalled()) {
            LOG_INFO("SKSE Menu Framework not installed; runtime config menu disabled");
            return;
        }
        SKSEMenuFramework::SetSection("Enhanced Combat AI");
        SKSEMenuFramework::AddSectionItem("Config", ConfigMenu::Render);
        LOG_INFO("Registered runtime config menu (SKSE Menu Framework)");
    }

    void __stdcall ConfigMenu::Render()
    {
        auto &config = Config::GetInstance();

        // Humanizer + general fields are copied into the Humanizer/director, so mark
        // them dirty on change; CombatDirector re-applies on the game thread.
        auto humanFloat = [&](const char *label, float *v, float lo, float hi, const char *fmt = "%.2f") {
            if (ImGuiMCP::SliderFloat(label, v, lo, hi, fmt)) {
                config.MarkHumanizerDirty();
            }
        };
        // Decision-matrix / scoring fields are read live every evaluation, so plain
        // sliders/checkboxes take effect immediately with no dirty flag needed.
        auto liveFloat = [&](const char *label, float *v, float lo, float hi, const char *fmt = "%.2f") {
            ImGuiMCP::SliderFloat(label, v, lo, hi, fmt);
        };
        // spdlog's level is global and was fixed at plugin load, so toggling the
        // debug-log bool alone does nothing until we re-apply the level here.
        auto applyLogLevel = [&]() {
            auto lvl = config.m_general.enableDebugLog ? spdlog::level::debug : spdlog::level::info;
            spdlog::set_level(lvl);
            spdlog::flush_on(lvl);
        };

        if (ImGuiMCP::BeginTabBar("ECAConfigTabs")) {
            if (ImGuiMCP::BeginTabItem("General")) {
                ImGuiMCP::Checkbox("Enable plugin", &config.m_general.enablePlugin);
                if (ImGuiMCP::Checkbox("Enable debug log", &config.m_general.enableDebugLog)) {
                    applyLogLevel();
                }
                humanFloat("Processing interval (s)", &config.m_general.processingInterval, 0.01f, 1.0f, "%.2f");
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Humanizer")) {
                humanFloat("Base reaction delay (ms)", &config.m_humanizer.baseReactionDelayMs, 0.0f, 500.0f, "%.0f");
                humanFloat("Reaction variance (ms)", &config.m_humanizer.reactionVarianceMs, 0.0f, 300.0f, "%.0f");
                humanFloat("Reaction reduction/level (ms)", &config.m_humanizer.reactionDelayReductionPerLevelMs, 0.0f,
                           10.0f);
                humanFloat("Min reaction delay (ms)", &config.m_humanizer.minReactionDelayMs, 0.0f, 300.0f, "%.0f");
                ImGuiMCP::Separator();
                humanFloat("Base mistake chance", &config.m_humanizer.baseMistakeChance, 0.0f, 1.0f);
                humanFloat("Mistake reduction/level", &config.m_humanizer.mistakeChanceReductionPerLevel, 0.0f, 0.1f,
                           "%.3f");
                humanFloat("Min mistake chance", &config.m_humanizer.minMistakeChance, 0.0f, 1.0f);
                ImGuiMCP::Separator();
                humanFloat("Bash cooldown (s)", &config.m_humanizer.bashCooldownSeconds, 0.0f, 10.0f);
                humanFloat("Dodge cooldown (s)", &config.m_humanizer.dodgeCooldownSeconds, 0.0f, 10.0f);
                humanFloat("Jump cooldown (s)", &config.m_humanizer.jumpCooldownSeconds, 0.0f, 10.0f);
                if (ImGuiMCP::CollapsingHeader("Mistake multipliers")) {
                    humanFloat("Bash", &config.m_humanizer.bashMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Dodge", &config.m_humanizer.dodgeMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Jump", &config.m_humanizer.jumpMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Strafe", &config.m_humanizer.strafeMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Power attack", &config.m_humanizer.powerAttackMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Attack", &config.m_humanizer.attackMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Sprint attack", &config.m_humanizer.sprintAttackMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Retreat", &config.m_humanizer.retreatMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Backoff", &config.m_humanizer.backoffMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Advancing", &config.m_humanizer.advancingMistakeMultiplier, 0.0f, 3.0f);
                    humanFloat("Flanking", &config.m_humanizer.flankingMistakeMultiplier, 0.0f, 3.0f);
                }
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Dodge")) {
                liveFloat("Dodge stamina cost", &config.m_dodgeSystem.dodgeStaminaCost, 0.0f, 100.0f, "%.0f");
                liveFloat("I-frame duration (s)", &config.m_dodgeSystem.iFrameDuration, 0.0f, 1.0f);
                ImGuiMCP::Checkbox("Enable step dodge", &config.m_dodgeSystem.enableStepDodge);
                ImGuiMCP::Checkbox("Enable dodge attack-cancel", &config.m_dodgeSystem.enableDodgeAttackCancel);
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Decision")) {
                ImGuiMCP::Checkbox("Enable offense", &config.m_decisionMatrix.enableOffense);
                ImGuiMCP::Checkbox("Enable evasion dodge", &config.m_decisionMatrix.enableEvasionDodge);
                ImGuiMCP::Checkbox("Enable survival retreat", &config.m_decisionMatrix.enableSurvivalRetreat);
                ImGuiMCP::Checkbox("Enable jump evasion", &config.m_decisionMatrix.enableJumpEvasion);
                ImGuiMCP::Checkbox("Enable sprint attack", &config.m_decisionMatrix.enableSprintAttack);
                ImGuiMCP::Checkbox("Enable sprint charge (gap-close)", &config.m_decisionMatrix.enableSprintCharge);
                ImGuiMCP::Separator();
                liveFloat("Offense reach multiplier", &config.m_decisionMatrix.offenseReachMultiplier, 0.5f, 3.0f);
                liveFloat("Offense reach safety margin", &config.m_decisionMatrix.offenseReachSafetyMargin, 0.0f,
                          100.0f, "%.0f");
                liveFloat("Interrupt reach multiplier", &config.m_decisionMatrix.interruptReachMultiplier, 0.5f, 3.0f);
                liveFloat("Evasion min distance", &config.m_decisionMatrix.evasionMinDistance, 0.0f, 600.0f, "%.0f");
                liveFloat("Sprint attack min dist", &config.m_decisionMatrix.sprintAttackMinDistance, 0.0f, 800.0f,
                          "%.0f");
                liveFloat("Sprint attack max dist", &config.m_decisionMatrix.sprintAttackMaxDistance, 0.0f, 1200.0f,
                          "%.0f");
                ImGuiMCP::Separator();
                liveFloat("Stamina threshold", &config.m_decisionMatrix.staminaThreshold, 0.0f, 1.0f);
                liveFloat("Health threshold", &config.m_decisionMatrix.healthThreshold, 0.0f, 1.0f);
                ImGuiMCP::Checkbox("Power attack stamina check",
                                   &config.m_decisionMatrix.enablePowerAttackStaminaCheck);
                ImGuiMCP::Checkbox("Sprint attack stamina check",
                                   &config.m_decisionMatrix.enableSprintAttackStaminaCheck);
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Weights")) {
                ImGuiMCP::Text("Base priorities (which action wins)");
                liveFloat("Interrupt power attack", &config.m_scoringWeights.interruptPowerAttackBase, 0.0f, 5.0f);
                liveFloat("Evasion / dodge", &config.m_scoringWeights.evasionDodgeBase, 0.0f, 5.0f);
                liveFloat("Advancing", &config.m_scoringWeights.advancingBase, 0.0f, 5.0f);
                liveFloat("Sprint attack", &config.m_scoringWeights.sprintAttackBase, 0.0f, 5.0f);
                liveFloat("Attack", &config.m_scoringWeights.attackBase, 0.0f, 5.0f);
                liveFloat("Backoff", &config.m_scoringWeights.backoffBase, 0.0f, 5.0f);
                liveFloat("Flanking", &config.m_scoringWeights.flankingBase, 0.0f, 5.0f);
                ImGuiMCP::Separator();
                ImGuiMCP::Text("Offense modifiers");
                liveFloat("Target staggered", &config.m_scoringWeights.targetStaggeredBonus, 0.0f, 3.0f);
                liveFloat("Target casting/drawing", &config.m_scoringWeights.targetCastingBonus, 0.0f, 3.0f);
                liveFloat("Target recovery", &config.m_scoringWeights.targetRecoveryBonus, 0.0f, 3.0f);
                liveFloat("Target fleeing", &config.m_scoringWeights.targetFleeingBonus, 0.0f, 3.0f);
                liveFloat("Target low-health finisher", &config.m_scoringWeights.targetLowHealthFinisherBonus, 0.0f,
                          3.0f);
                liveFloat("Opening risk penalty", &config.m_scoringWeights.openingRiskPenalty, 0.0f, 3.0f);
                liveFloat("Flanking attack bonus", &config.m_scoringWeights.flankingAttackBonus, 0.0f, 3.0f);
                liveFloat("Ally cover bonus", &config.m_scoringWeights.allyCoverBonus, 0.0f, 3.0f);
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Performance")) {
                ImGuiMCP::Checkbox("Only process combat actors", &config.m_performance.onlyProcessCombatActors);
                liveFloat("Cleanup interval (s)", &config.m_performance.cleanupInterval, 1.0f, 30.0f, "%.1f");
                ImGuiMCP::Separator();
                ImGuiMCP::Text("Adaptive processing interval (LOD)");
                liveFloat("Near distance", &config.m_performance.distanceNear, 0.0f, 5000.0f, "%.0f");
                liveFloat("Mid distance", &config.m_performance.distanceMid, 0.0f, 8000.0f, "%.0f");
                liveFloat("Mid interval (s)", &config.m_performance.processingIntervalMid, 0.05f, 2.0f, "%.2f");
                liveFloat("Far interval (s)", &config.m_performance.processingIntervalFar, 0.05f, 2.0f, "%.2f");
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Parry")) {
                ImGuiMCP::Text("Requires EldenParry. Applied live.");
                ImGuiMCP::Checkbox("Enable parry", &config.m_parry.enableParry);
                liveFloat("Window start (s)", &config.m_parry.parryWindowStart, 0.0f, 0.5f, "%.3f");
                liveFloat("Window end (s)", &config.m_parry.parryWindowEnd, 0.0f, 0.5f, "%.3f");
                liveFloat("Min distance", &config.m_parry.parryMinDistance, 0.0f, 300.0f, "%.0f");
                liveFloat("Max distance", &config.m_parry.parryMaxDistance, 0.0f, 400.0f, "%.0f");
                liveFloat("Base priority", &config.m_parry.parryBasePriority, 0.0f, 3.0f);
                liveFloat("Timing bonus max", &config.m_parry.timingBonusMax, 0.0f, 1.0f);
                liveFloat("Early bash penalty", &config.m_parry.earlyBashPenalty, 0.0f, 1.0f);
                liveFloat("Late bash penalty", &config.m_parry.lateBashPenalty, 0.0f, 1.0f);
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("TimedBlock")) {
                ImGuiMCP::Text("Requires Simple Timed Block. Applied live.");
                ImGuiMCP::Checkbox("Enable timed block", &config.m_timedBlock.enableTimedBlock);
                liveFloat("Window start (s)", &config.m_timedBlock.timedBlockWindowStart, 0.0f, 0.5f, "%.3f");
                liveFloat("Window end (s)", &config.m_timedBlock.timedBlockWindowEnd, 0.0f, 0.5f, "%.3f");
                liveFloat("Min distance", &config.m_timedBlock.timedBlockMinDistance, 0.0f, 300.0f, "%.0f");
                liveFloat("Max distance", &config.m_timedBlock.timedBlockMaxDistance, 0.0f, 400.0f, "%.0f");
                liveFloat("Base priority", &config.m_timedBlock.timedBlockBasePriority, 0.0f, 3.0f);
                liveFloat("Timing bonus max", &config.m_timedBlock.timedBlockTimingBonusMax, 0.0f, 1.0f);
                liveFloat("Early penalty", &config.m_timedBlock.timedBlockEarlyPenalty, 0.0f, 1.0f);
                liveFloat("Late penalty", &config.m_timedBlock.timedBlockLatePenalty, 0.0f, 1.0f);
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Pacing")) {
                ImGuiMCP::Text("Combat Pacing - cap concurrent attackers.");
                ImGuiMCP::Checkbox("Enable combat pacing", &config.m_combatPacing.enableCombatPacing);
                ImGuiMCP::SliderInt("Max simultaneous attackers", &config.m_combatPacing.maxSimultaneousAttackers, 1, 6);
                liveFloat("Slot window min (s)", &config.m_combatPacing.slotWindowMinSeconds, 0.5f, 10.0f, "%.1f");
                liveFloat("Slot window max (s)", &config.m_combatPacing.slotWindowMaxSeconds, 0.5f, 12.0f, "%.1f");
                ImGuiMCP::Checkbox("Pace player target only", &config.m_combatPacing.paceTargetPlayerOnly);
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Momentum")) {
                ImGuiMCP::Text("Aggression scales with how hard recent hits land.");
                ImGuiMCP::Checkbox("Enable damage momentum", &config.m_damageMomentum.enableDamageMomentum);
                liveFloat("Big-hit threshold (frac maxHP)", &config.m_damageMomentum.bigHitThreshold, 0.01f, 0.5f,
                          "%.3f");
                liveFloat("Chip threshold (frac maxHP)", &config.m_damageMomentum.chipThreshold, 0.0f, 0.3f, "%.3f");
                liveFloat("Offensive bonus max", &config.m_damageMomentum.offensiveBonusMax, 0.0f, 2.0f);
                liveFloat("Defensive penalty max", &config.m_damageMomentum.defensivePenaltyMax, 0.0f, 2.0f);
                liveFloat("Momentum decay (s)", &config.m_damageMomentum.momentumDecaySeconds, 0.5f, 20.0f, "%.1f");
                liveFloat("EWMA alpha", &config.m_damageMomentum.ewmaAlpha, 0.05f, 1.0f);
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("EnvSense")) {
                ImGuiMCP::Text("Raycast spatial awareness. Applied live.");
                ImGuiMCP::Checkbox("Enable LoS attack gate", &config.m_envSensor.enableLosGate);
                liveFloat("LoS eye height", &config.m_envSensor.losEyeHeight, 0.0f, 300.0f, "%.0f");
                ImGuiMCP::Separator();
                ImGuiMCP::Checkbox("Enable cliff-safe dodge", &config.m_envSensor.enableCliffSafeDodge);
                liveFloat("Dodge ledge probe dist", &config.m_envSensor.dodgeLedgeProbeDistance, 0.0f, 500.0f, "%.0f");
                liveFloat("Dodge max drop (cliff)", &config.m_envSensor.dodgeMaxDrop, 0.0f, 800.0f, "%.0f");
                ImGuiMCP::EndTabItem();
            }

            if (ImGuiMCP::BeginTabItem("Integrations")) {
                ImGuiMCP::Text("Applied on game load - restart to re-detect mods.");
                ImGuiMCP::Checkbox("CPR integration", &config.m_modIntegrations.enableCPRIntegration);
                ImGuiMCP::Checkbox("BFCO integration", &config.m_modIntegrations.enableBFCOIntegration);
                ImGuiMCP::Checkbox("Precision integration", &config.m_modIntegrations.enablePrecisionIntegration);
                ImGuiMCP::Checkbox("TK Dodge integration", &config.m_modIntegrations.enableTKDodgeIntegration);
                ImGuiMCP::EndTabItem();
            }

            ImGuiMCP::EndTabBar();
        }

        ImGuiMCP::Separator();
        if (ImGuiMCP::Button("Save to INI")) {
            config.Save();
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Reload from INI")) {
            config.Load();
            config.MarkHumanizerDirty();
            applyLogLevel();
        }
    }
} // namespace CombatAI

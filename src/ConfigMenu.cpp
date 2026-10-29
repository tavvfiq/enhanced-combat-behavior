#include "ConfigMenu.h"
#include "CombatDirector.h"
#include "Config.h"
#include "Logger.h"
#include "Personality.h"
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
        // The header resolves the module at our DLL load, before SKSE loads SKSEMenuFramework.dll
        // (plugins load alphabetically); resolve it again now or every call silently does nothing.
        menuFramework = REX::W32::GetModuleHandleW(L"SKSEMenuFramework");
        if (!menuFramework || !GetProcAddress(menuFramework, "AddSectionItem")) {
            LOG_ERROR("SKSE Menu Framework is installed but not loaded (or has no AddSectionItem); runtime config menu disabled");
            return;
        }
        SKSEMenuFramework::SetSection("Enhanced Combat Behavior");
        SKSEMenuFramework::AddSectionItem("Config", ConfigMenu::Render);
        LOG_INFO("Registered runtime config menu (SKSE Menu Framework: Enhanced Combat Behavior / Config)");
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

        // A preset resets all tuning (not General's switches, performance or integrations) to the defaults, then applies
        // its own changes; Save to keep it. The list (Default plus one entry per JSON file in the Presets folder, read
        // again on reload/save) only chooses; Load applies the choice. A loaded preset's name goes to the save field
        // (not Default's), so a tweaked preset is one click from being overwritten.
        static std::vector<std::string> presets = Config::ListPresets();
        static std::string selected; // chosen in the list; empty after a reload
        static char presetName[65] = "";
        ImGuiMCP::Text("Preset (resets tuning, then applies):");
        ImGuiMCP::SameLine();
        ImGuiMCP::SetNextItemWidth(220.0f);
        if (ImGuiMCP::BeginCombo("##Preset", selected.empty() ? "Choose..." : selected.c_str())) {
            if (ImGuiMCP::Selectable("Default", selected == "Default")) {
                selected = "Default";
            }
            for (const auto &name : presets) {
                if (ImGuiMCP::Selectable(name.c_str(), selected == name)) {
                    selected = name;
                }
            }
            ImGuiMCP::EndCombo();
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::BeginDisabled(selected.empty());
        if (ImGuiMCP::Button("Load") && config.ApplyPreset(selected) && selected != "Default") {
            strncpy_s(presetName, selected.c_str(), _TRUNCATE);
        }
        ImGuiMCP::EndDisabled();
        ImGuiMCP::InputText("##PresetName", presetName, sizeof(presetName));
        ImGuiMCP::SameLine();
        const bool exists = std::ranges::find(presets, std::string(presetName)) != presets.end();
        if (ImGuiMCP::Button(exists ? "Overwrite preset" : "Save as preset") && config.SavePreset(presetName)) {
            presets = Config::ListPresets();
            selected = presetName;
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::TextDisabled("current tuning, what differs from the defaults");
        ImGuiMCP::Separator();

        // Five tabs: the settings people change sit on top, the rest under collapsed "Advanced" headers.
        if (ImGuiMCP::BeginTabBar("ECBConfigTabs")) {
            // ---- General: master switches, debug, integrations, performance
            if (ImGuiMCP::BeginTabItem("General")) {
                ImGuiMCP::Checkbox("Enable plugin", &config.m_general.enablePlugin);
                if (ImGuiMCP::Checkbox("Enable debug log (select an NPC in the console to follow it)", &config.m_general.enableDebugLog)) {
                    applyLogLevel();
                }
                ImGuiMCP::Checkbox("Include humanoid creatures (draugr, falmer, skeletons)", &config.m_decisionMatrix.includeHumanoidCreatures);
                if (ImGuiMCP::CollapsingHeader("Integrations")) {
                    ImGuiMCP::Text("Precision and TK Dodge are detected on game load (restart to re-detect).");
                    ImGuiMCP::Checkbox("Precision integration", &config.m_modIntegrations.enablePrecisionIntegration);
                    ImGuiMCP::Checkbox("TK Dodge integration (dodges)", &config.m_modIntegrations.enableTKDodgeIntegration);
                    ImGuiMCP::Checkbox("SCAR integration (when SCAR.dll is loaded)", &config.m_modIntegrations.enableSCARIntegration);
                    liveFloat("SCAR combo window (s)", &config.m_modIntegrations.scarComboWindowSeconds, 1.0f, 120.0f);
                    liveFloat("SCAR stall fallback (s)", &config.m_modIntegrations.scarStallSeconds, 0.5f, 15.0f);
                }
                if (ImGuiMCP::CollapsingHeader("Performance")) {
                    humanFloat("Processing interval (s)", &config.m_general.processingInterval, 0.01f, 1.0f, "%.2f");
                    liveFloat("Cleanup interval (s)", &config.m_performance.cleanupInterval, 1.0f, 30.0f, "%.1f");
                    liveFloat("Near distance", &config.m_performance.distanceNear, 0.0f, 5000.0f, "%.0f");
                    liveFloat("Mid distance", &config.m_performance.distanceMid, 0.0f, 8000.0f, "%.0f");
                    liveFloat("Mid interval (s)", &config.m_performance.processingIntervalMid, 0.05f, 2.0f, "%.2f");
                    liveFloat("Far interval (s)", &config.m_performance.processingIntervalFar, 0.05f, 2.0f, "%.2f");
                }
                ImGuiMCP::EndTabItem();
            }

            // ---- Offense: attacking, combos, sprint attacks, feints
            if (ImGuiMCP::BeginTabItem("Offense")) {
                ImGuiMCP::Checkbox("Enable offense", &config.m_decisionMatrix.enableOffense);
                ImGuiMCP::Checkbox("Enable sprint attack", &config.m_decisionMatrix.enableSprintAttack);
                ImGuiMCP::Checkbox("Time shouts (stagger an open target, disarm an armed one)", &config.m_decisionMatrix.enableShoutTiming);
                ImGuiMCP::Checkbox("NPCs poison their weapons", &config.m_decisionMatrix.enableNPCPoisons);
                liveFloat("Poison cooldown (s)", &config.m_decisionMatrix.poisonCooldownSeconds, 0.0f, 120.0f, "%.0f");
                ImGuiMCP::Checkbox("Enable combo follow-up (MCO / BFCO, without SCAR)", &config.m_modIntegrations.enableComboFollowUp);
                liveFloat("Combo follow-up chance", &config.m_modIntegrations.comboFollowUpChance, 0.0f, 1.0f);
                liveFloat("Combo power follow-up chance", &config.m_modIntegrations.comboPowerChance, 0.0f, 1.0f);
                liveFloat("Trade blows with a swing (chance)", &config.m_decisionMatrix.tradeBlowsChance, 0.0f, 1.0f);
                liveFloat("Power attack min stamina", &config.m_decisionMatrix.powerAttackMinStamina, 0.0f, 1.0f);
                if (ImGuiMCP::CollapsingHeader("Advanced: reach and sprint attacks")) {
                    liveFloat("Offense reach multiplier", &config.m_decisionMatrix.offenseReachMultiplier, 0.5f, 3.0f);
                    liveFloat("Offense reach safety margin", &config.m_decisionMatrix.offenseReachSafetyMargin, 0.0f, 100.0f, "%.0f");
                    liveFloat("Combo follow-up reach x attack range", &config.m_modIntegrations.comboReachMult, 1.0f, 3.0f);
                    liveFloat("Sprint attack min dist", &config.m_decisionMatrix.sprintAttackMinDistance, 0.0f, 800.0f, "%.0f");
                    liveFloat("Sprint attack max dist", &config.m_decisionMatrix.sprintAttackMaxDistance, 0.0f, 1200.0f, "%.0f");
                    liveFloat("Sprint attack launch dist (0 = 3x reach)", &config.m_decisionMatrix.sprintAttackLaunchDistance, 0.0f, 1000.0f, "%.0f");
                    ImGuiMCP::Checkbox("Power attack stamina check", &config.m_decisionMatrix.enablePowerAttackStaminaCheck);
                    ImGuiMCP::Checkbox("Sprint attack stamina check", &config.m_decisionMatrix.enableSprintAttackStaminaCheck);
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: vanilla attack shaping")) {
                    ImGuiMCP::Checkbox("Enable vanilla attack shaping", &config.m_vanillaAttacks.enable);
                    liveFloat("Opening: power attack x", &config.m_vanillaAttacks.openingPowerMult, 0.0f, 5.0f);
                    liveFloat("Opening: normal attack x", &config.m_vanillaAttacks.openingNormalMult, 0.0f, 5.0f);
                    liveFloat("Target blocking: power attack x", &config.m_vanillaAttacks.blockedPowerMult, 0.0f, 5.0f);
                    liveFloat("Target blocking: normal attack x", &config.m_vanillaAttacks.blockedNormalMult, 0.0f, 5.0f);
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: action weights")) {
                    liveFloat("Attack", &config.m_scoringWeights.attackBase, 0.0f, 5.0f);
                    liveFloat("Sprint attack", &config.m_scoringWeights.sprintAttackBase, 0.0f, 5.0f);
                    liveFloat("Interrupt power attack", &config.m_scoringWeights.interruptPowerAttackBase, 0.0f, 5.0f);
                    liveFloat("Target staggered", &config.m_scoringWeights.targetStaggeredBonus, 0.0f, 3.0f);
                    liveFloat("Target casting/drawing", &config.m_scoringWeights.targetCastingBonus, 0.0f, 3.0f);
                    liveFloat("Target recovery", &config.m_scoringWeights.targetRecoveryBonus, 0.0f, 3.0f);
                    liveFloat("Target fleeing", &config.m_scoringWeights.targetFleeingBonus, 0.0f, 3.0f);
                    liveFloat("Target low-health finisher", &config.m_scoringWeights.targetLowHealthFinisherBonus, 0.0f, 3.0f);
                    liveFloat("Opening risk penalty", &config.m_scoringWeights.openingRiskPenalty, 0.0f, 3.0f);
                    liveFloat("Flanking attack bonus", &config.m_scoringWeights.flankingAttackBonus, 0.0f, 3.0f);
                    liveFloat("Ally cover bonus", &config.m_scoringWeights.allyCoverBonus, 0.0f, 3.0f);
                }
                ImGuiMCP::EndTabItem();
            }

            // ---- Defense: parry, timed block, counter, dodges, backing off
            if (ImGuiMCP::BeginTabItem("Defense")) {
                ImGuiMCP::Text("Vanilla block / bash / attack, timed by the brain. Parry and timed-block effects come from");
                ImGuiMCP::Text("mods (EldenParry, Simple Timed Block); without them these are well-timed bashes and blocks.");
                ImGuiMCP::Checkbox("Parry (timed bash)", &config.m_parry.enableParry);
                ImGuiMCP::SameLine();
                ImGuiMCP::Checkbox("Timed block", &config.m_timedBlock.enableTimedBlock);
                ImGuiMCP::SameLine();
                ImGuiMCP::Checkbox("Counter after a blocked hit", &config.m_decisionMatrix.enableCounter);
                liveFloat("Parry / timed block a swing (chance)", &config.m_decisionMatrix.defendOnSwingChance, 0.0f, 1.0f);
                liveFloat("Dodge a close swing (chance)", &config.m_decisionMatrix.closeDodgeChance, 0.0f, 1.0f);
                liveFloat("Back off from a swing (chance)", &config.m_decisionMatrix.backoffOnSwingChance, 0.0f, 1.0f);
                liveFloat("Step out after own swing (chance)", &config.m_decisionMatrix.backoffAfterSwingChance, 0.0f, 1.0f);
                ImGuiMCP::Checkbox("Enable survival retreat", &config.m_decisionMatrix.enableSurvivalRetreat);
                ImGuiMCP::SameLine();
                ImGuiMCP::Checkbox("NPCs back off, then drink potions", &config.m_decisionMatrix.enableNPCPotions);
                if (ImGuiMCP::CollapsingHeader("Advanced: parry and timed block windows")) {
                    liveFloat("Parry window start (s)", &config.m_parry.parryWindowStart, 0.0f, 0.5f, "%.3f");
                    liveFloat("Parry window end (s)", &config.m_parry.parryWindowEnd, 0.0f, 0.5f, "%.3f");
                    liveFloat("Parry min distance", &config.m_parry.parryMinDistance, 0.0f, 300.0f, "%.0f");
                    liveFloat("Parry max distance", &config.m_parry.parryMaxDistance, 0.0f, 400.0f, "%.0f");
                    liveFloat("Timed block window start (s)", &config.m_timedBlock.timedBlockWindowStart, 0.0f, 0.5f, "%.3f");
                    liveFloat("Timed block window end (s)", &config.m_timedBlock.timedBlockWindowEnd, 0.0f, 0.5f, "%.3f");
                    liveFloat("Timed block min distance", &config.m_timedBlock.timedBlockMinDistance, 0.0f, 300.0f, "%.0f");
                    liveFloat("Timed block max distance", &config.m_timedBlock.timedBlockMaxDistance, 0.0f, 400.0f, "%.0f");
                    liveFloat("Interrupt reach multiplier", &config.m_decisionMatrix.interruptReachMultiplier, 0.5f, 3.0f);
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: dodges (TK Dodge)")) {
                    liveFloat("Forward dodge chance", &config.m_dodgeSystem.forwardDodgeChance, 0.0f, 1.0f);
                    liveFloat("Forward dodge cooldown (s)", &config.m_dodgeSystem.forwardDodgeCooldownSeconds, 0.0f, 60.0f, "%.1f");
                    liveFloat("Forward dodge min stamina", &config.m_dodgeSystem.forwardDodgeMinStamina, 0.0f, 1.0f);
                    liveFloat("Forward dodge: max gap beyond reach", &config.m_dodgeSystem.forwardDodgeMaxGap, 0.0f, 1000.0f, "%.0f");
                    liveFloat("Forward dodge: counter window (s)", &config.m_dodgeSystem.forwardDodgeCounterWindowSeconds, 0.0f, 5.0f);
                    liveFloat("Dodge stamina cost", &config.m_dodgeSystem.dodgeStaminaCost, 0.0f, 100.0f, "%.0f");
                    liveFloat("I-frame duration (s)", &config.m_dodgeSystem.iFrameDuration, 0.0f, 1.0f);
                    ImGuiMCP::Checkbox("Enable step dodge", &config.m_dodgeSystem.enableStepDodge);
                    ImGuiMCP::Checkbox("Enable dodge attack-cancel", &config.m_dodgeSystem.enableDodgeAttackCancel);
                    ImGuiMCP::Checkbox("Enable jump evasion", &config.m_decisionMatrix.enableJumpEvasion);
                    liveFloat("Jump height", &config.m_decisionMatrix.jumpHeight, 20.0f, 300.0f, "%.0f");
                    liveFloat("Jump stamina cost", &config.m_decisionMatrix.jumpStaminaCost, 0.0f, 100.0f, "%.0f");
                    liveFloat("Evasion min distance", &config.m_decisionMatrix.evasionMinDistance, 0.0f, 600.0f, "%.0f");
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: projectiles")) {
                    ImGuiMCP::Checkbox("Enable projectile dodge", &config.m_projectileAwareness.enableProjectileDodge);
                    liveFloat("Projectile: react from (s)", &config.m_projectileAwareness.reactMinSeconds, 0.0f, 2.0f);
                    liveFloat("Projectile: react until (s)", &config.m_projectileAwareness.reactMaxSeconds, 0.05f, 3.0f);
                    liveFloat("Projectile: dodge chance", &config.m_projectileAwareness.dodgeChance, 0.0f, 1.0f);
                    liveFloat("Projectile: hit margin", &config.m_projectileAwareness.hitMargin, 0.0f, 200.0f, "%.0f");
                    liveFloat("Projectile: dodge priority", &config.m_projectileAwareness.dodgePriority, 0.0f, 5.0f);
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: retreat and back off")) {
                    liveFloat("Health threshold", &config.m_decisionMatrix.healthThreshold, 0.0f, 1.0f);
                    liveFloat("Potion at health", &config.m_decisionMatrix.potionHealthThreshold, 0.0f, 1.0f);
                    liveFloat("Magicka potion at (casters)", &config.m_decisionMatrix.potionMagickaThreshold, 0.0f, 1.0f);
                    liveFloat("Stamina potion at (melee)", &config.m_decisionMatrix.potionStaminaThreshold, 0.0f, 1.0f);
                    liveFloat("Potion cooldown (s)", &config.m_decisionMatrix.potionCooldownSeconds, 0.0f, 60.0f, "%.1f");
                    liveFloat("Drink this far from the target", &config.m_decisionMatrix.safeDrinkDistance, 0.0f, 2000.0f, "%.0f");
                    liveFloat("Or after backing off (s)", &config.m_decisionMatrix.safeDrinkMaxWaitSeconds, 0.0f, 30.0f, "%.1f");
                    liveFloat("Stamina threshold", &config.m_decisionMatrix.staminaThreshold, 0.0f, 1.0f);
                    ImGuiMCP::Checkbox("Low stamina makes NPCs back off", &config.m_decisionMatrix.staminaAffectsRetreat);
                    ImGuiMCP::Checkbox("Back off after own swing", &config.m_decisionMatrix.backoffAfterSwing);
                    ImGuiMCP::Checkbox("Brain backoff/fallback always start", &config.m_decisionMatrix.brainBackoffFallback);
                    liveFloat("Backoff step distance", &config.m_decisionMatrix.backoffStepDistance, 50.0f, 400.0f, "%.0f");
                    liveFloat("Evasion / dodge weight", &config.m_scoringWeights.evasionDodgeBase, 0.0f, 5.0f);
                    liveFloat("Backoff weight", &config.m_scoringWeights.backoffBase, 0.0f, 5.0f);
                }
                ImGuiMCP::EndTabItem();
            }

            // ---- Groups & Movement: pacing, flanking, ranged, target switching
            if (ImGuiMCP::BeginTabItem("Groups & Movement")) {
                ImGuiMCP::Checkbox("Enable combat pacing (attackers take turns)", &config.m_combatPacing.enableCombatPacing);
                ImGuiMCP::SliderInt("Max simultaneous attackers", &config.m_combatPacing.maxSimultaneousAttackers, 1, 6);
                ImGuiMCP::Checkbox("Archers and mages keep their distance", &config.m_decisionMatrix.enableRangedPositioning);
                liveFloat("Flank only within x attack range", &config.m_decisionMatrix.flankingMaxReach, 1.0f, 10.0f, "%.1f");
                ImGuiMCP::Checkbox("Enable target switching", &config.m_targetSwitching.enableTargetSwitching);
                if (ImGuiMCP::CollapsingHeader("Advanced: pacing")) {
                    liveFloat("Slot window min (s)", &config.m_combatPacing.slotWindowMinSeconds, 0.5f, 10.0f, "%.1f");
                    liveFloat("Slot window max (s)", &config.m_combatPacing.slotWindowMaxSeconds, 0.5f, 12.0f, "%.1f");
                    ImGuiMCP::Checkbox("Pace player target only", &config.m_combatPacing.paceTargetPlayerOnly);
                    liveFloat("Engage margin beyond reach", &config.m_combatPacing.engageMargin, 0.0f, 600.0f, "%.0f");
                    liveFloat("Waiting circle margin beyond reach", &config.m_combatPacing.waitDistanceMargin, 0.0f, 1000.0f, "%.0f");
                    liveFloat("Retaliate min (s)", &config.m_combatPacing.retaliateMinSeconds, 0.0f, 10.0f, "%.1f");
                    liveFloat("Retaliate max (s)", &config.m_combatPacing.retaliateMaxSeconds, 0.0f, 10.0f, "%.1f");
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: movement and ranged")) {
                    ImGuiMCP::Checkbox("Advance into own attack range", &config.m_decisionMatrix.brainAdvanceDistance);
                    liveFloat("Advancing weight", &config.m_scoringWeights.advancingBase, 0.0f, 5.0f);
                    liveFloat("Flanking weight", &config.m_scoringWeights.flankingBase, 0.0f, 5.0f);
                    ImGuiMCP::Checkbox("Enable follower pincer", &config.m_teamwork.enableFollowerPincer);
                    liveFloat("Follower pincer bonus", &config.m_teamwork.followerPincerBonus, 0.0f, 1.0f);
                    liveFloat("Ranged: fall back from melee closer than", &config.m_decisionMatrix.rangedKeepAwayDistance, 100.0f, 2000.0f, "%.0f");
                    liveFloat("Ranged: close in beyond", &config.m_decisionMatrix.rangedMaxDistance, 600.0f, 8000.0f, "%.0f");
                    liveFloat("Hold fire for an ally at most (s)", &config.m_decisionMatrix.shotHoldMaxSeconds, 0.0f, 30.0f, "%.1f");
                    ImGuiMCP::Checkbox("Enable LoS attack gate", &config.m_envSensor.enableLosGate);
                    liveFloat("LoS eye height", &config.m_envSensor.losEyeHeight, 0.0f, 300.0f, "%.0f");
                    ImGuiMCP::Checkbox("Enable cliff-safe dodge", &config.m_envSensor.enableCliffSafeDodge);
                    liveFloat("Dodge ledge probe dist", &config.m_envSensor.dodgeLedgeProbeDistance, 0.0f, 500.0f, "%.0f");
                    liveFloat("Dodge max drop (cliff)", &config.m_envSensor.dodgeMaxDrop, 0.0f, 800.0f, "%.0f");
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: target switching")) {
                    liveFloat("Switch cooldown (s)", &config.m_targetSwitching.switchCooldownSeconds, 0.5f, 30.0f, "%.1f");
                    liveFloat("Retaliate window (s)", &config.m_targetSwitching.retaliateWindowSeconds, 0.1f, 10.0f, "%.1f");
                    liveFloat("Retaliate chance", &config.m_targetSwitching.retaliateChance, 0.0f, 1.0f);
                    liveFloat("Retaliate max distance", &config.m_targetSwitching.retaliateMaxDistance, 0.0f, 3000.0f, "%.0f");
                    liveFloat("Finisher health", &config.m_targetSwitching.finisherHealthThreshold, 0.0f, 1.0f);
                    liveFloat("Finisher max distance", &config.m_targetSwitching.finisherMaxDistance, 0.0f, 2000.0f, "%.0f");
                    liveFloat("Finisher chance", &config.m_targetSwitching.finisherChance, 0.0f, 1.0f);
                }
                ImGuiMCP::EndTabItem();
            }

            // ---- NPC Character: personality, skill, bosses, creatures, momentum
            if (ImGuiMCP::BeginTabItem("NPC Character")) {
                ImGuiMCP::Checkbox("Enable personality (Confidence / Aggression / combat style)", &config.m_decisionMatrix.enablePersonality);
                if (ImGuiMCP::Checkbox("Enable skill rolls (level-scaled mistakes)", &config.m_humanizer.enableSkillRolls)) {
                    config.MarkHumanizerDirty();
                }
                humanFloat("Skill at level 1", &config.m_humanizer.skillBase, 0.0f, 1.0f);
                ImGuiMCP::Checkbox("Enable boss detection (names in the INI: [Boss] BossNames)", &config.m_boss.enableBossDetection);
                liveFloat("Boss skill bonus", &config.m_boss.skillBonus, 0.0f, 1.0f);
                if (ImGuiMCP::CollapsingHeader("Advanced: personality")) {
                    liveFloat("Per-NPC spread (+-)", &config.m_decisionMatrix.personalityJitter, 0.0f, 0.5f);
                    liveFloat("Tactical retreat: min caution", &config.m_decisionMatrix.tacticalRetreatMinCaution, 0.0f, 1.0f);
                    ImGuiMCP::Checkbox("Enable armor style", &config.m_armorStyle.enableArmorStyle);
                    liveFloat("Light armor: evasion bonus", &config.m_armorStyle.lightEvasionBonus, 0.0f, 1.0f);
                    liveFloat("Heavy armor: evasion penalty", &config.m_armorStyle.heavyEvasionPenalty, 0.0f, 1.0f);
                    liveFloat("Heavy armor: bash/block bonus", &config.m_armorStyle.heavyGuardBonus, 0.0f, 1.0f);
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: bosses")) {
                    liveFloat("Enraged below health", &config.m_boss.enragedHealth, 0.0f, 1.0f);
                    liveFloat("Desperate below health", &config.m_boss.desperateHealth, 0.0f, 1.0f);
                    liveFloat("Boss caution floor", &config.m_boss.cautionFloor, 0.0f, 1.0f);
                    liveFloat("Boss power attack min stamina", &config.m_boss.powerStaminaMin, 0.0f, 1.0f);
                    liveFloat("Guard break after block (s)", &config.m_boss.guardBreakBlockSeconds, 0.0f, 10.0f, "%.1f");
                    liveFloat("Humanoid creature swing window (s)", &config.m_decisionMatrix.creatureSwingSeconds, 0.0f, 10.0f, "%.1f");
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: skill, reaction, fatigue and panic")) {
                    humanFloat("Skill per level", &config.m_humanizer.skillPerLevel, 0.0f, 0.05f, "%.3f");
                    humanFloat("Skill min", &config.m_humanizer.skillMin, 0.0f, 1.0f);
                    humanFloat("Skill max", &config.m_humanizer.skillMax, 0.0f, 1.0f);
                    humanFloat("Re-roll window (s)", &config.m_humanizer.skillRerollSeconds, 0.1f, 5.0f);
                    humanFloat("Base reaction delay (ms)", &config.m_humanizer.baseReactionDelayMs, 0.0f, 500.0f, "%.0f");
                    humanFloat("Reaction variance (ms)", &config.m_humanizer.reactionVarianceMs, 0.0f, 300.0f, "%.0f");
                    humanFloat("Reaction reduction/level (ms)", &config.m_humanizer.reactionDelayReductionPerLevelMs, 0.0f, 10.0f);
                    humanFloat("Min reaction delay (ms)", &config.m_humanizer.minReactionDelayMs, 0.0f, 300.0f, "%.0f");
                    if (ImGuiMCP::Checkbox("Enable fatigue", &config.m_humanizer.enableFatigue)) {
                        config.MarkHumanizerDirty();
                    }
                    humanFloat("Fatigue onset (s)", &config.m_humanizer.fatigueOnsetSeconds, 0.0f, 300.0f, "%.0f");
                    humanFloat("Fatigue full (s)", &config.m_humanizer.fatigueFullSeconds, 1.0f, 600.0f, "%.0f");
                    humanFloat("Low-stamina threshold", &config.m_humanizer.fatigueStaminaThreshold, 0.0f, 1.0f);
                    humanFloat("Fatigue skill penalty", &config.m_humanizer.fatigueSkillPenalty, 0.0f, 3.0f);
                    if (ImGuiMCP::Checkbox("Enable panic", &config.m_humanizer.enablePanic)) {
                        config.MarkHumanizerDirty();
                    }
                    humanFloat("Panic health threshold", &config.m_humanizer.panicHealthThreshold, 0.0f, 1.0f);
                    humanFloat("Panic skill penalty", &config.m_humanizer.panicSkillPenalty, 0.0f, 3.0f);
                }
                if (ImGuiMCP::CollapsingHeader("Advanced: damage momentum and race")) {
                    ImGuiMCP::Checkbox("Enable damage momentum", &config.m_damageMomentum.enableDamageMomentum);
                    liveFloat("Big-hit threshold (frac maxHP)", &config.m_damageMomentum.bigHitThreshold, 0.01f, 0.5f, "%.3f");
                    liveFloat("Chip threshold (frac maxHP)", &config.m_damageMomentum.chipThreshold, 0.0f, 0.3f, "%.3f");
                    liveFloat("Offensive bonus max", &config.m_damageMomentum.offensiveBonusMax, 0.0f, 2.0f);
                    liveFloat("Defensive penalty max", &config.m_damageMomentum.defensivePenaltyMax, 0.0f, 2.0f);
                    liveFloat("Momentum decay (s)", &config.m_damageMomentum.momentumDecaySeconds, 0.5f, 20.0f, "%.1f");
                    liveFloat("EWMA alpha", &config.m_damageMomentum.ewmaAlpha, 0.05f, 1.0f);
                    ImGuiMCP::Checkbox("Enable damage race", &config.m_damageMomentum.enableDamageRace);
                    liveFloat("Retreat at hits from death", &config.m_damageMomentum.raceRetreatHits, 0.0f, 10.0f, "%.1f");
                    liveFloat("Race retreat max health", &config.m_damageMomentum.raceRetreatMaxHealth, 0.0f, 1.0f);
                    liveFloat("Press bonus while winning", &config.m_damageMomentum.racePressBonus, 0.0f, 2.0f);
                }
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
            Personality::ReloadProfiles();
            presets = Config::ListPresets();
            selected.clear();
            config.MarkHumanizerDirty();
            applyLogLevel();
        }
    }
} // namespace CombatAI

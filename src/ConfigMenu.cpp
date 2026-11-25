#include "ConfigMenu.h"
#include "CombatDirector.h"
#include "Config.h"
#include "Logger.h"
#include "Personality.h"
#include "pch.h"

#include <array>
#include <unordered_map>

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
        // Settings search (drawn above the tabs). Every widget below goes through shown(): while a query is
        // typed, it is drawn only if it matches, under a heading for its section.
        static char search[64] = "";
        std::string query;
        std::string tabName;
        std::string section;      // "Tab" or "Tab > Header" of the widgets being drawn
        std::string shownSection; // last heading drawn while searching
        int matches = 0;
        const auto lower = [](std::string a_text) {
            std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return a_text;
        };
        const auto shown = [&](const char *label) {
            if (query.empty()) {
                return true;
            }
            if (lower(label).find(query) == std::string::npos && lower(section).find(query) == std::string::npos) {
                return false;
            }
            if (shownSection != section) {
                ImGuiMCP::SeparatorText(section.c_str());
                shownSection = section;
            }
            ++matches;
            return true;
        };
        const auto tab = [&](const char *name) {
            tabName = section = name;
            return !query.empty() || ImGuiMCP::BeginTabItem(name);
        };
        const auto endTab = [&]() {
            if (query.empty()) {
                ImGuiMCP::EndTabItem();
            }
        };
        const auto header = [&](const char *name) {
            section = tabName + " > " + name;
            return !query.empty() || ImGuiMCP::CollapsingHeader(name);
        };
        const auto check = [&](const char *label, bool *v) { return shown(label) && ImGuiMCP::Checkbox(label, v); };
        const auto sliderInt = [&](const char *label, int *v, int lo, int hi) {
            return shown(label) && ImGuiMCP::SliderInt(label, v, lo, hi);
        };
        const auto sameLine = [&]() {
            if (query.empty()) {
                ImGuiMCP::SameLine();
            }
        };
        const auto note = [&](const char *text) {
            if (query.empty()) {
                ImGuiMCP::Text("%s", text);
            }
        };

        auto humanFloat = [&](const char *label, float *v, float lo, float hi, const char *fmt = "%.2f") {
            if (shown(label) && ImGuiMCP::SliderFloat(label, v, lo, hi, fmt)) {
                config.MarkHumanizerDirty();
            }
        };
        // Decision-matrix / scoring fields are read live every evaluation, so plain
        // sliders/checkboxes take effect immediately with no dirty flag needed.
        auto liveFloat = [&](const char *label, float *v, float lo, float hi, const char *fmt = "%.2f") {
            if (shown(label)) {
                ImGuiMCP::SliderFloat(label, v, lo, hi, fmt);
            }
        };
        // Text settings: typed into a buffer, applied with the button on the game thread (Config::QueueText). The
        // buffer reloads when the setting changes elsewhere (a preset, the INI).
        auto textSetting = [&](const char *label, std::string *a_field) {
            if (!shown(label)) {
                return;
            }
            struct Edit
            {
                std::array<char, 512> buffer{};
                std::string seen;
                bool loaded = false;
            };
            static std::unordered_map<const std::string *, Edit> edits;
            auto &edit = edits[a_field];
            if (!edit.loaded || edit.seen != *a_field) {
                strncpy_s(edit.buffer.data(), edit.buffer.size(), a_field->c_str(), _TRUNCATE);
                edit.seen = *a_field;
                edit.loaded = true;
            }
            ImGuiMCP::InputText(label, edit.buffer.data(), edit.buffer.size());
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button((std::string("Apply##") + label).c_str())) {
                config.QueueText(a_field, edit.buffer.data());
            }
        };
        // spdlog's level is global and was fixed at plugin load, so toggling the
        // debug-log bool alone does nothing until we re-apply the level here.
        auto applyLogLevel = [&]() {
            auto lvl = config.m_general.enableDebugLog ? spdlog::level::debug : spdlog::level::info;
            spdlog::set_level(lvl);
            spdlog::flush_on(lvl);
        };

        // A preset resets all tuning (not General's switches, performance or integrations) to the defaults, then applies
        // its own changes; Save to keep it. The list (Default plus one entry per JSON file in Presets and UserPresets, read
        // again on reload/save) only chooses; Load applies the choice. A loaded preset's name goes to the save field
        // (not Default's), so a tweaked preset is one click from being overwritten.
        // The plugin's version (xmake set_version), so a screenshot of the menu says which build is loaded
        static const std::string version = [] {
            const auto v = SKSE::PluginDeclaration::GetSingleton()->GetVersion();
            return std::format("Enhanced Combat Behavior {}.{}.{}", v.major(), v.minor(), v.patch());
        }();
        ImGuiMCP::TextDisabled("%s", version.c_str());

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
        ImGuiMCP::TextDisabled("current tuning, what differs from the defaults (to UserPresets)");
        ImGuiMCP::Separator();

        // Search: a non-empty query drops the tabs and headers and lists every matching setting under its
        // "Tab > Header" heading. A setting matches by its own label or by its section's name.
        ImGuiMCP::SetNextItemWidth(300.0f);
        ImGuiMCP::InputTextWithHint("##Search", "Search settings (e.g. potion, parry, stamina)", search, sizeof(search));
        if (search[0]) {
            ImGuiMCP::SameLine();
            if (ImGuiMCP::SmallButton("Clear")) {
                search[0] = '\0';
            }
        }
        query = lower(search);

        // Five tabs: the settings people change sit on top, the rest under collapsed "Advanced" headers.
        if (!query.empty() || ImGuiMCP::BeginTabBar("ECBConfigTabs")) {
            // ---- General: master switches, debug, integrations, performance
            if (tab("General")) {
                check("Enable plugin", &config.m_general.enablePlugin);
                if (check("Enable debug log (select an NPC in the console to follow it)", &config.m_general.enableDebugLog)) {
                    applyLogLevel();
                }
                check("Debug log: per-tick dumps (full state, vanilla attack rolls; bulky)", &config.m_general.verboseTicks);
                check("Include humanoid creatures (draugr, falmer, skeletons)", &config.m_decisionMatrix.includeHumanoidCreatures);
                check("Include beast creatures (werewolves, trolls, spriggans)", &config.m_decisionMatrix.includeBeastCreatures);
                if (header("Integrations")) {
                    note("Precision and TK Dodge are detected on game load (restart to re-detect).");
                    check("Precision integration", &config.m_modIntegrations.enablePrecisionIntegration);
                    check("TK Dodge integration (dodges)", &config.m_modIntegrations.enableTKDodgeIntegration);
                    check("Attack Warp integration (when AttackWarp.dll is loaded)",
                          &config.m_modIntegrations.enableAttackWarpIntegration);
                    // Guns: the keyword list is edited in a buffer and applied with the button (game threads read the
                    // parsed list, never the text being typed)
                    if (shown("Gun keywords (comma-separated keyword EditorIDs)")) {
                        static char gunBuffer[512] = {};
                        static bool gunLoaded = false;
                        if (!gunLoaded) {
                            strncpy_s(gunBuffer, config.m_modIntegrations.gunKeywords.c_str(), _TRUNCATE);
                            gunLoaded = true;
                        }
                        ImGuiMCP::InputText("Gun keywords (comma-separated keyword EditorIDs)", gunBuffer, sizeof(gunBuffer));
                        ImGuiMCP::SameLine();
                        if (ImGuiMCP::Button("Apply##GunKeywords")) {
                            config.SetGunKeywords(gunBuffer);
                        }
                        ImGuiMCP::TextDisabled("%zu keyword(s) active: a weapon with one is a gun in either hand",
                                               config.GunKeywordList()->size());
                    }
                    check("Gunman NPCs also fire on the brain's timing", &config.m_decisionMatrix.enableGunShots);
                    liveFloat("Gunman: seconds between shots", &config.m_decisionMatrix.gunShotInterval, 0.5f, 10.0f, "%.1f");
                    check("Duelists (gun + blade) hold at range for a shot, then close in", &config.m_decisionMatrix.enableDuelists);
                    liveFloat("Duelist: longest hold for the shot (s)", &config.m_decisionMatrix.duelistShotHoldSeconds, 0.0f, 10.0f, "%.1f");
                }
                if (header("Performance")) {
                    humanFloat("Processing interval (s)", &config.m_general.processingInterval, 0.01f, 1.0f, "%.2f");
                    liveFloat("Near distance", &config.m_performance.distanceNear, 0.0f, 5000.0f, "%.0f");
                    liveFloat("Mid distance", &config.m_performance.distanceMid, 0.0f, 8000.0f, "%.0f");
                    liveFloat("Mid interval (s)", &config.m_performance.processingIntervalMid, 0.05f, 2.0f, "%.2f");
                    liveFloat("Far interval (s)", &config.m_performance.processingIntervalFar, 0.05f, 2.0f, "%.2f");
                }
                endTab();
            }

            // ---- Offense: attacking, combos, sprint attacks, feints
            if (tab("Offense")) {
                check("Enable sprint attack", &config.m_decisionMatrix.enableSprintAttack);
                check("Time shouts (stagger an open target, disarm an armed one)", &config.m_decisionMatrix.enableShoutTiming);
                check("Self-buff shouts (Become Ethereal, Elemental Fury)", &config.m_decisionMatrix.enableSelfShouts);
                sameLine();
                check("Slow Time too", &config.m_decisionMatrix.selfShoutSlowTime);
                check("NPCs poison their weapons", &config.m_decisionMatrix.enableNPCPoisons);
                liveFloat("Poison cooldown (s)", &config.m_decisionMatrix.poisonCooldownSeconds, 0.0f, 120.0f, "%.0f");
                liveFloat("Paralysis poison cooldown (s)", &config.m_decisionMatrix.poisonParalysisCooldownSeconds, 0.0f, 300.0f, "%.0f");
                check("Enable combo follow-up (MCO / BFCO)", &config.m_modIntegrations.enableComboFollowUp);
                liveFloat("Combo follow-up chance", &config.m_modIntegrations.comboFollowUpChance, 0.0f, 1.0f);
                liveFloat("Combo power follow-up chance", &config.m_modIntegrations.comboPowerChance, 0.0f, 1.0f);
                liveFloat("Trade blows with a swing (chance)", &config.m_decisionMatrix.tradeBlowsChance, 0.0f, 1.0f);
                liveFloat("Power attack min stamina", &config.m_decisionMatrix.powerAttackMinStamina, 0.0f, 1.0f);
                if (header("Advanced: reach and sprint attacks")) {
                    liveFloat("Offense reach multiplier", &config.m_decisionMatrix.offenseReachMultiplier, 0.5f, 3.0f);
                    liveFloat("Combo follow-up reach x attack range", &config.m_modIntegrations.comboReachMult, 1.0f, 3.0f);
                    liveFloat("Sprint attack min dist", &config.m_decisionMatrix.sprintAttackMinDistance, 0.0f, 800.0f, "%.0f");
                    liveFloat("Sprint attack max dist", &config.m_decisionMatrix.sprintAttackMaxDistance, 0.0f, 1200.0f, "%.0f");
                    liveFloat("Sprint attack launch dist (0 = 3x reach)", &config.m_decisionMatrix.sprintAttackLaunchDistance, 0.0f, 1000.0f, "%.0f");
                }
                if (header("Advanced: vanilla attack shaping")) {
                    check("Enable vanilla attack shaping", &config.m_vanillaAttacks.enable);
                    liveFloat("Opening: power attack x", &config.m_vanillaAttacks.openingPowerMult, 0.0f, 5.0f);
                    liveFloat("Opening: normal attack x", &config.m_vanillaAttacks.openingNormalMult, 0.0f, 5.0f);
                    liveFloat("Target blocking: power attack x", &config.m_vanillaAttacks.blockedPowerMult, 0.0f, 5.0f);
                    liveFloat("Target blocking: normal attack x", &config.m_vanillaAttacks.blockedNormalMult, 0.0f, 5.0f);
                }
                if (header("Advanced: action weights")) {
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
                endTab();
            }

            // ---- Defense: parry, timed block, counter, dodges, backing off
            if (tab("Defense")) {
                note("Vanilla block / bash / attack, timed by the brain. Parry and timed-block effects come from");
                note("mods (EldenParry, Simple Timed Block); without them these are well-timed bashes and blocks.");
                check("Parry (timed bash)", &config.m_parry.enableParry);
                sameLine();
                check("Timed block", &config.m_timedBlock.enableTimedBlock);
                sameLine();
                check("Counter after a blocked hit", &config.m_decisionMatrix.enableCounter);
                check("Counter in Simple Elden Counter NG's window (when installed)",
                      &config.m_decisionMatrix.useEldenCounterNGWindow);
                check("Punish the player's swing recovery", &config.m_decisionMatrix.enablePunish);
                check("Brain attacks skip vanilla's hit cone when in reach", &config.m_decisionMatrix.intentHitConeOverride);
                check("Learn fist fighters' and beasts' reach from landed blows", &config.m_decisionMatrix.learnUnarmedReach);
                check("Melee NPCs don't hide while the brain sends them in", &config.m_decisionMatrix.noHideWhileAdvancing);
                check("No held block against melee targets (timed blocks only)", &config.m_decisionMatrix.noVanillaBlockVsMelee);
                liveFloat("Held block (vs ranged) needs stamina", &config.m_decisionMatrix.vanillaBlockMinStamina, 0.0f, 1.0f);
                liveFloat("Parry / timed block a swing (chance)", &config.m_decisionMatrix.defendOnSwingChance, 0.0f, 1.0f);
                liveFloat("Dodge a close swing (chance)", &config.m_decisionMatrix.closeDodgeChance, 0.0f, 1.0f);
                liveFloat("Back off from a swing (chance)", &config.m_decisionMatrix.backoffOnSwingChance, 0.0f, 1.0f);
                liveFloat("Step out after own swing (chance)", &config.m_decisionMatrix.backoffAfterSwingChance, 0.0f, 1.0f);
                check("Enable survival retreat", &config.m_decisionMatrix.enableSurvivalRetreat);
                sameLine();
                check("NPCs back off, then drink potions", &config.m_decisionMatrix.enableNPCPotions);
                check("NPCs drink Fortify / Resist potions (at range)", &config.m_decisionMatrix.enableBuffPotions);
                check("Poisoned NPCs drink antidotes (Cure Poison)", &config.m_decisionMatrix.enableAntidotes);
                if (header("Advanced: parry and timed block windows")) {
                    liveFloat("Parry window start (s)", &config.m_parry.parryWindowStart, 0.0f, 0.5f, "%.3f");
                    liveFloat("Parry window end (s)", &config.m_parry.parryWindowEnd, 0.0f, 0.5f, "%.3f");
                    liveFloat("Parry max distance", &config.m_parry.parryMaxDistance, 0.0f, 400.0f, "%.0f");
                    liveFloat("Timed block window start (s)", &config.m_timedBlock.timedBlockWindowStart, 0.0f, 0.5f, "%.3f");
                    liveFloat("Timed block window end (s)", &config.m_timedBlock.timedBlockWindowEnd, 0.0f, 0.5f, "%.3f");
                    liveFloat("Timed block max distance", &config.m_timedBlock.timedBlockMaxDistance, 0.0f, 400.0f, "%.0f");
                    liveFloat("Interrupt reach multiplier", &config.m_decisionMatrix.interruptReachMultiplier, 0.5f, 3.0f);
                    liveFloat("Interrupt max distance", &config.m_decisionMatrix.interruptMaxDistance, 0.0f, 1000.0f, "%.0f");
                    check("NPCs with a dagger can block and bash (after restart)", &config.m_decisionMatrix.allowDaggerBlock);
                }
                if (header("Advanced: dodges (TK Dodge)")) {
                    liveFloat("Forward dodge chance", &config.m_dodgeSystem.forwardDodgeChance, 0.0f, 1.0f);
                    liveFloat("Forward dodge cooldown (s)", &config.m_dodgeSystem.forwardDodgeCooldownSeconds, 0.0f, 60.0f, "%.1f");
                    liveFloat("Forward dodge min stamina", &config.m_dodgeSystem.forwardDodgeMinStamina, 0.0f, 1.0f);
                    liveFloat("Forward dodge: max gap beyond reach", &config.m_dodgeSystem.forwardDodgeMaxGap, 0.0f, 1000.0f, "%.0f");
                    liveFloat("Dodge stamina cost", &config.m_dodgeSystem.dodgeStaminaCost, 0.0f, 100.0f, "%.0f");
                    liveFloat("Min stamina to dodge (not charged)", &config.m_dodgeSystem.minDodgeStamina, 0.0f, 100.0f, "%.0f");
                    textSetting("Dodge event forward", &config.m_dodgeSystem.eventForward);
                    textSetting("Dodge event back", &config.m_dodgeSystem.eventBack);
                    textSetting("Dodge event left", &config.m_dodgeSystem.eventLeft);
                    textSetting("Dodge event right", &config.m_dodgeSystem.eventRight);
                    textSetting("Dodge event fallback (when the graph rejects one)", &config.m_dodgeSystem.eventFallback);
                    liveFloat("I-frame duration (s)", &config.m_dodgeSystem.iFrameDuration, 0.0f, 1.0f);
                    check("Enable step dodge", &config.m_dodgeSystem.enableStepDodge);
                    check("Enable dodge attack-cancel", &config.m_dodgeSystem.enableDodgeAttackCancel);
                    check("Enable jump evasion", &config.m_decisionMatrix.enableJumpEvasion);
                    liveFloat("Jump height", &config.m_decisionMatrix.jumpHeight, 20.0f, 300.0f, "%.0f");
                    liveFloat("Jump stamina cost", &config.m_decisionMatrix.jumpStaminaCost, 0.0f, 100.0f, "%.0f");
                    liveFloat("Jump chance (instead of a dodge)", &config.m_decisionMatrix.evasionJumpChance, 0.0f, 1.0f);
                    liveFloat("Jump evasion min distance", &config.m_decisionMatrix.jumpEvasionDistanceMin, 0.0f, 2000.0f, "%.0f");
                    liveFloat("Jump evasion max distance", &config.m_decisionMatrix.jumpEvasionDistanceMax, 0.0f, 3000.0f, "%.0f");
                    liveFloat("Evasion min distance", &config.m_decisionMatrix.evasionMinDistance, 0.0f, 600.0f, "%.0f");
                }
                if (header("Advanced: projectiles")) {
                    check("Enable projectile dodge", &config.m_projectileAwareness.enableProjectileDodge);
                    liveFloat("Projectile: react from (s)", &config.m_projectileAwareness.reactMinSeconds, 0.0f, 2.0f);
                    liveFloat("Projectile: react until (s)", &config.m_projectileAwareness.reactMaxSeconds, 0.05f, 3.0f);
                    liveFloat("Projectile: dodge chance", &config.m_projectileAwareness.dodgeChance, 0.0f, 1.0f);
                }
                if (header("Advanced: retreat and back off")) {
                    liveFloat("Health threshold", &config.m_decisionMatrix.healthThreshold, 0.0f, 1.0f);
                    liveFloat("Potion at health", &config.m_decisionMatrix.potionHealthThreshold, 0.0f, 1.0f);
                    liveFloat("Magicka potion at (casters)", &config.m_decisionMatrix.potionMagickaThreshold, 0.0f, 1.0f);
                    liveFloat("Stamina potion at (melee)", &config.m_decisionMatrix.potionStaminaThreshold, 0.0f, 1.0f);
                    liveFloat("Potion cooldown (s)", &config.m_decisionMatrix.potionCooldownSeconds, 0.0f, 60.0f, "%.1f");
                    liveFloat("Drink this far from the target", &config.m_decisionMatrix.safeDrinkDistance, 0.0f, 2000.0f, "%.0f");
                    liveFloat("Or after backing off (s)", &config.m_decisionMatrix.safeDrinkMaxWaitSeconds, 0.0f, 30.0f, "%.1f");
                    liveFloat("Stamina threshold", &config.m_decisionMatrix.staminaThreshold, 0.0f, 1.0f);
                    check("Low stamina makes NPCs back off", &config.m_decisionMatrix.staminaAffectsRetreat);
                    check("Back off after own swing", &config.m_decisionMatrix.backoffAfterSwing);
                    check("Brain backoff/fallback always start", &config.m_decisionMatrix.brainBackoffFallback);
                    liveFloat("Evasion / dodge weight", &config.m_scoringWeights.evasionDodgeBase, 0.0f, 5.0f);
                    liveFloat("Backoff weight", &config.m_scoringWeights.backoffBase, 0.0f, 5.0f);
                }
                endTab();
            }

            // ---- Groups & Movement: pacing, flanking, ranged, target switching
            if (tab("Groups & Movement")) {
                check("Enable combat pacing (attackers take turns)", &config.m_combatPacing.enableCombatPacing);
                check("Spread around the target (each melee attacker its own side)", &config.m_combatPacing.enableRingSlots);
                check("Close in, step aside from a crowding ally", &config.m_combatPacing.enableCloseSpacing);
                check("Combat style sets the waiting distance", &config.m_combatPacing.styleWaiting);
                check("Wait at the ring when every turn is taken", &config.m_combatPacing.holdBeforeEngaging);
                sliderInt("Max simultaneous attackers", &config.m_combatPacing.maxSimultaneousAttackers, 1, 6);
                check("Archers and mages keep their distance", &config.m_decisionMatrix.enableRangedPositioning);
                check("Enable target switching", &config.m_targetSwitching.enableTargetSwitching);
                check("Stuck advance / flank: aim at the navmesh next to a goal off it", &config.m_decisionMatrix.advanceNavmeshRedirect);
                check("Flanking combat styles use the brain's movement", &config.m_decisionMatrix.flankingStyleBrainMovement);
                if (header("Advanced: pacing")) {
                    liveFloat("Slot window min (s)", &config.m_combatPacing.slotWindowMinSeconds, 1.5f, 10.0f, "%.1f");
                    liveFloat("Rest after a turn (s)", &config.m_combatPacing.restSeconds, 0.5f, 10.0f, "%.1f");
                    liveFloat("Slot window max (s)", &config.m_combatPacing.slotWindowMaxSeconds, 0.5f, 12.0f, "%.1f");
                    check("Pace player target only", &config.m_combatPacing.paceTargetPlayerOnly);
                    sliderInt("Extra attackers vs giants / beasts", &config.m_combatPacing.dangerExtraAttackers, 0, 6);
                    liveFloat("Engage margin beyond reach", &config.m_combatPacing.engageMargin, 0.0f, 600.0f, "%.0f");
                    liveFloat("Waiting circle margin beyond reach", &config.m_combatPacing.waitDistanceMargin, 0.0f, 1000.0f, "%.0f");
                    liveFloat("Retaliate min (s)", &config.m_combatPacing.retaliateMinSeconds, 0.0f, 10.0f, "%.1f");
                    liveFloat("Retaliate max (s)", &config.m_combatPacing.retaliateMaxSeconds, 0.0f, 10.0f, "%.1f");
                }
                if (header("Advanced: movement and ranged")) {
                    check("Advance into own attack range", &config.m_decisionMatrix.brainAdvanceDistance);
                    liveFloat("Advancing weight", &config.m_scoringWeights.advancingBase, 0.0f, 5.0f);
                    liveFloat("Flanking weight", &config.m_scoringWeights.flankingBase, 0.0f, 5.0f);
                    check("Enable follower pincer", &config.m_teamwork.enableFollowerPincer);
                    liveFloat("Follower pincer bonus", &config.m_teamwork.followerPincerBonus, 0.0f, 1.0f);
                    liveFloat("Ranged: fall back from melee closer than", &config.m_decisionMatrix.rangedKeepAwayDistance, 100.0f, 2000.0f, "%.0f");
                    liveFloat("Ranged: close in beyond", &config.m_decisionMatrix.rangedMaxDistance, 600.0f, 8000.0f, "%.0f");
                    check("Enable LoS attack gate", &config.m_envSensor.enableLosGate);
                    check("Enable cliff-safe dodge", &config.m_envSensor.enableCliffSafeDodge);
                }
                if (header("Advanced: target switching")) {
                    liveFloat("Switch cooldown (s)", &config.m_targetSwitching.switchCooldownSeconds, 0.5f, 30.0f, "%.1f");
                    liveFloat("Retaliate chance", &config.m_targetSwitching.retaliateChance, 0.0f, 1.0f);
                    liveFloat("Retaliate max distance", &config.m_targetSwitching.retaliateMaxDistance, 0.0f, 3000.0f, "%.0f");
                    liveFloat("Finisher health", &config.m_targetSwitching.finisherHealthThreshold, 0.0f, 1.0f);
                    liveFloat("Finisher max distance", &config.m_targetSwitching.finisherMaxDistance, 0.0f, 2000.0f, "%.0f");
                    liveFloat("Finisher chance", &config.m_targetSwitching.finisherChance, 0.0f, 1.0f);
                }
                endTab();
            }

            // ---- NPC Character: personality, skill, bosses, creatures, momentum
            if (tab("NPC Character")) {
                check("Enable personality (Confidence / Aggression / combat style)", &config.m_decisionMatrix.enablePersonality);
                check("Fear dangerous targets (giants, werewolves, bosses, much stronger foes)", &config.m_decisionMatrix.enableTargetDanger);
                if (check("Enable skill rolls (level-scaled mistakes)", &config.m_humanizer.enableSkillRolls)) {
                    config.MarkHumanizerDirty();
                }
                humanFloat("Skill at level 1", &config.m_humanizer.skillBase, 0.0f, 1.0f);
                if (check("Skill as timing (parries, timed blocks and close dodges come early or late)", &config.m_humanizer.skillAsTiming)) {
                    config.MarkHumanizerDirty();
                }
                humanFloat("Reach judgment error (0 = off)", &config.m_humanizer.reachJudgmentError, 0.0f, 0.3f);
                check("Enable boss detection", &config.m_boss.enableBossDetection);
                textSetting("Boss names (comma-separated)", &config.m_boss.bossNames);
                liveFloat("Boss skill bonus", &config.m_boss.skillBonus, 0.0f, 1.0f);
                if (header("Advanced: personality")) {
                    liveFloat("Per-NPC spread (+-)", &config.m_decisionMatrix.personalityJitter, 0.0f, 0.5f);
                    liveFloat("Target danger: caution influence", &config.m_decisionMatrix.targetDangerInfluence, 0.0f, 1.0f);
                    check("Enable armor style", &config.m_armorStyle.enableArmorStyle);
                    liveFloat("Light armor: evasion bonus", &config.m_armorStyle.lightEvasionBonus, 0.0f, 1.0f);
                    liveFloat("Heavy armor: evasion penalty", &config.m_armorStyle.heavyEvasionPenalty, 0.0f, 1.0f);
                    liveFloat("Heavy armor: bash/block bonus", &config.m_armorStyle.heavyGuardBonus, 0.0f, 1.0f);
                }
                if (header("Advanced: bosses")) {
                    liveFloat("Enraged below health", &config.m_boss.enragedHealth, 0.0f, 1.0f);
                    liveFloat("Desperate below health", &config.m_boss.desperateHealth, 0.0f, 1.0f);
                    liveFloat("Boss caution floor", &config.m_boss.cautionFloor, 0.0f, 1.0f);
                    liveFloat("Boss power attack min stamina", &config.m_boss.powerStaminaMin, 0.0f, 1.0f);
                    liveFloat("Guard break after block (s)", &config.m_boss.guardBreakBlockSeconds, 0.0f, 10.0f, "%.1f");
                }
                if (header("Advanced: skill, reaction, fatigue and panic")) {
                    humanFloat("Skill per level", &config.m_humanizer.skillPerLevel, 0.0f, 0.05f, "%.3f");
                    humanFloat("Skill min", &config.m_humanizer.skillMin, 0.0f, 1.0f);
                    humanFloat("Skill max", &config.m_humanizer.skillMax, 0.0f, 1.0f);
                    humanFloat("Skill per 100 actor-value gap", &config.m_humanizer.skillActorValueWeight, 0.0f, 2.0f);
                    humanFloat("Base reaction delay (ms)", &config.m_humanizer.baseReactionDelayMs, 0.0f, 500.0f, "%.0f");
                    humanFloat("Reaction variance (ms)", &config.m_humanizer.reactionVarianceMs, 0.0f, 300.0f, "%.0f");
                    humanFloat("Reaction reduction/level (ms)", &config.m_humanizer.reactionDelayReductionPerLevelMs, 0.0f, 10.0f);
                    humanFloat("Min reaction delay (ms)", &config.m_humanizer.minReactionDelayMs, 0.0f, 300.0f, "%.0f");
                    if (check("Enable fatigue", &config.m_humanizer.enableFatigue)) {
                        config.MarkHumanizerDirty();
                    }
                    humanFloat("Low-stamina threshold", &config.m_humanizer.fatigueStaminaThreshold, 0.0f, 1.0f);
                    humanFloat("Fatigue skill penalty", &config.m_humanizer.fatigueSkillPenalty, 0.0f, 3.0f);
                    if (check("Enable panic", &config.m_humanizer.enablePanic)) {
                        config.MarkHumanizerDirty();
                    }
                    humanFloat("Panic health threshold", &config.m_humanizer.panicHealthThreshold, 0.0f, 1.0f);
                    humanFloat("Panic skill penalty", &config.m_humanizer.panicSkillPenalty, 0.0f, 3.0f);
                }
                if (header("Advanced: damage momentum and race")) {
                    check("Enable damage momentum", &config.m_damageMomentum.enableDamageMomentum);
                    liveFloat("Big-hit threshold (frac maxHP)", &config.m_damageMomentum.bigHitThreshold, 0.01f, 0.5f, "%.3f");
                    liveFloat("Chip threshold (frac maxHP)", &config.m_damageMomentum.chipThreshold, 0.0f, 0.3f, "%.3f");
                    liveFloat("Offensive bonus max", &config.m_damageMomentum.offensiveBonusMax, 0.0f, 2.0f);
                    liveFloat("Defensive penalty max", &config.m_damageMomentum.defensivePenaltyMax, 0.0f, 2.0f);
                    check("Enable damage race", &config.m_damageMomentum.enableDamageRace);
                    liveFloat("Retreat at hits from death", &config.m_damageMomentum.raceRetreatHits, 0.0f, 10.0f, "%.1f");
                    liveFloat("Race retreat max health", &config.m_damageMomentum.raceRetreatMaxHealth, 0.0f, 1.0f);
                    liveFloat("Press bonus while winning", &config.m_damageMomentum.racePressBonus, 0.0f, 2.0f);
                }
                endTab();
            }

            if (query.empty()) {
                ImGuiMCP::EndTabBar();
            } else if (matches == 0) {
                ImGuiMCP::TextDisabled("No setting matches \"%s\"", search);
            }
        }

        ImGuiMCP::Separator();
        // Saves go to the user INI, which a mod update does not replace (Config::UserIniPath)
        if (ImGuiMCP::Button("Save settings")) {
            config.Save();
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Reload saved settings")) {
            config.Load();
            Personality::ReloadProfiles();
            presets = Config::ListPresets();
            selected.clear();
            config.MarkHumanizerDirty();
            applyLogLevel();
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::TextDisabled("SKSE/Plugins/EnhancedCombatBehavior_User.ini");
    }
} // namespace CombatAI

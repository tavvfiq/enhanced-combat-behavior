#pragma once

#include <SimpleIni.h>
#include <atomic>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace CombatAI
{
    // Forward declaration for the runtime config menu (needs write access).
    class ConfigMenu;

    // Configuration manager for the plugin
    class Config
    {
        friend class ConfigMenu;

      public:
        struct GeneralSettings
        {
            bool enablePlugin = true;
            bool enableDebugLog = false;
            bool verboseTicks = false; // + per-tick dumps of the console-selected NPC (full state, vanilla attack rolls)
            float processingInterval = 0.1f;
        };

        struct HumanizerSettings
        {
            // One-time delay before an actor that just entered combat acts
            float baseReactionDelayMs = 200.0f;            // Base delay at level 1
            float reactionDelayReductionPerLevelMs = 2.0f; // Reduce delay by 2ms per level
            float minReactionDelayMs = 50.0f;              // Minimum possible delay
            float reactionVarianceMs = 100.0f;

            // Skill rolls: deliberate actions roll once per opportunity (see Humanizer)
            bool enableSkillRolls = true;
            float skillBase = 0.75f;          // Chance at level 1
            float skillPerLevel = 0.008f;     // Added per level above 1
            float skillMin = 0.2f;
            float skillMax = 0.95f;
            float skillRerollSeconds = 1.0f;  // Openings that aren't one swing re-roll this often
            float skillActorValueWeight = 0.4f;  // skill per 100 points of actor-value gap to the target (0 = level only)
            float skillActorValueMaxGap = 50.0f; // the gap counts up to this many points either way

            // Fatigue: long fights and low stamina make the actor sloppier
            bool enableFatigue = true;
            bool fatigueFromExertion = true;       // fatigue = stamina spent lately (off: the combat-time ramp below)
            float fatigueExertionSeconds = 30.0f;  // spending a full stamina bar within about this long = full fatigue
            float fatigueOnsetSeconds = 45.0f;     // Combat time before fatigue starts building
            float fatigueFullSeconds = 180.0f;     // Combat time at full fatigue
            float fatigueStaminaThreshold = 0.25f; // Below this stamina fraction, fatigue rises to full at 0
            float fatigueSkillPenalty = 0.5f;      // Failure chance x(1 + this) at full fatigue
            // Panic: low health makes everything except retreating sloppier
            bool enablePanic = true;
            float panicHealthThreshold = 0.3f; // Below this health fraction, panic rises to full at 0
            float panicSkillPenalty = 0.75f;   // Failure chance x(1 + this) at full panic
        };

        struct DodgeSystemSettings
        {
            float dodgeStaminaCost = 10.0f;
            float iFrameDuration = 0.3f;
            bool enableStepDodge = false;
            bool enableDodgeAttackCancel = true;
            // Forward dodge only closes the last stretch: up to this far beyond attack reach
            float forwardDodgeMaxGap = 200.0f;
            // Counter: dodge in when the target is recovering from a swing we evaded this recently
            float forwardDodgeCounterWindowSeconds = 1.5f;
            float forwardDodgeChance = 0.25f;          // per opening, x personality risk (0.25..1.75), x2 for a counter
            float forwardDodgeCooldownSeconds = 8.0f;  // min time between forward dodges
            float forwardDodgeMinStamina = 0.5f;       // stamina fraction needed
            // Animation events sent per dodge direction (TK Dodge RE by default). Another dodge
            // framework's events can go here; the fallback is tried when the graph rejects one.
            std::string eventForward = "TKDodgeForward";
            std::string eventBack = "TKDodgeBack";
            std::string eventLeft = "TKDodgeLeft";
            std::string eventRight = "TKDodgeRight";
            std::string eventFallback;
            // Stamina an NPC must have to start a dodge, without being charged it (for dodge
            // frameworks that charge their own cost). The check uses the larger of this and the cost.
            float minDodgeStamina = 0.0f;
            float RequiredStamina() const { return (std::max)(dodgeStaminaCost, minDodgeStamina); }
        };

        struct DecisionMatrixSettings
        {
            float interruptMaxDistance = 150.0f;
            float interruptReachMultiplier = 1.2f;
            float evasionMinDistance = 250.0f;
            bool enableJumpEvasion = true;
            float jumpEvasionDistanceMin = 500.0f;
            float jumpEvasionDistanceMax = 1500.0f;
            float evasionJumpChance = 0.5f;
            float jumpHeight = 100.0f; // Jump apex height in game units
            float jumpStaminaCost = 20.0f; // Spent per jump; no cooldown, an NPC without this much stamina can't jump
            float staminaThreshold = 0.2f;
            bool brainBackoffFallback = true;    // Our Backoff/Fallback start where the brain asks (false: vanilla start rules)
            float backoffStepDistance = 150.0f;  // How far our Backoff steps back from where the NPC stands
            bool backoffAfterSwing = true;       // Step out during its own swing's follow-through when the target isn't open
            bool brainAdvanceDistance = true;    // Our Advance closes to the brain's attack range (false: vanilla's range)
            float backoffOnSwingChance = 0.35f;   // Chance to back off from each of the target's normal swings
            float backoffAfterSwingChance = 0.4f; // Chance to step out after each of its own swings
            float tradeBlowsChance = 0.3f;        // Chance to attack into each of the target's normal swings
            bool enablePunish = true;             // attack while the player recovers from a swing (its clip's recovery)
            float punishChainWaitSeconds = 0.05f; // wait this long past the combo window opening (reckless: none)
            float punishPriority = 3.0f;          // decision priority of a punish (a normal attack scores ~2.3)
            bool enablePersonality = true;          // Confidence / Aggression / combat style scale risky rolls
            bool includeHumanoidCreatures = true;   // draugr, falmer, skeletons with a weapon get the brain (no dodges)
            float creatureSwingSeconds = 3.0f;      // such a creature starts no new attack this soon after a swing start
            float personalityJitter = 0.15f;        // fixed per-NPC spread added to risk / caution / patience
            float tacticalRetreatMinCaution = 0.3f; // retreat from a crowd (not low health) only this careful or more
            bool enableTargetDanger = true;           // fear giants, beasts, bosses and much stronger targets (TargetDanger.h)
            float targetDangerInfluence = 0.5f;       // how far danger lowers risk / raises caution (0 = rating only)
            float targetDangerStrengthWeight = 0.4f;  // danger from a target far above the NPC's level or max health
            float flankingMaxReach = 2.5f;          // flank only within this x the attack range
            float shotHoldMaxSeconds = 3.0f;        // longest an archer / caster holds fire for an ally in the way
            float closeDodgeChance = 0.3f;        // Chance to dodge each melee swing aimed at it from within reach
            float defendOnSwingChance = 0.5f;     // Chance to parry / timed block each swing aimed at it (in window)
            bool enableCounter = true;            // Power attack in the window after blocking a hit (guard counter)
            bool enableSelfShouts = true;         // Become Ethereal at low health, Elemental/Battle Fury on engage (vanilla never shouts them)
            bool selfShoutSlowTime = false;       // also Slow Time at low health or outnumbered (an NPC's Slow Time is untested)
            bool enableShoutTiming = true;        // Stagger shouts for an open target, Disarm for an armed one; none at a hidden player or while waiting a turn
            bool allowDaggerBlock = true;         // Let daggers block and bash (patches vanilla; applies at game start)
            bool enableRangedPositioning = true;  // Archers and mages keep their distance from melee attackers
            bool usePlayerClipTiming = true;      // Parry/timed block timed from the player's attack animation (off: learned median)
            float rangedKeepAwayDistance = 450.0f; // Fall back when a melee attacker is closer than this
            float rangedMaxDistance = 2000.0f;     // Close in when the target is farther than this (or out of sight)
            bool staminaAffectsRetreat = false;  // Off: retreat/backoff depend on health only (light attacks cost no stamina)
            float powerAttackMinStamina = 0.7f;  // Power/sprint attacks and power bashes need this stamina fraction
            float healthThreshold = 0.3f;
            bool enableSurvivalRetreat = true;
            bool enableNPCPotions = true;         // The brain runs NPC healing potions: back off at low health, then drink
            float potionHealthThreshold = 0.35f;  // drink at or below this health fraction
            float potionMagickaThreshold = 0.25f; // spellcasters back off and drink a magicka potion at or below this
            float potionStaminaThreshold = 0.2f;  // melee fighters drink a stamina potion where they stand at or below this
            float potionCooldownSeconds = 5.0f;   // between drinks (the effect, or an animation, takes a moment)
            bool enableBuffPotions = true;        // NPCs drink Fortify weapon-skill potions on engage, Resist vs an elemental target
            bool enableNPCPoisons = true;         // NPCs coat their weapon with a poison they carry (vanilla never does)
            float poisonCooldownSeconds = 10.0f;  // between poisons (one dose each: the next hit spends it)
            float poisonParalysisCooldownSeconds = 30.0f; // between paralysis poisons (none on a target already down)
            float safeDrinkDistance = 350.0f;     // drink once this far from the target
            float safeDrinkMaxWaitSeconds = 3.0f; // or after backing off this long
            bool enableOffense = true;
            float offenseReachMultiplier = 1.0f;
            float offenseReachSafetyMargin = 20.0f;
            bool enableSprintAttack = true; // Master toggle for NPC sprint (gap-closer) attacks
            float sprintAttackLaunchDistance = 0.0f; // Launch only while sprinting and this close; 0 = 3 x weapon reach
            float sprintAttackMinDistance = 300.0f;
            float sprintAttackMaxDistance = 800.0f;
            float powerAttackStaminaCost = 20.0f;  // Stamina cost for power attacks
            float sprintAttackStaminaCost = 25.0f; // Stamina cost for sprint attacks (higher due to sprinting + attack)
            bool enablePowerAttackStaminaCheck = true;  // If true, check stamina before allowing power attacks
            bool enableSprintAttackStaminaCheck = true; // If true, check stamina before allowing sprint attacks
            // Note: Normal attacks always allowed regardless of stamina (even if stamina is 0)
            // Stamina checks compare actual stamina value against stamina cost (not percentage thresholds)
        };

        // Curated high-impact scoring weights for the decision matrix.
        // Defaults mirror the previously hardcoded literals in DecisionMatrix.cpp.
        // Base priorities decide which action tends to win; modifiers shift priority
        // based on the tactical situation.
        struct ScoringWeights
        {
            // Per-action base priorities
            float interruptPowerAttackBase = 1.4f; // Bash to interrupt a power attack
            float evasionDodgeBase = 1.3f;         // Dodge/strafe when threatened
            float advancingBase = 1.0f;            // Run toward target to close distance
            float sprintAttackBase = 1.3f;         // Sprint (gap-closer) attack
            float attackBase = 1.0f;               // Normal/power attack in melee range
            float backoffBase = 1.8f;              // Back away from caster/archer
            float flankingBase = 1.4f;             // Tactical flanking movement

            // High-impact offense modifiers (added to attack priority)
            float targetStaggeredBonus = 0.6f;          // Target knocked/staggered
            float targetCastingBonus = 0.5f;            // Target casting or drawing bow
            float targetRecoveryBonus = 0.5f;           // Target in attack recovery window
            float targetFleeingBonus = 0.4f;            // Target fleeing (pursue/finish)
            float targetLowHealthFinisherBonus = 0.4f;  // Target very low health (finish)
            float openingRiskPenalty = 0.4f;            // Penalty when target is ready & facing us
            float flankingAttackBonus = 0.3f;           // Attacking from behind/side
            float allyCoverBonus = 0.2f;                // Allies nearby to cover the opening
        };

        struct PerformanceSettings
        {
            float cleanupInterval = 5.0f;

            // Adaptive Processing Interval (LOD) settings
            float processingIntervalMid = 0.2f; // Slower updates for mid-range (1000-3000 units)
            float processingIntervalFar = 0.5f; // Slowest updates for far range (>3000 units)
            float distanceNear = 1000.0f;       // Distance threshold for "Near" (uses processingInterval)
            float distanceMid = 3000.0f;        // Distance threshold for "Mid" (uses processingIntervalMid)
        };

        struct ModIntegrationSettings
        {
            bool enablePrecisionIntegration = true;
            bool enableTKDodgeIntegration = true;
            bool enableSCARIntegration = true;
            float scarComboWindowSeconds = 20.0f; // A SCAR_ComboStart this recent marks the moveset as SCAR-driven
            float scarStallSeconds = 3.0f;        // SCAR actor wanting to attack this long without one: we attack
            bool enableComboFollowUp = true;      // MCO/BFCO combo windows: start the next attack (off while SCAR.dll is loaded)
            float comboFollowUpChance = 0.7f;     // per window: chance of a normal follow-up
            float comboPowerChance = 0.3f;        // per power window: chance of a power follow-up (needs power stamina)
            float comboReachMult = 1.3f;          // a follow-up starts up to this x the attack range (combo steps lunge)
        };

        struct ParrySettings
        {
            bool enableParry = true;         // Enable parry mechanism (timed bash)
            float parryWindowStart = 0.05f;  // Start bash this many seconds before attack hits (minimum)
            float parryWindowEnd = 0.25f;    // Latest time to start bash before attack hits (maximum)
            float parryMinDistance = 0.0f;   // Minimum distance for parry (run 38: you hit from 42-47 units)
            float parryMaxDistance = 200.0f; // Maximum distance for parry
        };

        struct TimedBlockSettings
        {
            bool enableTimedBlock = true;          // Enable timed block mechanism
            float timedBlockWindowStart = 0.05f;   // Start block this many seconds before attack hits (minimum)
            float timedBlockWindowEnd = 0.33f;     // End of timing window (matches mod's 0.33s window)
            float timedBlockMinDistance = 0.0f;    // Minimum distance for timed block (run 38: hits from 42-47 units)
            float timedBlockMaxDistance = 200.0f;  // Maximum distance for timed block
        };

        // Attack-slot pacing ("Wait Your Turn"): cap concurrent attackers per target.
        struct CombatPacingSettings
        {
            bool enableCombatPacing = true;      // Master toggle
            int maxSimultaneousAttackers = 3;    // How many may attack a target at once
            int dangerExtraAttackers = 2;        // more may attack a huge or beast target at once (they gang up)
            float slotWindowMinSeconds = 2.0f;   // Min time a slot is held before rotating
            float slotWindowMaxSeconds = 4.0f;   // Max time a slot is held before rotating
            bool paceTargetPlayerOnly = true;    // Only pace attacks against the player
            float engageMargin = 150.0f;         // Within reach + this (or attacking) = engaging, needs a slot
            float waitDistanceMargin = 120.0f;   // Waiting NPCs circle at reach + this
            float retaliateMinSeconds = 1.0f;    // A waiting NPC hit by its target may fight back
            float retaliateMaxSeconds = 3.0f;    // for a random time in this range
        };

        // Damage momentum: NPC aggression scales with how hard its recent hits landed.
        struct DamageMomentumSettings
        {
            bool enableDamageMomentum = true;   // Master toggle
            float bigHitThreshold = 0.08f;      // >= this fraction of target max health = full aggression
            float chipThreshold = 0.03f;        // <= this = full defensive
            float offensiveBonusMax = 0.4f;     // Max attack-priority bonus at full aggression
            float defensivePenaltyMax = 0.3f;   // Max attack-priority penalty at full chip (anti-turtle cap)
            float momentumDecaySeconds = 5.0f;  // Momentum fades to 0 over this long with no hits
            float ewmaAlpha = 0.5f;             // Smoothing of per-hit damage fraction (0-1)
            bool enableDamageRace = true;       // Weigh hits-to-kill against hits-to-die
            float raceRetreatHits = 2.0f;       // Losing and this many of the target's hits from death: retreat
            float raceRetreatMaxHealth = 0.75f; // ...but never above this health fraction
            float racePressBonus = 0.4f;        // Attack priority while winning (and no health retreat)
        };

        // Environmental sensing (raycast-driven spatial awareness).
        struct EnvSensorSettings
        {
            bool enableLosGate = true;              // Suppress attacks at a target behind a wall/railing
            float losEyeHeight = 96.0f;             // Torso offset (game units) for line-of-sight rays
            bool enableCliffSafeDodge = true;       // Never dodge off a ledge/cliff
            float dodgeLedgeProbeDistance = 180.0f; // How far ahead a dodge is assumed to land (game units)
            float dodgeMaxDrop = 200.0f;            // Drop beyond this below the landing spot = treat as cliff
        };

        // Target switching: turn on whoever just hit us, or finish off a nearly dead enemy.
        struct TargetSwitchingSettings
        {
            bool enableTargetSwitching = true;
            float switchCooldownSeconds = 5.0f;   // Min time between switch rolls per actor
            float retaliateWindowSeconds = 2.0f;  // A hit counts this long after it lands
            float retaliateChance = 0.5f;         // Chance to turn on the attacker
            float retaliateMaxDistance = 600.0f;  // Ignore attackers farther than this (archers/mages)
            float finisherHealthThreshold = 0.25f; // Enemy health fraction that counts as "nearly dead"
            float finisherMaxDistance = 500.0f;   // Only switch to nearby finishers
            float finisherChance = 0.35f;         // Chance to switch to a nearly dead enemy
        };

        // Behavior shaped by the armor class worn on the body slot.
        struct ArmorStyleSettings
        {
            bool enableArmorStyle = true;
            float lightEvasionBonus = 0.15f;   // Light armor: more dodge/strafe/jump/flank
            float heavyEvasionPenalty = 0.2f;  // Heavy armor: less dodge/strafe/jump
            float heavyGuardBonus = 0.15f;     // Heavy armor: more bash/timed block
        };

        // Followers treat the player as their flanking partner.
        // Boss detection (logging only for now): the Boss location ref type, or a listed name
        struct BossSettings
        {
            bool enableBossDetection = true;
            std::string bossNames = "Bandit Chief, Draugr Deathlord"; // comma-separated display names, case-insensitive
            float enragedHealth = 0.5f;           // below this health fraction: enraged
            float desperateHealth = 0.25f;        // below this: desperate (brave: all-in, careful: falls back)
            float cautionFloor = 0.4f;            // a boss's caution never drops below this (outside the desperate phase)
            float skillBonus = 0.15f;             // added to its skill roll (fewer mistakes)
            float powerStaminaMin = 0.4f;         // stamina fraction a boss's power attack needs (if below PowerAttackMinStamina)
            float guardBreakBlockSeconds = 1.0f;  // the target has held its block this long: guard break
        };

        struct TeamworkSettings
        {
            bool enableFollowerPincer = true;
            float followerPincerBonus = 0.3f; // Extra flanking priority when the player engages the target
        };

        // Sidestep projectiles on course to hit the actor (engine threat list).
        struct ProjectileAwarenessSettings
        {
            bool enableProjectileDodge = true;
            float reactMinSeconds = 0.2f;  // Closer than this to impact: too late to react
            float reactMaxSeconds = 0.9f;  // Farther than this: not yet
            float dodgeChance = 0.6f;      // Rolled once per projectile
            float hitMargin = 40.0f;       // Extra radius counted as "on course"
            float dodgePriority = 2.2f;    // Beats melee evasion (~1.5-2.2) and offense
        };

        // Shapes the attacks the vanilla melee AI picks (per-attack chance multipliers).
        struct VanillaAttackSettings
        {
            bool enable = true;
                float openingPowerMult = 2.0f;   // Target staggered, recovering or casting
            float openingNormalMult = 1.5f;
            float blockedPowerMult = 1.5f;   // Target blocking and facing us: power attacks break guard
            float blockedNormalMult = 0.25f; // ... normal attacks just bounce
        };

        static Config &GetInstance()
        {
            static Config instance;
            return instance;
        }

        Config(const Config &) = delete;
        Config &operator=(const Config &) = delete;

        // Load configuration from INI file
        bool Load(const std::string &a_filePath = "Data/SKSE/Plugins/EnhancedCombatBehavior.ini");

        // Save current configuration back to the INI file (used by the runtime menu)
        bool Save(const std::string &a_filePath = "Data/SKSE/Plugins/EnhancedCombatBehavior.ini");

        // Humanizer settings are copied into the Humanizer at init, so changes made at
        // runtime must be re-applied on the game thread. The menu marks them dirty;
        // CombatDirector consumes the flag on its next process tick.
        void MarkHumanizerDirty() { m_humanizerDirty = true; }
        bool ConsumeHumanizerDirty() { return m_humanizerDirty.exchange(false); }

        // Get settings
        const GeneralSettings &GetGeneral() const { return m_general; }

        // Per-actor debug logs only follow the actor selected in the console
        bool IsDebugTarget(RE::FormID a_formID) const
        {
            if (!m_general.enableDebugLog || a_formID == 0) {
                return false;
            }
            const auto selected = RE::Console::GetSelectedRef();
            return selected && selected->GetFormID() == a_formID;
        }
        const HumanizerSettings &GetHumanizer() const { return m_humanizer; }
        const DodgeSystemSettings &GetDodgeSystem() const { return m_dodgeSystem; }
        const DecisionMatrixSettings &GetDecisionMatrix() const { return m_decisionMatrix; }
        const ScoringWeights &GetScoringWeights() const { return m_scoringWeights; }
        const PerformanceSettings &GetPerformance() const { return m_performance; }
        const ModIntegrationSettings &GetModIntegrations() const { return m_modIntegrations; }
        const ParrySettings &GetParry() const { return m_parry; }
        const TimedBlockSettings &GetTimedBlock() const { return m_timedBlock; }
        const CombatPacingSettings &GetCombatPacing() const { return m_combatPacing; }
        const DamageMomentumSettings &GetDamageMomentum() const { return m_damageMomentum; }
        const EnvSensorSettings &GetEnvSensor() const { return m_envSensor; }
        const TargetSwitchingSettings &GetTargetSwitching() const { return m_targetSwitching; }
        const ArmorStyleSettings &GetArmorStyle() const { return m_armorStyle; }
        const TeamworkSettings &GetTeamwork() const { return m_teamwork; }
        const BossSettings &GetBoss() const { return m_boss; }
        const VanillaAttackSettings &GetVanillaAttacks() const { return m_vanillaAttacks; }
        const ProjectileAwarenessSettings &GetProjectileAwareness() const { return m_projectileAwareness; }

        // Presets are JSON files in PresetDirectory, {"Section": {"Key": value}} with the INI's names (plus an optional
        // "Description" text, skipped), holding what differs from the defaults. Applying one resets the tuning sections
        // (not General, Performance or the integrations) to the defaults, then reads the file's values like INI lines.
        // "Default" is built in (reset only). Marks the humanizer dirty; does not save the INI. False (nothing
        // changed) if the file is unusable.
        static constexpr const char *PresetDirectory = "Data/SKSE/Plugins/EnhancedCombatBehavior/Presets";
        bool ApplyPreset(const std::string &a_name);
        // Preset names (file names without .json), sorted
        static std::vector<std::string> ListPresets();
        // Writes the current tuning that differs from the defaults as PresetDirectory/<name>.json (overwrites)
        bool SavePreset(const std::string &a_name) const;

        // Check if plugin is enabled
        bool IsEnabled() const { return m_general.enablePlugin; }

      private:
        Config() = default;
        ~Config() = default;

        // Helper methods to read settings
        void ReadGeneralSettings(CSimpleIniA &a_ini);
        void ReadHumanizerSettings(CSimpleIniA &a_ini);
        void ReadDodgeSystemSettings(CSimpleIniA &a_ini);
        void ReadDecisionMatrixSettings(CSimpleIniA &a_ini);
        void ReadScoringWeightsSettings(CSimpleIniA &a_ini);
        void ReadPerformanceSettings(CSimpleIniA &a_ini);
        void ReadModIntegrationSettings(CSimpleIniA &a_ini);
        void ReadParrySettings(CSimpleIniA &a_ini);
        void ReadTimedBlockSettings(CSimpleIniA &a_ini);
        void ReadCombatPacingSettings(CSimpleIniA &a_ini);
        void ReadDamageMomentumSettings(CSimpleIniA &a_ini);
        void ReadEnvSensorSettings(CSimpleIniA &a_ini);
        void ReadTargetSwitchingSettings(CSimpleIniA &a_ini);
        void ReadArmorStyleSettings(CSimpleIniA &a_ini);
        void ReadTeamworkSettings(CSimpleIniA &a_ini);
        void ReadVanillaAttackSettings(CSimpleIniA &a_ini);
        void ReadProjectileAwarenessSettings(CSimpleIniA &a_ini);
        // Every section reader, in Load's order. Each reader keeps a setting the INI doesn't have.
        void ReadAll(CSimpleIniA &a_ini);
        // Writes every setting into a_ini; returns their (section, key) names
        std::set<std::pair<std::string, std::string>> WriteTo(CSimpleIniA &a_ini) const;

        GeneralSettings m_general;
        HumanizerSettings m_humanizer;
        DodgeSystemSettings m_dodgeSystem;
        DecisionMatrixSettings m_decisionMatrix;
        ScoringWeights m_scoringWeights;
        PerformanceSettings m_performance;
        ModIntegrationSettings m_modIntegrations;
        ParrySettings m_parry;
        TimedBlockSettings m_timedBlock;
        CombatPacingSettings m_combatPacing;
        DamageMomentumSettings m_damageMomentum;
        EnvSensorSettings m_envSensor;
        TargetSwitchingSettings m_targetSwitching;
        ArmorStyleSettings m_armorStyle;
        TeamworkSettings m_teamwork;
        BossSettings m_boss;
        VanillaAttackSettings m_vanillaAttacks;
        ProjectileAwarenessSettings m_projectileAwareness;

        std::atomic<bool> m_humanizerDirty{false};
    };
} // namespace CombatAI

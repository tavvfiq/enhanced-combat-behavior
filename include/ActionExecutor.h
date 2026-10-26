#pragma once

#include "ActorStateData.h"
#include "DecisionResult.h"
#include "DodgeSystem.h"
#include "Intents.h"
#include "ThreadSafeMap.h"
#include "pch.h"
#include <chrono>

namespace CombatAI
{
    // Executes actions (animations, movement)
    class ActionExecutor
    {
      public:
        ActionExecutor() = default;
        ~ActionExecutor() = default;

        // Execute a decision result
        bool Execute(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);


        // True when a sprint attack can launch now: sprinting for a moment (so the graph is
        // in its sprint state), not attacking, inside launch range. Otherwise the caller
        // advances (the NPC sprints there on its own or not at all).
        bool CanLaunchSprintAttack(RE::Actor *a_actor, const ActorStateData &a_state);

        // Drop a pending Fallback/Backoff so a swing in progress isn't dragged backward
        void SuspendRetreat(RE::Actor *a_actor);

        // Sprint-attack launch distance: the INI value, or 3 x weapon reach when it is 0
        static float SprintAttackLaunchDistance(const ActorStateData &a_state);

        void EvictActor(RE::FormID a_formID)
        {
            m_sprintSince.Erase(a_formID);
        }

        // Per-frame jump upkeep: sustain ascent, then release tracking on landing.
        // Call once per frame (CombatDirector::Update).
        void UpdateJumps(float a_deltaTime);

      private:
        // In-flight jump, tracked until the actor lands (port of FJF's AirborneJump)
        // A jump we drive along a planned ballistic arc (game units, seconds). Method after
        // FluidMotionNPC's NPC jumps (own implementation): plan and ray-check the arc first, then place
        // the actor on it every frame instead of launching it and hoping the controller follows.
        static constexpr float kJumpGravity = 900.0f;       // game units/s^2 (FluidMotion's NPC default)
        static constexpr float kJumpSideDistance = 150.0f;  // sideways leap out of a shooter's line
        static constexpr float kJumpSettleSeconds = 0.35f;  // no upward velocity right after touchdown
        static constexpr float kJumpRelandSeconds = 2.0f;   // one JumpLand retry if the graph is stuck
        struct ActiveJump
        {
            RE::ActorHandle handle;
            RE::NiPoint3 start, end, vel; // vel at t = 0
            float T = 0.0f;               // flight time
            float t = 0.0f;
            float settle = -1.0f;         // < 0 airborne, else seconds since touchdown
            bool relanded = false;

            RE::NiPoint3 At(float a_t) const
            {
                return {start.x + vel.x * a_t, start.y + vel.y * a_t, start.z + vel.z * a_t - 0.5f * kJumpGravity * a_t * a_t};
            }
        };

        // Execute bash action
        bool ExecuteBash(RE::Actor *a_actor, const ActorStateData *a_state = nullptr);

        // Execute parry (timed bash for parrying incoming attacks)
        bool ExecuteParry(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute timed block (timed block for blocking incoming attacks)
        bool ExecuteTimedBlock(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute dodge (TK Dodge integration)
        bool ExecuteDodge(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute a directional dodge toward the target for long-range advancing
        bool ExecuteForwardDodge(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        void ConfigureDodgeSystem();

        // Execute strafe movement (Circle node on the brain's side)
        bool ExecuteStrafe(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute flanking movement (Circle node toward the target's side)
        bool ExecuteFlanking(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute retreat movement (Fallback node)
        bool ExecuteRetreat(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute attack
        bool ExecuteAttack(RE::Actor *a_actor);

        // Execute power attack
        bool ExecutePowerAttack(RE::Actor *a_actor, const ActorStateData &a_state);

        // Execute sprint attack
        bool ExecuteSprintAttack(RE::Actor *a_actor, const ActorStateData &a_state);
        bool ExecuteDirectionalDodge(RE::Actor *a_actor, RE::NiPoint3 a_direction);

        // Execute jump: a planned ballistic arc driven per frame, with the graph's jump start/land events
        bool ExecuteJump(RE::Actor *a_actor, const ActorStateData &a_state);

        // Put the controller into its jumping state and apply the launch velocity.
        // Returns the launch vertical speed (Havok units/s), or a negative value on failure.
        bool PlanJumpArc(RE::Actor *a_actor, const RE::NiPoint3 &a_direction, float a_distance, float a_height,
                         ActiveJump &a_out);

        // Execute backoff movement (away from target when they're casting/drawing)
        bool ExecuteBackoff(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute advancing movement (closing distance when too far)
        bool ExecuteAdvancing(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // Execute feint (fake attack to bait enemy response)
        bool ExecuteFeint(RE::Actor *a_actor, const DecisionResult &a_decision, const ActorStateData &a_state);

        // +1 if a_direction circles the target counter-clockwise (left), -1 clockwise, 0 none
        static float CircleSide(const ActorStateData &a_state, const RE::NiPoint3 &a_direction);

        // Post Circle / Fallback / Backoff: a move that keeps the distance on purpose
        void PostSpacingMove(RE::Actor *a_actor, Intents::Move a_kind, float a_param = 0.0f);

        // Helper: Trigger animation graph event
        bool NotifyAnimation(RE::Actor *a_actor, const char *a_eventName);

        // Helper: Set movement via animation graph (if possible)
        bool SetMovementDirection(RE::Actor *a_actor, const RE::NiPoint3 &a_direction, float a_intensity);











        // Dodge system
        DodgeSystem m_dodgeSystem;

        ThreadSafeMap<RE::FormID, ActiveJump> m_activeJumps;
        // When each actor's current sprint started (sprint-attack launch gate)
        ThreadSafeMap<RE::FormID, std::chrono::steady_clock::time_point> m_sprintSince;
    };
} // namespace CombatAI

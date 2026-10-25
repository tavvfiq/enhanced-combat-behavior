#pragma once

#include "pch.h"

// Read-only reader for SCAR action data (phase 1 of driving SCAR / MCO / BFCO movesets from our brain).
// SCAR-patched movesets carry annotations "SCAR_ActionData{json}" on their clips; SCAR reads the active clip's
// annotations each time it starts an attack. Format and range rules follow SCAR 2.0.1 (max-su-2019/SCAR, MIT,
// commit e62b63a: SCARActionData.cpp from_json, Function.cpp WithinAttackRange). Nothing here triggers an attack.
namespace CombatAI::SCARData
{
    struct Action
    {
        float weight = 0.0f; // the annotation's time: higher is tried first
        std::string attackData; // "AttackData": an attack event, e.g. attackStart
        std::string idle;       // "IdleAnimation": an IDLE EditorID instead
        std::string type = "RA"; // RA RPA LA LPA DA DPA BA BPA IDLE
        float minDistance = 0.0f, maxDistance = 150.0f;
        float startAngle = -60.0f, endAngle = 60.0f;
        float chance = 100.0f;
        float triggerStart = 0.0f, triggerEnd = 3.4e38f;
        float weaponLength = -1.0f; // < 0: not set
        float cooldown = -1.0f;
        int variant = 0;
    };

    // Parse the JSON after the "SCAR_ActionData" prefix (flat object of strings and numbers). False if malformed or
    // a required key (AttackData or IdleAnimation, Type, MinDistance, MaxDistance, StartAngle, EndAngle, Chance) is missing.
    bool Parse(std::string_view a_json, Action &a_out);

    // Checks Parse against SCAR's documented examples; logs the result
    bool SelfTest();

    // Actions on the actor's first active clip that has any, sorted by weight (highest first); a_clipTime gets that
    // clip's local time. Takes the graph manager's update lock.
    std::vector<Action> Read(RE::Actor *a_actor, float &a_clipTime);

    // SCAR's range rule, flat: distance to the target within [min (+ both body radii if > 0), max + reach + target
    // radius], and the angle to the target inside [start, end] (degrees, + = to the actor's right)
    bool InRange(const Action &a_action, RE::Actor *a_self, RE::Actor *a_target, float a_reach);

    // Debug (selected NPC): log the actions on its active clip and which ones fit right now
    void Probe(RE::Actor *a_self, RE::Actor *a_target, float a_reach);
} // namespace CombatAI::SCARData

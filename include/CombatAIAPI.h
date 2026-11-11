#pragma once

#include <functional>
#include <windows.h>

/*
 * For modders: Copy this file into your own project if you wish to use this API.
 * V1: decision callback (action, priority, direction, intensity).
 * V2 (1.21.0): V1 plus a callback with the whole decision (DecisionDataV2). Request V2; the V1 calls work on it too.
 */
namespace ECA_API
{
    constexpr const auto PluginName = "EnhancedCombatBehavior";
    constexpr const auto PluginAuthor = "tavvfiq";

    // Available interface versions
    enum class InterfaceVersion : uint8_t
    {
        V1,
        V2
    };

    // Error types that may be returned
    enum class APIResult : uint8_t
    {
        // Your API call was successful
        OK,
        // A callback from this plugin has already been registered
        AlreadyRegistered,
        // A callback from this plugin has not been registered
        NotRegistered,
    };

    // Mirror of internal ActionType
    enum class ActionType : std::uint8_t
    {
        None = 0,
        Retreat,      // 1
        Strafe,       // 2
        Bash,         // 3
        PowerAttack,  // 4
        SprintAttack, // 5
        Attack,       // 6
        Jump,         // 7
        Dodge,        // 8
        Backoff,      // 9
        Advancing,    // 10
        Feint,        // 11
        Flanking,     // 12
        Parry,        // 13
        TimedBlock,   // 14
        ForwardDodge  // 15
    };

    struct Vector3
    {
        float x, y, z;
    };

    // Mirror of internal DecisionResult
    struct DecisionData
    {
        ActionType action;
        float priority;
        Vector3 direction;
        float intensity;
    };

    // V2: the whole executed decision
    struct DecisionDataV2
    {
        ActionType action;
        float priority;
        Vector3 direction;
        float intensity;
        float holdDistance; // Strafe / Flanking: circles no closer than this (0 = no ring)
        bool reflex;        // Skipped the skill roll: a projectile sidestep, or a close dodge timed by skill as timing
        bool paced;         // A pacing wait (waiting its turn while others attack), not the brain's own choice
    };

    // Callback function signature
    // Param 1: the actor that executed the decision
    // Param 2: Decision that was just executed
    // Called on the game thread after the decision runs. Registering or removing callbacks from inside one is allowed.
    using DecisionCallback = std::function<void(RE::Actor *, const DecisionData &)>;
    using DecisionCallbackV2 = std::function<void(RE::Actor *, const DecisionDataV2 &)>;

    // Interface
    class IVCombatAI1
    {
      public:
        virtual ~IVCombatAI1() = default;

        /// <summary>
        /// Registers a callback that will be notified whenever an NPC executes a decision.
        /// </summary>
        /// <param name="a_pluginHandle">Your SKSE Plugin Handle</param>
        /// <param name="a_callback">Your callback function</param>
        virtual APIResult RegisterDecisionCallback(SKSE::PluginHandle a_pluginHandle,
                                                   DecisionCallback &&a_callback) noexcept = 0;

        /// <summary>
        /// Unregisters your decision callback.
        /// </summary>
        virtual APIResult RemoveDecisionCallback(SKSE::PluginHandle a_pluginHandle) noexcept = 0;
    };

    // V2 interface: everything in V1 (same slots first), plus the whole-decision callback (one per plugin, separate
    // from the V1 one)
    class IVCombatAI2 : public IVCombatAI1
    {
      public:
        /// <summary>
        /// Registers a callback that receives the whole decision (DecisionDataV2) whenever an NPC executes one.
        /// </summary>
        virtual APIResult RegisterDecisionCallbackV2(SKSE::PluginHandle a_pluginHandle,
                                                     DecisionCallbackV2 &&a_callback) noexcept = 0;

        /// <summary>
        /// Unregisters your V2 decision callback.
        /// </summary>
        virtual APIResult RemoveDecisionCallbackV2(SKSE::PluginHandle a_pluginHandle) noexcept = 0;
    };

    typedef void *(*_RequestPluginAPI)(const InterfaceVersion interfaceVersion);

    /// <summary>
    /// Request the Combat AI API interface.
    /// Recommended: Call this during your PostLoad message.
    /// </summary>
    [[nodiscard]] inline void *RequestPluginAPI(const InterfaceVersion a_interfaceVersion = InterfaceVersion::V1)
    {
        auto pluginHandle = GetModuleHandleA(PluginName);
        _RequestPluginAPI requestAPIFunction = (_RequestPluginAPI)GetProcAddress(pluginHandle, "RequestPluginAPI");
        if (requestAPIFunction) {
            return requestAPIFunction(a_interfaceVersion);
        }
        return nullptr;
    }
} // namespace ECA_API

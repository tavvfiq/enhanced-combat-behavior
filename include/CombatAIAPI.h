#pragma once

#include <functional>
#include <windows.h>

/*
 * For modders: Copy this file into your own project if you wish to use this API
 */
namespace ECA_API
{
    constexpr const auto PluginName = "EnhancedCombatAI";
    constexpr const auto PluginAuthor = "tavvfiq";

    // Available interface versions
    enum class InterfaceVersion : uint8_t
    {
        V1
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

    // Callback function signature
    // Param 1: Actor FormID (Raw pointer might be unsafe across DLLs if layout differs, but standard for SKSE)
    // Param 2: Decision that was just executed
    using DecisionCallback = std::function<void(RE::Actor *, const DecisionData &)>;

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

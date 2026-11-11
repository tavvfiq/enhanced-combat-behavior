#pragma once

#include "CombatAIAPI.h"
#include "DecisionResult.h"
#include "ThreadSafeMap.h"
#include <shared_mutex>
#include <unordered_map>

namespace CombatAI
{
    class APIManager : public ECA_API::IVCombatAI2
    {
      public:
        static APIManager *GetSingleton();

        // IVCombatAI1 Implementation
        ECA_API::APIResult RegisterDecisionCallback(SKSE::PluginHandle a_pluginHandle,
                                                    ECA_API::DecisionCallback &&a_callback) noexcept override;
        ECA_API::APIResult RemoveDecisionCallback(SKSE::PluginHandle a_pluginHandle) noexcept override;

        // IVCombatAI2 Implementation
        ECA_API::APIResult RegisterDecisionCallbackV2(SKSE::PluginHandle a_pluginHandle,
                                                      ECA_API::DecisionCallbackV2 &&a_callback) noexcept override;
        ECA_API::APIResult RemoveDecisionCallbackV2(SKSE::PluginHandle a_pluginHandle) noexcept override;

        // Internal Interface
        void NotifyDecision(RE::Actor *a_actor, const DecisionResult &a_result);

      private:
        APIManager() = default;
        ~APIManager() = default;
        APIManager(const APIManager &) = delete;
        APIManager &operator=(const APIManager &) = delete;

        mutable std::shared_mutex m_mutex;
        std::unordered_map<SKSE::PluginHandle, ECA_API::DecisionCallback> m_callbacks;
        std::unordered_map<SKSE::PluginHandle, ECA_API::DecisionCallbackV2> m_callbacksV2;
    };
} // namespace CombatAI

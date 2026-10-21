#include "APIManager.h"
#include "pch.h"

namespace CombatAI
{
    APIManager *APIManager::GetSingleton()
    {
        static APIManager singleton;
        return &singleton;
    }

    ECA_API::APIResult APIManager::RegisterDecisionCallback(SKSE::PluginHandle a_pluginHandle,
                                                            ECA_API::DecisionCallback &&a_callback) noexcept
    {
        std::unique_lock lock(m_mutex);
        if (m_callbacks.find(a_pluginHandle) != m_callbacks.end()) {
            return ECA_API::APIResult::AlreadyRegistered;
        }

        m_callbacks.emplace(a_pluginHandle, std::move(a_callback));
        return ECA_API::APIResult::OK;
    }

    ECA_API::APIResult APIManager::RemoveDecisionCallback(SKSE::PluginHandle a_pluginHandle) noexcept
    {
        std::unique_lock lock(m_mutex);
        if (m_callbacks.erase(a_pluginHandle) == 0) {
            return ECA_API::APIResult::NotRegistered;
        }
        return ECA_API::APIResult::OK;
    }

    void APIManager::NotifyDecision(RE::Actor *a_actor, const DecisionResult &a_result)
    {
        if (m_callbacks.empty()) {
            return;
        }

        // Convert internal result to public API data
        ECA_API::DecisionData data;
        data.action = static_cast<ECA_API::ActionType>(a_result.action);
        data.priority = a_result.priority;
        data.direction = {a_result.direction.x, a_result.direction.y, a_result.direction.z};
        data.intensity = a_result.intensity;

        // Broadcast to all listeners
        // Use shared lock for reading the map
        std::shared_lock lock(m_mutex);
        for (const auto &[handle, callback] : m_callbacks) {
            if (callback) {
                // Listener callbacks are third-party code: keep a C++ exception from escaping into the game
                try {
                    callback(a_actor, data);
                } catch (...) {
                    // Log error? For now, swallow to protect our own stability
                }
            }
        }
    }
} // namespace CombatAI

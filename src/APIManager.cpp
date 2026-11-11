#include "APIManager.h"
#include "pch.h"

namespace CombatAI
{
    // The public ActionType mirrors the internal one value for value; a new internal action must be added to both
    static_assert(static_cast<int>(ECA_API::ActionType::ForwardDodge) == static_cast<int>(ActionType::ForwardDodge));

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

    ECA_API::APIResult APIManager::RegisterDecisionCallbackV2(SKSE::PluginHandle a_pluginHandle,
                                                              ECA_API::DecisionCallbackV2 &&a_callback) noexcept
    {
        std::unique_lock lock(m_mutex);
        return m_callbacksV2.try_emplace(a_pluginHandle, std::move(a_callback)).second
                   ? ECA_API::APIResult::OK
                   : ECA_API::APIResult::AlreadyRegistered;
    }

    ECA_API::APIResult APIManager::RemoveDecisionCallbackV2(SKSE::PluginHandle a_pluginHandle) noexcept
    {
        std::unique_lock lock(m_mutex);
        return m_callbacksV2.erase(a_pluginHandle) ? ECA_API::APIResult::OK : ECA_API::APIResult::NotRegistered;
    }

    void APIManager::NotifyDecision(RE::Actor *a_actor, const DecisionResult &a_result)
    {
        // Copy the listeners under the lock and call them outside it: a callback that registers or removes one would
        // otherwise deadlock on m_mutex
        std::vector<ECA_API::DecisionCallback> v1;
        std::vector<ECA_API::DecisionCallbackV2> v2;
        {
            std::shared_lock lock(m_mutex);
            if (m_callbacks.empty() && m_callbacksV2.empty()) {
                return;
            }
            for (const auto &[handle, callback] : m_callbacks) {
                if (callback) {
                    v1.push_back(callback);
                }
            }
            for (const auto &[handle, callback] : m_callbacksV2) {
                if (callback) {
                    v2.push_back(callback);
                }
            }
        }

        // Convert internal result to public API data
        const ECA_API::Vector3 direction{a_result.direction.x, a_result.direction.y, a_result.direction.z};
        const ECA_API::DecisionData data{static_cast<ECA_API::ActionType>(a_result.action), a_result.priority,
                                         direction, a_result.intensity};
        const ECA_API::DecisionDataV2 dataV2{data.action,           data.priority,   direction,      data.intensity,
                                             a_result.holdDistance, a_result.reflex, a_result.paced};

        // Listener callbacks are third-party code: keep a C++ exception from escaping into the game
        for (const auto &callback : v1) {
            try {
                callback(a_actor, data);
            } catch (...) {
            }
        }
        for (const auto &callback : v2) {
            try {
                callback(a_actor, dataV2);
            } catch (...) {
            }
        }
    }
} // namespace CombatAI

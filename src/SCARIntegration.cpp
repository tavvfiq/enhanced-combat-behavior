#include "SCARIntegration.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Logger.h"
#include "pch.h"

namespace CombatAI
{
    namespace
    {
        // Gap in our offense's attack wish that starts a new stall wait (covers LOD intervals)
        constexpr float kWantGapSeconds = 1.0f;
    } // namespace

    void SCARIntegration::Initialize()
    {
        m_isAvailable = GetModuleHandleA("SCAR.dll") != nullptr;
        if (m_isAvailable) {
            LOG_INFO("SCAR detected - patched movesets keep SCAR's attack selection");
        }
    }

    void SCARIntegration::Track(RE::Actor *a_actor, RE::FormID a_formID)
    {
        if (!m_isAvailable || !a_actor || !Config::GetInstance().GetModIntegrations().enableSCARIntegration) {
            return;
        }

        // Idempotent; re-adds after the graph is rebuilt (cell reload, race change)
        a_actor->AddAnimationGraphEventSink(this);

        const auto attackState = ActorUtils::SafeGetAttackState(a_actor);
        if (attackState != RE::ATTACK_STATE_ENUM::kNone && attackState != RE::ATTACK_STATE_ENUM::kDraw) {
            const float now = m_time.load(std::memory_order_relaxed);
            m_states.ModifyOrCreate(a_formID, [&](ActorSCARState &s) { s.lastAttackTime = now; });
        }
    }

    bool SCARIntegration::OwnsAttacks(RE::FormID a_formID)
    {
        const auto &settings = Config::GetInstance().GetModIntegrations();
        if (!m_isAvailable || !settings.enableSCARIntegration) {
            return false;
        }

        const float now = m_time.load(std::memory_order_relaxed);
        bool owns = false;
        m_states.Modify(a_formID, [&](ActorSCARState &s) {
            if (now - s.lastComboTime > settings.scarComboWindowSeconds) {
                return; // Moveset not SCAR-patched (or SCAR stopped driving it)
            }
            // Start a new wait unless we have been wanting to attack continuously with no
            // attack in between
            if (s.wantSince < 0.0f || s.lastAttackTime >= s.wantSince || now - s.lastWantTime > kWantGapSeconds) {
                s.wantSince = now;
            }
            s.lastWantTime = now;
            owns = now - s.wantSince < settings.scarStallSeconds;
        });
        return owns;
    }

    bool SCARIntegration::IsDriving(RE::FormID a_formID) const
    {
        const auto &settings = Config::GetInstance().GetModIntegrations();
        if (!m_isAvailable || !settings.enableSCARIntegration) {
            return false;
        }
        const auto state = m_states.Find(a_formID);
        return state && m_time.load(std::memory_order_relaxed) - state->lastComboTime <= settings.scarComboWindowSeconds;
    }

    void SCARIntegration::Update(float a_deltaTime)
    {
        m_time.store(m_time.load(std::memory_order_relaxed) + a_deltaTime, std::memory_order_relaxed);
    }

    RE::BSEventNotifyControl SCARIntegration::ProcessEvent(const RE::BSAnimationGraphEvent *a_event,
                                                           RE::BSTEventSource<RE::BSAnimationGraphEvent> *)
    {
        if (a_event && a_event->holder && a_event->tag == "SCAR_ComboStart") {
            const RE::FormID formID = a_event->holder->GetFormID();
            const float now = m_time.load(std::memory_order_relaxed);
            bool first = false;
            m_states.ModifyOrCreate(formID, [&](ActorSCARState &s) {
                first = s.lastComboTime < 0.0f;
                s.lastComboTime = now;
                s.lastAttackTime = now;
            });
            if (first && Config::GetInstance().IsDebugTarget(formID)) {
                LOG_DEBUG("SCAR moveset active on 0x{:08X}", static_cast<std::uint32_t>(formID));
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
} // namespace CombatAI

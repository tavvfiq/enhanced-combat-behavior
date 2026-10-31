// Attack Warp integration: see AttackWarpIntegration.h. API from Attack Warp 0.5.0 (include/AttackWarpAPI.h, copied from
// D:\Modding\AttackWarp\src\API).
#include "AttackWarpIntegration.h"
#include "ActorStateData.h"
#include "Boss.h"
#include "Config.h"
#include "Logger.h"
#include "Personality.h"
#include "pch.h"
#include <Windows.h>

#include "AttackWarpAPI.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace CombatAI::AttackWarp
{
    namespace
    {
        ATTACKWARP_API::IVAttackWarp1 *g_api = nullptr;

        // Tuning (starting values to tune; skill runs about 0.75-0.95 today):
        //   turnRate = 1 + 0.6 (skill - 0.85): the INI rate for an average NPC, 0.94-1.06 across today's range
        //   lead     = (skill - 0.6) / 0.35, 0..1: a level-1 bandit aims about 40% as far ahead as the INI
        //   stretch  = 0.7 + 0.6 risk (Personality::Effective, which already adds an enraged boss's +0.2 risk)
        //   boss: enraged turn x1.15, lead x1.2; desperate and all-in turn x1.25, lead x1.2 (stretch through risk)
        //   waiting its turn: stretch 0 (a waiter's moveset swing must not reach from the waiting ring)
        constexpr float kSkillMid = 0.85f;
        constexpr float kResend = 0.05f;    // a field changed by more than this is sent again
        constexpr float kCheckEvery = 5.0f; // seconds between skill / personality re-checks (a held flip goes at once)

        struct Sent
        {
            ATTACKWARP_API::Tuning tuning;
            bool held = false;
            float checkedAt = -1000.0f;
        };

        // One swing being read back (an NPC's, or the player's)
        struct Swing
        {
            std::uint32_t id = 0;
            bool power = false;
            float startDist = -1.0f;
            float aimAtLock = 1000.0f; // first Locked sample's aim error (1000 = never seen locked)
            float aimLast = 0.0f;
            float stretchMax = 1.0f;
            float timeToHitFirst = -1.0f;
            int tracking = 0, locked = 0, holding = 0; // samples per phase
            int hits = 0, blocked = 0;
        };

        struct Summary
        {
            int swings = 0, hit = 0, blocked = 0, missWideAtLock = 0, missWideAfterLock = 0, missFar = 0;
            double stretchSum = 0.0;
            int playerSwings = 0, playerTracking = 0, playerLocked = 0, playerNoPhase = 0;
            int playerHitsTaken = 0;
            float playerHealthLost = 0.0f;
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, Sent> g_sent;
        std::unordered_map<RE::FormID, Swing> g_swings;
        Summary g_summary;
        std::atomic<float> g_now{0.0f}; // written by Tick (main thread), read by Observe (brain threads)
        float g_summaryAt = 0.0f;
        float g_playerHealth = -1.0f;

        constexpr float kWideAim = 60.0f; // degrees off the aim point: wide even for Precision (ECB's hit cone override)

        bool Enabled() { return g_api && Config::GetInstance().GetModIntegrations().enableAttackWarpIntegration; }

        ATTACKWARP_API::Tuning Wanted(RE::Actor *a_actor, float a_skill, bool a_held)
        {
            ATTACKWARP_API::Tuning t;
            t.turnRate = std::clamp(1.0f + 0.6f * (a_skill - kSkillMid), 0.5f, 1.5f);
            t.lead = std::clamp((a_skill - 0.6f) / 0.35f, 0.0f, 1.0f);
            t.stretch = a_held ? 0.0f : 0.7f + 0.6f * Personality::Effective(a_actor).risk;
            switch (Boss::CurrentPhase(a_actor)) {
            case Boss::Phase::Enraged:
                t.turnRate *= 1.15f;
                t.lead *= 1.2f;
                break;
            case Boss::Phase::Desperate:
                if (Boss::Brave(a_actor)) {
                    t.turnRate *= 1.25f;
                    t.lead *= 1.2f;
                }
                break;
            default:
                break;
            }
            return t;
        }

        bool Differs(const ATTACKWARP_API::Tuning &a_a, const ATTACKWARP_API::Tuning &a_b)
        {
            return std::fabs(a_a.turnRate - a_b.turnRate) > kResend || std::fabs(a_a.lead - a_b.lead) > kResend ||
                   std::fabs(a_a.stretch - a_b.stretch) > kResend;
        }

        void Sample(Swing &a_swing, const ATTACKWARP_API::SwingInfo &a_info)
        {
            using P = ATTACKWARP_API::Phase;
            a_swing.aimLast = a_info.aimErrorDeg;
            a_swing.stretchMax = (std::max)(a_swing.stretchMax, a_info.stretch);
            if (a_swing.timeToHitFirst < 0.0f && a_info.timeToHit >= 0.0f) {
                a_swing.timeToHitFirst = a_info.timeToHit;
            }
            switch (a_info.phase) {
            case P::Tracking:
                ++a_swing.tracking;
                break;
            case P::Locked:
                if (a_swing.locked++ == 0) {
                    a_swing.aimAtLock = a_info.aimErrorDeg;
                }
                break;
            case P::Holding:
                ++a_swing.holding;
                break;
            default:
                break;
            }
        }

        // A swing is over (a new swingId, or the actor stopped swinging): count it, log it for the selected NPC
        void Close(RE::FormID a_id, const Swing &a_swing, bool a_player)
        {
            if (a_swing.id == 0) {
                return;
            }
            if (a_player) {
                ++g_summary.playerSwings;
                g_summary.playerTracking += a_swing.tracking > 0;
                g_summary.playerLocked += a_swing.locked > 0;
                g_summary.playerNoPhase += a_swing.tracking + a_swing.locked + a_swing.holding == 0;
            } else {
                ++g_summary.swings;
                g_summary.stretchSum += a_swing.stretchMax;
                if (a_swing.hits > 0) {
                    ++g_summary.hit;
                } else if (a_swing.blocked > 0) {
                    ++g_summary.blocked;
                } else if (a_swing.locked > 0 && std::fabs(a_swing.aimAtLock) > kWideAim) {
                    ++g_summary.missWideAtLock; // turned too slowly: already wide when it locked
                } else if (std::fabs(a_swing.aimLast) > kWideAim) {
                    ++g_summary.missWideAfterLock; // the target got out of the line after the lock
                } else {
                    ++g_summary.missFar;
                }
            }
            if (a_player || Config::GetInstance().IsDebugTarget(a_id)) {
                const char *outcome = a_swing.hits ? "hit" : a_swing.blocked ? "blocked" : "missed";
                LOG_DEBUG("Warp 0x{:08X}: swing {} {}, start {:.0f}; samples tracking {} / locked {} / holding {}; aim at lock {}, "
                          "last {:+.0f}; stretch max {:.2f}; time to hit first {:.2f}; {}",
                          static_cast<std::uint32_t>(a_id), a_swing.id, a_swing.power ? "power" : "light", a_swing.startDist,
                          a_swing.tracking, a_swing.locked, a_swing.holding,
                          a_swing.locked ? std::format("{:+.0f}", a_swing.aimAtLock) : std::string("never"), a_swing.aimLast,
                          a_swing.stretchMax, a_swing.timeToHitFirst, outcome);
            }
        }

        // Read a_actor's swing; closes the previous one when the id changes or the phase falls to None
        void Read(RE::Actor *a_actor, bool a_player)
        {
            ATTACKWARP_API::SwingInfo info{};
            if (!g_api->GetSwingInfo(a_actor->GetHandle(), info)) {
                return;
            }
            const auto id = a_actor->GetFormID();
            std::scoped_lock guard(g_lock);
            auto &swing = g_swings[id];
            const bool none = info.phase == ATTACKWARP_API::Phase::None;
            if (swing.id != 0 && (info.swingId != swing.id || none)) {
                Close(id, swing, a_player);
                swing = {};
            }
            if (!none && info.swingId != 0) {
                if (swing.id == 0) {
                    swing.id = info.swingId;
                    swing.power = info.power;
                    swing.startDist = info.startDist;
                }
                Sample(swing, info);
            }
        }
    } // namespace

    void Initialize()
    {
        if (!Config::GetInstance().GetModIntegrations().enableAttackWarpIntegration) {
            LOG_INFO("Attack Warp: integration off in the INI");
            return;
        }
        g_api = static_cast<ATTACKWARP_API::IVAttackWarp1 *>(ATTACKWARP_API::RequestPluginAPI(ATTACKWARP_API::InterfaceVersion::V1));
        LOG_INFO("Attack Warp: {}", g_api ? "API V1 found - per-NPC tracking, no stretch while waiting a turn, swing read-back"
                                          : "not loaded (or no V1 API) - integration off");
    }

    bool Available() { return Enabled(); }

    void Observe(RE::Actor *a_actor, [[maybe_unused]] const ActorStateData &a_state, float a_skill, bool a_held)
    {
        if (!a_actor || !Enabled() || a_actor->IsPlayerRef()) {
            return;
        }
        const auto id = a_actor->GetFormID();
        bool send = false;
        ATTACKWARP_API::Tuning wanted;
        {
            std::scoped_lock guard(g_lock);
            auto [it, fresh] = g_sent.try_emplace(id);
            auto &sent = it->second;
            const float now = g_now.load();
            if (fresh || sent.held != a_held || now - sent.checkedAt >= kCheckEvery) {
                sent.checkedAt = now;
                wanted = Wanted(a_actor, a_skill, a_held);
                send = fresh || sent.held != a_held || Differs(wanted, sent.tuning);
                if (send) {
                    sent.tuning = wanted;
                    sent.held = a_held;
                }
            }
        }
        if (send) {
            const auto result = g_api->SetTuning(a_actor->GetHandle(), wanted);
            if (Config::GetInstance().IsDebugTarget(id)) {
                LOG_DEBUG("Warp 0x{:08X}: tuning turn x{:.2f}, lead x{:.2f}, stretch x{:.2f}{} (skill {:.2f}){}",
                          static_cast<std::uint32_t>(id), wanted.turnRate, wanted.lead, wanted.stretch,
                          a_held ? " - waiting its turn, no stretch" : "", a_skill,
                          result == ATTACKWARP_API::APIResult::OK ? "" : " - refused");
            }
        }
        Read(a_actor, false);
    }

    void Tick(float a_deltaTime)
    {
        if (!Enabled()) {
            return;
        }
        g_now.store(g_now.load() + a_deltaTime);
        if (auto *player = RE::PlayerCharacter::GetSingleton(); player && player->IsInCombat()) {
            Read(player, true);
            const float health = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            std::scoped_lock guard(g_lock);
            if (g_playerHealth >= 0.0f && health < g_playerHealth) {
                g_summary.playerHealthLost += g_playerHealth - health;
            }
            g_playerHealth = health;
        } else {
            g_playerHealth = -1.0f;
        }
        if (g_now.load() - g_summaryAt < 60.0f) {
            return;
        }
        g_summaryAt = g_now.load();
        Summary s;
        {
            std::scoped_lock guard(g_lock);
            s = g_summary;
            g_summary = {};
        }
        if (s.swings == 0 && s.playerSwings == 0 && s.playerHitsTaken == 0) {
            return;
        }
        LOG_INFO("Attack Warp (last 60s): NPC swings {} - hit {}, blocked {}, missed wide at the lock {}, wide after the lock {}, "
                 "short {}; mean stretch {:.2f}; player swings {} (tracking seen {}, locked seen {}, no phase {}); player hit {} "
                 "times, lost {:.0f} health",
                 s.swings, s.hit, s.blocked, s.missWideAtLock, s.missWideAfterLock, s.missFar,
                 s.swings ? s.stretchSum / s.swings : 0.0, s.playerSwings, s.playerTracking, s.playerLocked, s.playerNoPhase,
                 s.playerHitsTaken, s.playerHealthLost);
    }

    void NoteHit(RE::Actor *a_attacker, RE::Actor *a_victim, bool a_blocked)
    {
        if (!a_attacker || !a_victim || !Enabled()) {
            return;
        }
        std::scoped_lock guard(g_lock);
        if (a_victim->IsPlayerRef() && !a_blocked) {
            ++g_summary.playerHitsTaken;
        }
        if (const auto it = g_swings.find(a_attacker->GetFormID()); it != g_swings.end() && it->second.id != 0) {
            ++(a_blocked ? it->second.blocked : it->second.hits);
        }
    }

    void Evict(RE::FormID a_formID)
    {
        if (!g_api) {
            return;
        }
        bool had = false;
        {
            std::scoped_lock guard(g_lock);
            had = g_sent.erase(a_formID) > 0;
            g_swings.erase(a_formID);
        }
        if (had) {
            if (auto *actor = RE::TESForm::LookupByID<RE::Actor>(a_formID)) {
                g_api->ClearTuning(actor->GetHandle());
            }
        }
    }
} // namespace CombatAI::AttackWarp

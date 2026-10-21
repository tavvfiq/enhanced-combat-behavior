#include "Intents.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

namespace CombatAI::Intents
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using Seconds = std::chrono::duration<float>;

        // How long an untaken intent stays valid. The tree re-checks every 0.05 s when idle,
        // so these only cover the wait for a running node (e.g. an earlier swing) to finish.
        constexpr Seconds kAttackTTL{0.5f};
        constexpr Seconds kDefendTTL{0.3f};
        // How long a taken attack kind steers the attack-chance hook (StartAttack runs right
        // after the branch is taken; this only has to outlast that one call).
        constexpr Seconds kCommitWindow{1.0f};

        struct Entry
        {
            Attack attack = Attack::None;
            Clock::time_point attackUntil{};
            Defend defend = Defend::None;
            Clock::time_point defendUntil{};
            Attack committed = Attack::None;
            Clock::time_point committedUntil{};
        };

        ThreadSafeMap<RE::FormID, Entry> g_board;

        void Debug(RE::Actor *a_actor, const char *a_what, int a_kind)
        {
            if (Config::GetInstance().IsDebugTarget(a_actor->GetFormID())) {
                LOG_DEBUG("Intent 0x{:08X}: {} {}", static_cast<std::uint32_t>(a_actor->GetFormID()), a_what, a_kind);
            }
        }
    } // namespace

    bool PostAttack(RE::Actor *a_actor, Attack a_kind)
    {
        if (!a_actor) {
            return false;
        }
        const auto now = Clock::now();
        bool posted = false;
        g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) {
            if (e.attack == a_kind && now < e.attackUntil) {
                return; // still waiting for the tree
            }
            e.attack = a_kind;
            e.attackUntil = now + std::chrono::duration_cast<Clock::duration>(kAttackTTL);
            posted = true;
        });
        if (posted) {
            Debug(a_actor, "post attack", static_cast<int>(a_kind));
        }
        return posted;
    }

    bool PostDefend(RE::Actor *a_actor, Defend a_kind)
    {
        if (!a_actor) {
            return false;
        }
        const auto now = Clock::now();
        bool posted = false;
        g_board.ModifyOrCreate(a_actor->GetFormID(), [&](Entry &e) {
            if (e.defend == a_kind && now < e.defendUntil) {
                return; // still waiting for the tree
            }
            e.defend = a_kind;
            e.defendUntil = now + std::chrono::duration_cast<Clock::duration>(kDefendTTL);
            posted = true;
        });
        if (posted) {
            Debug(a_actor, "post defend", static_cast<int>(a_kind));
        }
        return posted;
    }

    Attack TakeAttack(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return Attack::None;
        }
        Attack taken = Attack::None;
        const auto now = Clock::now();
        g_board.Modify(a_actor->GetFormID(), [&](Entry &e) {
            if (e.attack != Attack::None && now < e.attackUntil) {
                taken = e.attack;
                e.committed = taken;
                e.committedUntil = now + std::chrono::duration_cast<Clock::duration>(kCommitWindow);
            }
            e.attack = Attack::None;
        });
        if (taken != Attack::None) {
            Debug(a_actor, "tree took attack", static_cast<int>(taken));
        }
        return taken;
    }

    bool TakeDefend(RE::Actor *a_actor, Defend a_kind)
    {
        if (!a_actor) {
            return false;
        }
        bool taken = false;
        const auto now = Clock::now();
        g_board.Modify(a_actor->GetFormID(), [&](Entry &e) {
            if (e.defend == a_kind && now < e.defendUntil) {
                taken = true;
                e.defend = Defend::None;
            }
        });
        if (taken) {
            Debug(a_actor, "tree took defend", static_cast<int>(a_kind));
        }
        return taken;
    }

    bool HasPending(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return false;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        const auto now = Clock::now();
        return e && ((e->attack != Attack::None && now < e->attackUntil) ||
                     (e->defend != Defend::None && now < e->defendUntil));
    }

    Attack CommittedAttack(RE::Actor *a_actor)
    {
        if (!a_actor) {
            return Attack::None;
        }
        const auto e = g_board.Find(a_actor->GetFormID());
        return e && Clock::now() < e->committedUntil ? e->committed : Attack::None;
    }

    void Evict(RE::FormID a_formID) { g_board.Erase(a_formID); }
} // namespace CombatAI::Intents

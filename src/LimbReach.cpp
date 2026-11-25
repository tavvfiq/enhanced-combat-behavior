// LimbReach: measures each beast attack's reach from its hand bones at the first blow (see LimbReach.h).
#include "LimbReach.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Intents.h"
#include "Logger.h"
#include "PrecisionIntegration.h"
#include "ThreadSafeMap.h"

namespace CombatAI::LimbReach
{
    namespace
    {
        // How far past the hand bone a blow still lands. 40 was too much: trolls (hands 76-112 out) missed from
        // 140-170 (1.22.0 run 3). 0 was too strict: vanilla parks a werewolf 3-15 past its hand's reach, so it stood
        // there and never swung (run 9). Werewolf blows landed up to 29 past the hand, trolls' about 20.
        // shortcut: one allowance for every beast; learn it per race from the blows' gap (Limb lines) if one misses
        // at the edge or stands short
        constexpr float kHandAllowance = 20.0f;
        constexpr std::size_t kSamples = 5;

        // Case-insensitive FNV-1a: attack data events and graph events spell the same name in different cases
        std::uint32_t Hash(std::string_view a_text)
        {
            std::uint32_t h = 2166136261u;
            for (const char c : a_text) {
                h = (h ^ static_cast<std::uint8_t>(std::tolower(static_cast<unsigned char>(c)))) * 16777619u;
            }
            return h;
        }

        std::uint64_t Key(const RE::TESRace *a_race, std::string_view a_event)
        {
            return (static_cast<std::uint64_t>(a_race->GetFormID()) << 32) | Hash(a_event);
        }

        float Median(std::vector<float> a_values)
        {
            std::ranges::sort(a_values);
            return a_values[a_values.size() / 2];
        }

        struct Swing
        {
            std::string event;
            bool measured = true; // no swing pending
            std::chrono::steady_clock::time_point started{};
        };
        ThreadSafeMap<RE::FormID, Swing> g_swings;                    // per actor: the swing awaiting its first blow
        ThreadSafeMap<std::uint64_t, std::vector<float>> g_reach;     // per race + event: reach per unit of scale
        ThreadSafeMap<std::uint64_t, std::vector<float>> g_startup;   // per race + event: seconds to the first blow

        // The farther hand's horizontal distance from the actor's center; 0 when no hand bone is found
        float HandDistance(RE::Actor *a_actor)
        {
            auto *root = a_actor->Get3D(false);
            if (!root) {
                return 0.0f;
            }
            // Werewolves name their left hand "[RHnd]" too (Precision_base.toml uses the same names)
            static const RE::BSFixedString kHands[] = {"NPC R Hand [RHnd]", "NPC L Hand [LHnd]", "NPC L Hand [RHnd]"};
            const RE::NiPoint3 center = a_actor->GetPosition();
            float farthest = 0.0f;
            for (const auto &name : kHands) {
                if (const auto *hand = root->GetObjectByName(name)) {
                    const float dx = hand->world.translate.x - center.x, dy = hand->world.translate.y - center.y;
                    farthest = (std::max)(farthest, std::hypot(dx, dy));
                }
            }
            return farthest;
        }

        void Push(std::vector<float> &a_samples, float a_value)
        {
            a_samples.push_back(a_value);
            if (a_samples.size() > kSamples) {
                a_samples.erase(a_samples.begin());
            }
        }

        void OnHitFrame(RE::Actor *a_actor)
        {
            std::string event;
            float startup = 0.0f;
            g_swings.Modify(a_actor->GetFormID(), [&](Swing &a_swing) {
                if (!a_swing.measured) {
                    a_swing.measured = true; // first blow only: a combo's later blows come after it stepped in
                    event = a_swing.event;
                    startup = std::chrono::duration<float>(std::chrono::steady_clock::now() - a_swing.started).count();
                }
            });
            RE::TESRace *race = a_actor->GetRace();
            const float scale = a_actor->GetScale();
            if (event.empty() || !race || scale <= 0.0f) {
                return;
            }
            const float hand = HandDistance(a_actor);
            const bool debug = Config::GetInstance().IsDebugTarget(a_actor->GetFormID());
            if (hand <= 0.0f) {
                if (debug) {
                    LOG_DEBUG("Limb 0x{:08X}: '{}' blow, no hand bone found - not measured", a_actor->GetFormID(), event);
                }
                return;
            }
            std::size_t count = 0;
            float median = 0.0f;
            g_reach.ModifyOrCreate(Key(race, event), [&](std::vector<float> &a_samples) {
                Push(a_samples, hand / scale);
                count = a_samples.size();
                median = Median(a_samples);
            });
            if (startup > 0.0f && startup < 2.5f) {
                g_startup.ModifyOrCreate(Key(race, event), [&](std::vector<float> &a_samples) { Push(a_samples, startup); });
            }
            if (debug) {
                // Where the target stood at the blow: with the Probe Hit lines after it, shows where contact stops
                std::string target = "no target";
                if (auto *combat = ActorUtils::SafeGetCombatController(a_actor)) {
                    if (const auto other = combat->targetHandle.get()) {
                        const float body = ActorUtils::GetBodyRadius(other.get());
                        const float gap = a_actor->GetPosition().GetDistance(other->GetPosition()) - body - hand;
                        target = std::format("target 0x{:08X} body {:.0f} at {:.0f} deg, its body {:.0f} {} the hand",
                                             other->GetFormID(), body, a_actor->GetHeadingAngle(other->GetPosition(), false),
                                             std::abs(gap), gap > 0.0f ? "past" : "inside");
                    }
                }
                LOG_DEBUG("Limb 0x{:08X}: '{}' first blow {:.2f}s in, hand {:.0f} out ({} {} swings, median {:.0f}; longest {:.0f}); {}",
                          a_actor->GetFormID(), event, startup, hand, race->GetFormEditorID() ? race->GetFormEditorID() : "",
                          count, median * scale, LongestReach(a_actor), target);
            }
        }

        struct AnimEvent
        {
            static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent> *a_sink,
                                                  RE::BSAnimationGraphEvent *a_event,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent> *a_source)
            {
                if (a_event && a_event->holder && _stricmp(a_event->tag.c_str(), "HitFrame") == 0) {
                    // The sink is Character's, so the holder is an actor
                    if (auto *actor = const_cast<RE::TESObjectREFR *>(a_event->holder)->As<RE::Actor>(); actor && Active(actor)) {
                        OnHitFrame(actor);
                    }
                }
                return func(a_sink, a_event, a_source);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        bool SelfTest()
        {
            const bool ok = Hash("attackStartPowerComboA") == Hash("AttackStartPowerComboA") &&
                            Hash("attackStartLeftA") != Hash("attackStartLeftB") && Median({3, 1, 2}) == 2.0f &&
                            Median({4, 1, 3, 2}) == 3.0f;
            if (!ok) {
                LOG_ERROR("Limb reach: self-test failed, limb reach is off");
            }
            return ok;
        }
    } // namespace

    void Install()
    {
        if (!SelfTest()) {
            return;
        }
        REL::Relocation<std::uintptr_t> sink{RE::Character::VTABLE[2]};
        AnimEvent::func = sink.write_vfunc(0x1, AnimEvent::thunk);
        LOG_INFO("Limb reach: HitFrame hooked (beast attacks' hand reach, used with Precision)");
    }

    bool Active(RE::Actor *a_actor)
    {
        return a_actor && Config::GetInstance().GetDecisionMatrix().learnUnarmedReach &&
               PrecisionIntegration::GetInstance().IsAvailable() && ActorUtils::IsBeastCreature(a_actor);
    }

    void NoteAttackStart(RE::Actor *a_actor, std::string_view a_event)
    {
        if (Active(a_actor)) {
            g_swings.ModifyOrCreate(a_actor->GetFormID(), [&](Swing &a_swing) {
                a_swing.event = a_event;
                a_swing.measured = false;
                a_swing.started = std::chrono::steady_clock::now();
            });
        }
    }

    bool CreatureStillSwinging(RE::Actor *a_actor)
    {
        if (!ActorUtils::IsBrainCreature(a_actor)) {
            return false;
        }
        if (Active(a_actor)) {
            // Werewolves under the fixed 3 s started every attack 3.0-3.1 s after the last (1.22.0 run 6): no combos.
            // 2.5 s: a swing whose HitFrame never came (cancelled, staggered) doesn't hold it forever.
            const auto swing = g_swings.Find(a_actor->GetFormID());
            return swing && !swing->measured &&
                   std::chrono::steady_clock::now() - swing->started < std::chrono::milliseconds(2500);
        }
        return Intents::SinceSwing(a_actor) < Config::GetInstance().GetDecisionMatrix().creatureSwingSeconds;
    }

    float Reach(RE::Actor *a_actor, std::string_view a_event)
    {
        RE::TESRace *race = a_actor ? a_actor->GetRace() : nullptr;
        const auto samples = race ? g_reach.Find(Key(race, a_event)) : std::nullopt;
        return samples && !samples->empty() ? Median(*samples) * a_actor->GetScale() : 0.0f;
    }

    float ContactDistance(float a_reach, float a_targetRadius) { return a_reach + a_targetRadius + kHandAllowance; }

    float Startup(RE::Actor *a_actor, std::string_view a_event)
    {
        RE::TESRace *race = a_actor ? a_actor->GetRace() : nullptr;
        const auto samples = race ? g_startup.Find(Key(race, a_event)) : std::nullopt;
        return samples && !samples->empty() ? Median(*samples) : 0.0f;
    }

    float DistanceAtBlow(RE::Actor *a_actor, RE::Actor *a_target, float a_startup)
    {
        const RE::NiPoint3 self = a_actor->GetPosition(), other = a_target->GetPosition();
        const float distance = self.GetDistance(other);
        RE::NiPoint3 velocity;
        a_target->GetLinearVelocity(velocity);
        const float dx = self.x - other.x, dy = self.y - other.y, flat = std::hypot(dx, dy);
        const float closing = flat > 1.0f ? (velocity.x * dx + velocity.y * dy) / flat : 0.0f;
        return (std::max)(0.0f, distance - closing * a_startup);
    }

    namespace
    {
        // Each of the race's attacks (no bashes, chance > 0) with its measured reach (0 = not yet)
        template <typename Func> void ForEachAttack(RE::Actor *a_actor, Func &&a_func)
        {
            RE::TESRace *race = a_actor ? a_actor->GetRace() : nullptr;
            if (!race || !race->attackDataMap) {
                return;
            }
            for (const auto &[name, data] : race->attackDataMap->attackDataMap) {
                if (!data || data->data.flags.any(RE::AttackData::AttackFlag::kBashAttack) || data->data.attackChance <= 0.0f) {
                    continue;
                }
                a_func(*data, Reach(a_actor, name.c_str()));
            }
        }
    }

    float LongestReach(RE::Actor *a_actor)
    {
        float longest = 0.0f;
        ForEachAttack(a_actor, [&](const RE::BGSAttackData &, float a_reach) { longest = (std::max)(longest, a_reach); });
        return longest;
    }

    bool AnyReaches(RE::Actor *a_actor, RE::Actor *a_target, bool a_power, float a_longerThan)
    {
        if (!a_actor || !a_target) {
            return false;
        }
        const float radius = ActorUtils::GetBodyRadius(a_target);
        bool any = false;
        ForEachAttack(a_actor, [&](const RE::BGSAttackData &a_data, float a_reach) {
            const bool power = a_data.data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);
            any |= power == a_power && a_reach > 0.0f && (a_longerThan <= 0.0f || a_reach > a_longerThan) &&
                   DistanceAtBlow(a_actor, a_target, Startup(a_actor, a_data.event.c_str())) <=
                       ContactDistance(a_reach, radius);
        });
        return any;
    }
} // namespace CombatAI::LimbReach

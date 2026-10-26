#include "SCARData.h"
#include "AttackCoordinator.h"
#include "Intents.h"
#include "SCARIntegration.h"
#include "Config.h"
#include "Logger.h"

#include <cmath>
#include <numbers>

namespace CombatAI::SCARData
{
    namespace
    {
        constexpr std::string_view kPrefix = "SCAR_ActionData";

        // Minimal reader for a flat JSON object {"Key": "string" | number, ...}; enough for SCAR_ActionData
        struct Reader
        {
            std::string_view s;
            std::size_t i = 0;

            void Space()
            {
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
                    ++i;
                }
            }
            bool Eat(char a_c)
            {
                Space();
                if (i < s.size() && s[i] == a_c) {
                    ++i;
                    return true;
                }
                return false;
            }
            bool String(std::string &a_out)
            {
                if (!Eat('"')) {
                    return false;
                }
                a_out.clear();
                while (i < s.size() && s[i] != '"') {
                    if (s[i] == '\\' && i + 1 < s.size()) {
                        ++i;
                    }
                    a_out.push_back(s[i++]);
                }
                return Eat('"');
            }
            bool Number(float &a_out)
            {
                Space();
                const std::size_t start = i;
                while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' ||
                                        s[i] == '.' || s[i] == 'e' || s[i] == 'E')) {
                    ++i;
                }
                if (i == start) {
                    return false;
                }
                const std::string text(s.substr(start, i - start));
                char *end = nullptr;
                a_out = std::strtof(text.c_str(), &end);
                return end && *end == '\0';
            }
        };
    } // namespace

    bool Parse(std::string_view a_json, Action &a_out)
    {
        Reader r{a_json};
        if (!r.Eat('{')) {
            return false;
        }
        Action a;
        bool haveName = false, haveType = false;
        int numbers = 0; // MinDistance, MaxDistance, StartAngle, EndAngle, Chance
        if (!r.Eat('}')) {
            do {
                std::string key;
                if (!r.String(key) || !r.Eat(':')) {
                    return false;
                }
                r.Space();
                if (r.i < r.s.size() && r.s[r.i] == '"') {
                    std::string value;
                    if (!r.String(value)) {
                        return false;
                    }
                    if (key == "AttackData") {
                        a.attackData = value;
                        haveName = true;
                    } else if (key == "IdleAnimation") {
                        a.idle = value;
                        haveName = true;
                    } else if (key == "Type") {
                        a.type = value;
                        haveType = true;
                    } // CoolDownAlias, ConditionsAlias: not used yet
                } else {
                    float v = 0.0f;
                    if (!r.Number(v)) {
                        return false;
                    }
                    if (key == "MinDistance") {
                        a.minDistance = (std::max)(v, 0.0f), ++numbers;
                    } else if (key == "MaxDistance") {
                        a.maxDistance = (std::max)(v, 0.0f), ++numbers;
                    } else if (key == "StartAngle") {
                        a.startAngle = std::clamp(v, -180.0f, 180.0f), ++numbers;
                    } else if (key == "EndAngle") {
                        a.endAngle = std::clamp(v, -180.0f, 180.0f), ++numbers;
                    } else if (key == "Chance") {
                        a.chance = std::clamp(v, 0.0f, 100.0f), ++numbers; // SCAR clamps to +-180 (its typo)
                    } else if (key == "TriggerStartTime") {
                        a.triggerStart = (std::max)(v, 0.0f);
                    } else if (key == "TriggerEndTime") {
                        a.triggerEnd = v;
                    } else if (key == "WeaponLength") {
                        a.weaponLength = (std::max)(v, 0.0f);
                    } else if (key == "CoolDownTime") {
                        a.cooldown = (std::max)(v, 0.0f);
                    } else if (key == "VariantID") {
                        a.variant = (std::max)(static_cast<int>(v), 0);
                    }
                }
            } while (r.Eat(','));
            if (!r.Eat('}')) {
                return false;
            }
        }
        a.triggerEnd = (std::max)(a.triggerEnd, a.triggerStart);
        if (!haveName || !haveType || numbers < 5) {
            return false;
        }
        a_out = a;
        return true;
    }

    bool SelfTest()
    {
        // Examples from SCAR's "Developers Manual For SCAR 2.0+" (EN), lines 24 and 95
        Action a;
        const bool attack =
            Parse(R"({"AttackData":"attackStart","MinDistance":0,"MaxDistance":139,"StartAngle":-60,"EndAngle":60,"Chance":100,"Type":"RA"})", a) &&
            a.attackData == "attackStart" && a.maxDistance == 139.0f && a.startAngle == -60.0f && a.type == "RA";
        Action b;
        const bool idle =
            Parse(R"({ "IdleAnimation" : "SCAR_Combo1", "Type": "IDLE", "MinDistance": 50.5, "MaxDistance": 200,
                       "StartAngle": -45, "EndAngle": 45, "Chance": 50, "VariantID": 2, "TriggerStartTime": 0.3 })", b) &&
            b.idle == "SCAR_Combo1" && b.minDistance == 50.5f && b.variant == 2 && b.triggerStart == 0.3f;
        Action c;
        const bool rejects = !Parse(R"({"AttackData":"attackStart","Type":"RA"})", c) && !Parse("not json", c);
        const bool ok = attack && idle && rejects;
        LOG_INFO("SCAR data reader self-test {}", ok ? "passed" : "FAILED (SCAR action data probe off)");
        return ok;
    }

    std::vector<Action> Read(RE::Actor *a_actor, float &a_clipTime)
    {
        std::vector<Action> actions;
        a_clipTime = 0.0f;
        RE::BSAnimationGraphManagerPtr manager;
        if (!a_actor || !a_actor->GetAnimationGraphManager(manager) || !manager || manager->graphs.empty()) {
            return actions;
        }
        RE::BSSpinLockGuard lock(manager->GetRuntimeData().updateLock);
        auto *graph = manager->graphs[0] ? manager->graphs[0]->behaviorGraph : nullptr;
        auto *activeNodes = graph ? graph->activeNodes : nullptr;
        if (!activeNodes) {
            return actions;
        }
        for (auto &info : *activeNodes) {
            auto *node = info.nodeClone;
            const auto *type = node ? node->GetClassType() : nullptr;
            if (!type || !type->name || _stricmp(type->name, "hkbClipGenerator") != 0) {
                continue;
            }
            auto *clip = static_cast<RE::hkbClipGenerator *>(node);
            auto *animation = clip->binding ? clip->binding->animation.get() : nullptr;
            if (!animation || animation->annotationTracks.empty()) {
                continue;
            }
            for (auto &annotation : animation->annotationTracks[0].annotations) {
                const char *text = annotation.text.c_str();
                const std::string_view view = text ? text : "";
                Action action;
                if (view.starts_with(kPrefix) && Parse(view.substr(kPrefix.size()), action)) {
                    action.weight = annotation.time;
                    actions.push_back(std::move(action));
                }
            }
            if (!actions.empty()) {
                a_clipTime = clip->localTime;
                break; // the first clip with SCAR data, as SCAR does
            }
        }
        std::ranges::stable_sort(actions, [](const Action &x, const Action &y) { return x.weight > y.weight; });
        return actions;
    }

    bool InRange(const Action &a_action, RE::Actor *a_self, RE::Actor *a_target, float a_reach)
    {
        if (!a_self || !a_target) {
            return false;
        }
        const float reach = a_action.weaponLength >= 0.0f ? a_action.weaponLength * a_self->GetScale() : a_reach;
        const float maxDist = a_action.maxDistance + reach + a_target->GetBoundRadius();
        const float minDist =
            a_action.minDistance > 0.0f ? a_action.minDistance + a_target->GetBoundRadius() + a_self->GetBoundRadius() : 0.0f;
        RE::NiPoint3 d = a_target->GetPosition() - a_self->GetPosition();
        d.z = 0.0f;
        const float dist = d.Length();
        // Local frame: heading turns clockwise, forward = (sin h, cos h), right = (cos h, -sin h)
        const float h = a_self->GetAngleZ();
        const float localX = d.x * std::cos(h) - d.y * std::sin(h);
        const float localY = d.x * std::sin(h) + d.y * std::cos(h);
        const float angle = std::atan2(localX, localY) * 180.0f / std::numbers::pi_v<float>;
        const bool inAngle = a_action.startAngle < a_action.endAngle
                                 ? angle >= a_action.startAngle && angle <= a_action.endAngle
                                 : !(angle >= a_action.endAngle && angle <= a_action.startAngle);
        return dist <= maxDist && dist >= minDist && inAngle;
    }

    void Probe(RE::Actor *a_self, RE::Actor *a_target, float a_reach)
    {
        static const bool ok = SelfTest();
        if (!ok || !a_self || !Config::GetInstance().IsDebugTarget(a_self->GetFormID())) {
            return;
        }
        float clipTime = 0.0f;
        const auto actions = Read(a_self, clipTime);
        if (actions.empty()) {
            return;
        }
        std::string fits;
        const Action *best = nullptr;
        for (const auto &a : actions) {
            const bool inTime = clipTime >= a.triggerStart && clipTime <= a.triggerEnd;
            const bool range = InRange(a, a_self, a_target, a_reach);
            if (inTime && range && !best) {
                best = &a;
            }
            fits += std::format("{}{} {} {:.0f}-{:.0f} {:.0f}..{:.0f}deg {:.0f}%{}; ", a.attackData.empty() ? a.idle : a.attackData,
                                a.idle.empty() ? "" : " (idle)", a.type, a.minDistance, a.maxDistance, a.startAngle,
                                a.endAngle, a.chance, inTime ? (range ? " FITS" : "") : " (not in its time window)");
        }
        LOG_DEBUG("SCAR 0x{:08X}: {} actions on the active clip (t {:.2f}), best fit {}: {}", a_self->GetFormID(),
                  actions.size(), clipTime, best ? (best->attackData.empty() ? best->idle : best->attackData) : "none", fits);
    }

    float AttackReach(RE::Actor *a_self, float a_reach)
    {
        float clipTime = 0.0f;
        const auto actions = Read(a_self, clipTime);
        float likely = 0.0f, any = 0.0f;
        for (const auto &a : actions) {
            const float reach = a.maxDistance + (a.weaponLength >= 0.0f ? a.weaponLength * a_self->GetScale() : a_reach);
            any = (std::max)(any, reach);
            if (a.chance >= 50.0f) {
                likely = (std::max)(likely, reach);
            }
        }
        return likely > 0.0f ? likely : any;
    }

    namespace
    {
        bool GateEnabled()
        {
            return SCARIntegration::GetInstance().IsAvailable() &&
                   Config::GetInstance().GetModIntegrations().enableSCARIntegration;
        }

        // Why the brain refuses a SCAR attack right now, or nullptr
        const char *Refusal(RE::Actor *a_actor)
        {
            if (AttackCoordinator::GetSingleton().IsHeld(a_actor->GetFormID())) {
                return "waiting its turn";
            }
            if (Intents::WantsDualBlock(a_actor)) {
                return "holding a block";
            }
            if (Intents::AttackVetoed(a_actor)) {
                return "defending";
            }
            return nullptr;
        }

        struct AttackStart
        {
            static bool thunk(void *a_context)
            {
                auto *actor = RE::CombatBehaviorController::GetCurrentAttacker();
                if (actor && GateEnabled()) {
                    if (const char *why = Refusal(actor)) {
                        float clipTime = 0.0f;
                        if (!Read(actor, clipTime).empty()) { // a SCAR clip: SCAR would start it (vanilla is paced already)
                            if (Config::GetInstance().IsDebugTarget(actor->GetFormID())) {
                                LOG_DEBUG("SCAR gate 0x{:08X}: attack start refused ({})", actor->GetFormID(), why);
                            }
                            return false;
                        }
                    }
                }
                return func(a_context);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct ComboEvent
        {
            static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent> *a_sink,
                                                  RE::BSAnimationGraphEvent *a_event,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent> *a_source)
            {
                if (a_event && a_event->holder && _stricmp(a_event->tag.c_str(), "SCAR_ComboStart") == 0 && GateEnabled()) {
                    auto *actor = const_cast<RE::Actor *>(a_event->holder->As<RE::Actor>());
                    if (actor && !actor->IsPlayerRef()) {
                        if (const char *why = Refusal(actor)) {
                            if (Config::GetInstance().IsDebugTarget(actor->GetFormID())) {
                                LOG_DEBUG("SCAR gate 0x{:08X}: combo refused ({})", actor->GetFormID(), why);
                            }
                            return RE::BSEventNotifyControl::kContinue; // a SCAR-only event: vanilla ignores it
                        }
                    }
                }
                return func(a_sink, a_event, a_source);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    } // namespace

    void InstallGate(SKSE::Trampoline &a_trampoline)
    {
        if (!SCARIntegration::GetInstance().IsAvailable() || REL::Module::IsVR()) {
            return;
        }
        REL::Relocation<std::uintptr_t> site{RELOCATION_ID(48147, 49178), REL::Relocate(0x164, 0x148)};
        if (*reinterpret_cast<const std::uint8_t *>(site.address()) != 0xE8) {
            LOG_WARN("SCAR gate: attack start site is not a call - gate off");
            return;
        }
        AttackStart::func = a_trampoline.write_call<5>(site.address(), AttackStart::thunk);
        REL::Relocation<std::uintptr_t> sink{RE::Character::VTABLE[2]};
        ComboEvent::func = sink.write_vfunc(0x1, ComboEvent::thunk);
        LOG_INFO("SCAR gate installed: pacing and defending also hold SCAR attacks and combos");
    }
} // namespace CombatAI::SCARData

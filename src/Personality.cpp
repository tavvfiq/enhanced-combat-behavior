#include "Personality.h"
#include "TargetDanger.h"
#include "Boss.h"
#include "CombatStyleEnhancer.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

namespace CombatAI::Personality
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        struct Cached
        {
            Traits traits;
            Clock::time_point at{};
        };
        ThreadSafeMap<RE::FormID, Cached> g_cache;

        // One resolved profile entry; an unset trait keeps the computed value
        struct Profile
        {
            std::string match;
            RE::TESForm *form = nullptr;
            std::optional<float> risk, caution, patience;
        };
        // Replaced whole by ReloadProfiles (menu thread) while the brain reads it
        std::atomic<std::shared_ptr<const std::vector<Profile>>> g_profiles;

        std::string_view Trim(std::string_view a_text)
        {
            const auto first = a_text.find_first_not_of(" \t");
            if (first == std::string_view::npos) {
                return {};
            }
            return a_text.substr(first, a_text.find_last_not_of(" \t") - first + 1);
        }

        // "0x1BCC0~Skyrim.esm" (local FormID ~ plugin) or an EditorID. Keywords always keep their EditorID; factions,
        // races and NPCs only with EditorIDs kept in memory (powerofthree's Tweaks), as for boss detection.
        RE::TESForm *Resolve(std::string_view a_match)
        {
            const auto tilde = a_match.find('~');
            if (tilde == std::string_view::npos) {
                return RE::TESForm::LookupByEditorID(a_match);
            }
            std::string_view idText = Trim(a_match.substr(0, tilde));
            if (idText.starts_with("0x") || idText.starts_with("0X")) {
                idText.remove_prefix(2);
            }
            RE::FormID id = 0;
            const auto [end, ec] = std::from_chars(idText.data(), idText.data() + idText.size(), id, 16);
            auto *data = RE::TESDataHandler::GetSingleton();
            if (ec != std::errc{} || end != idText.data() + idText.size() || !data) {
                return nullptr;
            }
            return data->LookupForm(id & 0xFFFFFF, Trim(a_match.substr(tilde + 1)));
        }

        bool Matches(RE::Actor *a_actor, RE::TESForm *a_form)
        {
            auto *base = a_actor->GetActorBase();
            switch (a_form->GetFormType()) {
            case RE::FormType::Keyword: {
                const auto *keyword = a_form->As<RE::BGSKeyword>();
                const auto *race = a_actor->GetRace();
                return (base && base->HasKeyword(keyword)) || (race && race->HasKeyword(keyword));
            }
            case RE::FormType::Faction:
                return a_actor->IsInFaction(a_form->As<RE::TESFaction>());
            case RE::FormType::Race:
                return a_actor->GetRace() == a_form;
            case RE::FormType::NPC:
                return base == a_form || a_actor->GetTemplateActorBase() == a_form; // leveled actors: the picked NPC
            default:
                return false;
            }
        }

        // Fixed per-NPC offset in [-0.15, 0.15] (run 52: one template's bandits read the same data, the offset is the spread)
        float Jitter(RE::FormID a_formID, std::uint32_t a_salt)
        {
            std::uint32_t h = (a_formID ^ a_salt) * 0x9E3779B1u;
            h ^= h >> 15;
            return (static_cast<float>(h & 0xFFFF) / 65535.0f - 0.5f) * 2.0f * Config::GetInstance().GetDecisionMatrix().personalityJitter;
        }

        Traits Compute(RE::Actor *a_actor)
        {
            auto *av = a_actor->AsActorValueOwner();
            const float confidence = std::clamp(av->GetActorValue(RE::ActorValue::kConfidence), 0.0f, 4.0f) / 4.0f;
            const float aggression = std::clamp(av->GetActorValue(RE::ActorValue::kAggression), 0.0f, 3.0f) / 3.0f;
            float offense = 0.5f, avoid = 0.3f;
            if (auto *style = CombatStyleEnhancer::GetCombatStyle(a_actor)) {
                const float off = (std::max)(style->generalData.offensiveMult, 0.0f);
                const float def = (std::max)(style->generalData.defensiveMult, 0.0f);
                offense = off + def > 0.0f ? off / (off + def) : 0.5f;
                avoid = std::clamp(style->generalData.avoidThreatChance, 0.0f, 1.0f);
            }
            const RE::FormID id = a_actor->GetFormID();
            // The actor values weigh more than the style's balance, which is all-offense for many vanilla styles
            float risk = 0.45f * confidence + 0.35f * aggression + 0.2f * offense;
            float caution = 0.45f * (1.0f - confidence) + 0.2f * (1.0f - offense) + 0.35f * avoid;
            float patience = 0.7f * (1.0f - aggression) + 0.3f * (1.0f - offense);
            std::string_view profile = "none";
            if (const auto profiles = g_profiles.load()) {
                for (const auto &p : *profiles) { // in file order: a later matching line wins
                    if (Matches(a_actor, p.form)) {
                        risk = p.risk.value_or(risk);
                        caution = p.caution.value_or(caution);
                        patience = p.patience.value_or(patience);
                        profile = p.match;
                    }
                }
            }
            Traits t;
            t.risk = std::clamp(risk + Jitter(id, 1), 0.0f, 1.0f);
            t.caution = std::clamp(caution + Jitter(id, 2), 0.0f, 1.0f);
            t.patience = std::clamp(patience + Jitter(id, 3), 0.0f, 1.0f);
            if (Config::GetInstance().IsDebugTarget(id)) {
                LOG_DEBUG("Personality 0x{:08X}: confidence {:.0f}, aggression {:.0f}, style offense {:.2f}, avoid threat {:.2f}, profile {} -> risk {:.2f}, caution {:.2f}, patience {:.2f}",
                          id, confidence * 4.0f, aggression * 3.0f, offense, avoid, profile, t.risk, t.caution, t.patience);
            }
            return t;
        }
    } // namespace

    Traits Get(RE::Actor *a_actor)
    {
        if (!a_actor || !Config::GetInstance().GetDecisionMatrix().enablePersonality) {
            return {};
        }
        const auto now = Clock::now();
        const RE::FormID id = a_actor->GetFormID();
        if (const auto c = g_cache.Find(id); c && now - c->at < std::chrono::seconds(10)) {
            return c->traits;
        }
        const Traits t = Compute(a_actor);
        g_cache.ModifyOrCreate(id, [&](Cached &c) { c = {t, now}; });
        return t;
    }

    Traits Effective(RE::Actor *a_actor)
    {
        Traits t = Get(a_actor);
        switch (Boss::CurrentPhase(a_actor)) {
        case Boss::Phase::None: {
            // A dangerous target (TargetDanger.h) makes it warier; a Foolhardy NPC (Confidence 4) fears nothing
            const float danger = TargetDanger::Of(a_actor).score * Config::GetInstance().GetDecisionMatrix().targetDangerInfluence;
            auto *av = danger > 0.0f ? a_actor->AsActorValueOwner() : nullptr;
            if (av && av->GetActorValue(RE::ActorValue::kConfidence) < 4.0f) {
                t.risk *= 1.0f - danger;
                t.caution += (1.0f - t.caution) * danger;
            }
            return t;
        }
        case Boss::Phase::Enraged:
            t.risk = (std::min)(1.0f, t.risk + 0.2f);
            break;
        case Boss::Phase::Desperate:
            if (Boss::Brave(a_actor)) {
                t.risk = 1.0f; // all-in
                t.caution = 0.25f;
                return t;
            }
            t.risk *= 0.5f; // falls back among its minions (EvaluateBoss)
            t.caution = 0.9f;
            return t;
        default:
            break;
        }
        t.caution = (std::max)(t.caution, Config::GetInstance().GetBoss().cautionFloor); // a boss still defends well (run 55's chief read caution 0.01)
        return t;
    }

    void ReloadProfiles()
    {
        // Files in name order, entries in file order: a later matching entry wins per trait
        constexpr const char *directory = "Data/SKSE/Plugins/EnhancedCombatBehavior/Profiles";
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
            if (entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::ranges::sort(files);

        auto profiles = std::make_shared<std::vector<Profile>>();
        for (const auto &path : files) {
            const std::string file = path.filename().string();
            nlohmann::json json;
            try {
                std::ifstream in(path);
                json = nlohmann::json::parse(in); // plain JSON, no comments
            } catch (const nlohmann::json::exception &e) {
                LOG_WARN("Profiles {}: not valid JSON ({}): file skipped", file, e.what());
                continue;
            }
            if (!json.is_array()) {
                LOG_WARN("Profiles {}: expected [ {{\"match\": ..., \"risk\": ..., \"caution\": ..., \"patience\": ...}} ]: file skipped", file);
                continue;
            }
            for (const auto &entry : json) {
                Profile p;
                bool usable = entry.is_object() && entry.contains("match") && entry["match"].is_string();
                if (usable) {
                    p.match = entry["match"].get<std::string>();
                    for (const auto &[key, value] : entry.items()) {
                        std::optional<float> *trait = key == "risk" ? &p.risk : key == "caution" ? &p.caution : key == "patience" ? &p.patience : nullptr;
                        if (trait && value.is_number()) {
                            *trait = std::clamp(value.get<float>(), 0.0f, 1.0f);
                        } else if (key != "match" && !(key == "note" && value.is_string())) { // note: a label, JSON has no comments
                            usable = false;
                        }
                    }
                }
                if (!usable) {
                    LOG_WARN("Profiles {}: {} skipped (needs \"match\" text, then only risk / caution / patience numbers 0-1 and a \"note\" text)", file, entry.dump());
                    continue;
                }
                p.form = Resolve(p.match);
                const auto type = p.form ? p.form->GetFormType() : RE::FormType::None;
                if (type != RE::FormType::Keyword && type != RE::FormType::Faction && type != RE::FormType::Race && type != RE::FormType::NPC) {
                    LOG_WARN("Profiles {}: {}: {}: skipped", file, p.match,
                             p.form ? "not a keyword, faction, race or NPC"
                                    : "form not found (wrong FormID or plugin; a faction, race or NPC by EditorID needs EditorIDs kept in memory)");
                    continue;
                }
                const auto show = [](const std::optional<float> &a_value) { return a_value ? std::format("{:.2f}", *a_value) : std::string("-"); };
                LOG_INFO("Profiles {}: {} = {} 0x{:08X} ({}) -> risk {}, caution {}, patience {}", file, p.match, RE::FormTypeToString(type),
                         p.form->GetFormID(), p.form->GetName(), show(p.risk), show(p.caution), show(p.patience));
                profiles->push_back(std::move(p));
            }
        }
        LOG_INFO("Profiles: {} entr(y/ies) from {} file(s) in {}", profiles->size(), files.size(), directory);
        g_profiles.store(std::move(profiles));
        g_cache.Clear(); // traits computed under the old profiles
    }
} // namespace CombatAI::Personality

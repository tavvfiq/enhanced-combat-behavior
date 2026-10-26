#include "Boss.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

#include <algorithm>
#include <cctype>

namespace CombatAI::Boss
{
    namespace
    {
        // Keyed by FormID and checked against the base: spawned references (placeatme, spawners) get FF FormIDs the
        // game reuses once the actor is gone, so a new NPC under an old id is checked again
        struct Cached
        {
            Info info;
            const RE::TESNPC *base = nullptr;
        };
        ThreadSafeMap<RE::FormID, Cached> g_cache;

        RE::BGSLocationRefType *BossRefType()
        {
            // Skyrim.esm is always load index 00, so its FormID is fixed
            static auto *type = RE::TESForm::LookupByID<RE::BGSLocationRefType>(0x000130F7);
            return type;
        }

        bool NameListed(std::string_view a_name)
        {
            const std::string &list = Config::GetInstance().GetBoss().bossNames;
            std::size_t start = 0;
            while (start <= list.size()) {
                const std::size_t comma = list.find(',', start);
                std::string_view entry(list.data() + start, (comma == std::string::npos ? list.size() : comma) - start);
                while (!entry.empty() && entry.front() == ' ') {
                    entry.remove_prefix(1);
                }
                while (!entry.empty() && entry.back() == ' ') {
                    entry.remove_suffix(1);
                }
                if (!entry.empty() && entry.size() == a_name.size() &&
                    _strnicmp(entry.data(), a_name.data(), entry.size()) == 0) {
                    return true;
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            return false;
        }
        // EditorIDs exist only if a mod keeps them: test a vanilla NPC whose EditorID we know (Skyrim.esm scan)
        bool EditorIDsLoaded()
        {
            static const bool loaded = [] {
                const auto *npc = RE::TESForm::LookupByID<RE::TESNPC>(0x000E1646);
                const char *edid = npc ? npc->GetFormEditorID() : nullptr;
                const bool ok = edid && std::string_view(edid) == "LvlBanditBossMelee2HNordM";
                LOG_INFO("Boss detection: EditorIDs {}", ok ? "loaded - the \"Boss\" EditorID rule is on"
                                                            : "not kept in memory - the \"Boss\" EditorID rule is off (powerofthree's Tweaks \"Load EditorIDs\" turns it on)");
                return ok;
            }();
            return loaded;
        }

        bool HasBoss(const RE::TESForm *a_form, std::string &a_match)
        {
            const char *edid = a_form ? a_form->GetFormEditorID() : nullptr;
            if (!edid || !*edid) {
                return false;
            }
            std::string lower(edid);
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("boss") == std::string::npos) {
                return false;
            }
            a_match = edid;
            return true;
        }

        // The NPC, its template chain, and for a leveled actor the leveled list it came from and the NPC it picked
        bool EditorIDBoss(RE::Actor *a_actor, std::string &a_match)
        {
            const auto chain = [&](const RE::TESForm *a_form) {
                for (int depth = 0; a_form && depth < 4; ++depth) {
                    if (HasBoss(a_form, a_match)) {
                        return true;
                    }
                    const auto *npc = a_form->As<RE::TESNPC>();
                    a_form = npc ? npc->baseTemplateForm : nullptr;
                }
                return false;
            };
            if (chain(a_actor->GetActorBase())) {
                return true;
            }
            const auto *leveled = a_actor->extraList.GetByType<RE::ExtraLeveledCreature>();
            return leveled && (chain(leveled->originalBase) || chain(leveled->templateBase));
        }
    } // namespace

    Info Get(RE::Actor *a_actor)
    {
        if (!a_actor || !Config::GetInstance().GetBoss().enableBossDetection) {
            return {};
        }
        const RE::FormID id = a_actor->GetFormID();
        const RE::TESNPC *base = a_actor->GetActorBase();
        if (const auto cached = g_cache.Find(id); cached && cached->base == base) {
            return cached->info;
        }
        Info info;
        const auto *extra = a_actor->extraList.GetByType<RE::ExtraLocationRefType>();
        info.marker = extra && extra->locRefType && extra->locRefType == BossRefType();
        const char *name = a_actor->GetName();
        info.named = name && NameListed(name);
        info.editorID = EditorIDsLoaded() && EditorIDBoss(a_actor, info.match);
        info.boss = info.marker || info.named || info.editorID;
        g_cache.ModifyOrCreate(id, [&](Cached &c) { c = {info, base}; });

        auto *player = RE::PlayerCharacter::GetSingleton();
        const int playerLevel = player ? player->GetLevel() : 0;
        if (info.boss) {
            std::string why;
            const auto add = [&](bool a_on, std::string_view a_what) {
                if (a_on) {
                    why += why.empty() ? "" : ", ";
                    why += a_what;
                }
            };
            add(info.marker, "boss marker");
            add(info.named, "name");
            add(info.editorID, "EditorID " + info.match);
            LOG_INFO("Boss 0x{:08X}: {} ({}) level {} vs player {}", id, name ? name : "?", why, a_actor->GetLevel(), playerLevel);
        } else if (Config::GetInstance().IsDebugTarget(id)) {
            LOG_DEBUG("Boss 0x{:08X}: not a boss ({}, no boss marker, level {} vs player {})", id, name ? name : "?",
                      a_actor->GetLevel(), playerLevel);
        }
        return info;
    }
} // namespace CombatAI::Boss

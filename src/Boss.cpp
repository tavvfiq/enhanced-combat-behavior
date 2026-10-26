#include "Boss.h"
#include "Config.h"
#include "Logger.h"
#include "ThreadSafeMap.h"

namespace CombatAI::Boss
{
    namespace
    {
        ThreadSafeMap<RE::FormID, Info> g_cache;

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
    } // namespace

    Info Get(RE::Actor *a_actor)
    {
        if (!a_actor || !Config::GetInstance().GetBoss().enableBossDetection) {
            return {};
        }
        const RE::FormID id = a_actor->GetFormID();
        if (const auto cached = g_cache.Find(id)) {
            return *cached;
        }
        Info info;
        const auto *extra = a_actor->extraList.GetByType<RE::ExtraLocationRefType>();
        info.marker = extra && extra->locRefType && extra->locRefType == BossRefType();
        const char *name = a_actor->GetName();
        info.named = name && NameListed(name);
        info.boss = info.marker || info.named;
        g_cache.ModifyOrCreate(id, [&](Info &i) { i = info; });

        auto *player = RE::PlayerCharacter::GetSingleton();
        const int playerLevel = player ? player->GetLevel() : 0;
        if (info.boss) {
            LOG_INFO("Boss 0x{:08X}: {} ({}{}{}) level {} vs player {}", id, name ? name : "?", info.marker ? "boss marker" : "",
                     info.marker && info.named ? ", " : "", info.named ? "name" : "", a_actor->GetLevel(), playerLevel);
        } else if (Config::GetInstance().IsDebugTarget(id)) {
            LOG_DEBUG("Boss 0x{:08X}: not a boss ({}, no boss marker, level {} vs player {})", id, name ? name : "?",
                      a_actor->GetLevel(), playerLevel);
        }
        return info;
    }
} // namespace CombatAI::Boss

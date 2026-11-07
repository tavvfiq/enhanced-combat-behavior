// Guns: which weapons are guns (GunKeywords), what an actor holds, and a gunman's firing range from its personality.
#include "Guns.h"
#include "ActorUtils.h"
#include "Config.h"
#include "Personality.h"

#include <algorithm>
#include <string_view>

namespace CombatAI::Guns
{
    bool IsGun(RE::TESForm *a_item)
    {
        auto *weapon = a_item ? a_item->As<RE::TESObjectWEAP>() : nullptr;
        if (!weapon) {
            return false;
        }
        const auto list = Config::GetInstance().GunKeywordList();
        return std::ranges::any_of(*list, [&](const std::string &a_keyword) { return weapon->HasKeywordString(a_keyword); });
    }

    Hands Of(RE::Actor *a_actor)
    {
        Hands hands;
        if (!a_actor) {
            return hands;
        }
        RE::TESForm *right = ActorUtils::SafeGetEquippedObject(a_actor, false);
        RE::TESForm *left = ActorUtils::SafeGetEquippedObject(a_actor, true);
        hands.right = IsGun(right);
        hands.left = IsGun(left);
        if (hands.Any()) {
            RE::TESForm *other = hands.right ? (hands.left ? nullptr : left) : right;
            auto *weapon = other ? other->As<RE::TESObjectWEAP>() : nullptr;
            hands.meleeOther = weapon && !weapon->IsBow() && !weapon->IsCrossbow() && !weapon->IsStaff();
        }
        return hands;
    }

    bool IsGunman(RE::Actor *a_actor)
    {
        const auto hands = Of(a_actor);
        return hands.Any() && !hands.meleeOther;
    }

    Band RangeOf(RE::Actor *a_actor)
    {
        // caution - risk: +1 careful (holds at 900, backs away inside 450), -1 reckless (450 / 250), 0 at 650 / 350
        const auto traits = Personality::Effective(a_actor);
        const float lean = std::clamp(traits.caution - traits.risk, -1.0f, 1.0f);
        Band band;
        band.ideal = std::clamp(650.0f + 250.0f * lean, 450.0f, 900.0f);
        band.backOff = std::clamp(350.0f + 100.0f * lean, 250.0f, 450.0f);
        band.max = band.ideal + 300.0f;
        return band;
    }
} // namespace CombatAI::Guns

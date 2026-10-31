// AttackWarpAPI.h - Attack Warp's public interface, V1. Copy this header into a consumer plugin (like TDM's and Precision's).
// Include it after CommonLibSSE (RE::ActorHandle) and Windows.h. Request it at or after SKSE kPostLoad: ATTACKWARP_API::RequestPluginAPI() returns nullptr when Attack Warp is absent or
// the version is unknown. Every call is safe from any thread; nothing here touches the game's own data.
#pragma once

#include <cstdint>

namespace ATTACKWARP_API
{
	enum class InterfaceVersion : std::uint8_t
	{
		V1
	};

	enum class APIResult : std::uint8_t
	{
		OK,
		NotFound,    // the handle does not resolve to a loaded actor
		BadArgument  // a multiplier is negative, above 10 or not a number
	};

	// Per-actor multipliers on the INI values; 1 = unchanged. Applies from the actor's next attack (read when a swing arms).
	// Kept until ClearTuning or the game exits; not saved and not cleared on load, so set it again each combat.
	struct Tuning
	{
		float turnRate = 1.0f;  // all turn rates (light, power, sprint draw; after the lock); 0 = never turned
		float lead = 1.0f;      // fLeadTime; 0 = aim at where the target is now
		float stretch = 1.0f;   // the stretch above 1 (fMaxScale - 1); 0 = never stretched
		bool  enabled = true;   // false = Attack Warp leaves this actor's attacks vanilla
	};

	// None: no melee swing, the actor is not allowed (killmove, mount, disabled kind, Tuning.enabled false) or root motion
	// stopped (no hook call for 0.25 s). Tracking: wind-up, turning toward the target. Locked: wind-up after the lock
	// (fLockLead before the learned hit, or weaponSwing), no turning. Holding: after the hit phase, same attack (follow-through).
	enum class Phase : std::uint8_t
	{
		None,
		Tracking,
		Locked,
		Holding
	};

	// snapshot from the actor's last root-motion frame, at most one frame old
	struct SwingInfo
	{
		std::uint32_t   swingId;      // new value each swing of this actor (0 = never swung)
		Phase           phase;
		RE::ActorHandle target;       // the swing's target (player: Attack Warp's pick or TDM's lock; NPCs: combat target)
		bool            power;        // power or charge attack
		bool            sprint;       // started while sprinting
		float           startDist;    // distance to the target when the swing armed; -1 = no target
		float           aimErrorDeg;  // heading error to the aim point now, + = to the right
		float           turnedDeg;    // degrees Attack Warp turned the actor this swing
		float           stretch;      // current eased stretch factor (1 = as authored)
		float           timeToHit;    // predicted seconds to the hit phase from this actor's earlier swings of the attack; -1 = unknown
	};

	// what decides how far a lunge reaches, with the actor's Tuning and its Precision weapon length applied
	struct WarpParams
	{
		float startLimit;       // fMaxStartDist or fMaxStartDistPower: no stretch for swings started further away
		float maxScale;         // fMaxScale after Tuning.stretch
		float stopDist;         // player or NPC stop distance, shifted by the last queried Precision length
		float brakeZone;        // fBrakeZone
		float stretchRange;     // fStretchRange
		float maxStretchAngle;  // fMaxStretchAngle, degrees
	};

	class IVAttackWarp1
	{
	public:
		virtual APIResult SetTuning(RE::ActorHandle a_actor, const Tuning& a_tuning) noexcept = 0;
		virtual APIResult ClearTuning(RE::ActorHandle a_actor) noexcept = 0;
		// false = the actor never ran through Attack Warp's hook (no swing seen) or the handle does not resolve
		virtual bool GetSwingInfo(RE::ActorHandle a_actor, SwingInfo& a_out) const noexcept = 0;
		// false = the handle does not resolve; a_power selects fMaxStartDistPower
		virtual bool GetWarpParams(RE::ActorHandle a_actor, bool a_power, WarpParams& a_out) const noexcept = 0;
	};

	using _RequestPluginAPI = void* (*)(InterfaceVersion);

	// returns an IVAttackWarp1* for V1, or nullptr
	[[nodiscard]] inline void* RequestPluginAPI(InterfaceVersion a_version = InterfaceVersion::V1)
	{
		const auto module = GetModuleHandleA("AttackWarp.dll");
		if (!module) {
			return nullptr;
		}
		const auto request = reinterpret_cast<_RequestPluginAPI>(GetProcAddress(module, "RequestPluginAPI"));
		return request ? request(a_version) : nullptr;
	}
}

#include "APIManager.h"
#include "CombatAIAPI.h"
#include "CombatDirector.h"
#include "Config.h"
#include "ConfigMenu.h"
#include "Hooks.h"
#include "Logger.h"
#include "Trees.h"
#include "pch.h"

extern "C" __declspec(dllexport) void *__cdecl RequestPluginAPI(ECA_API::InterfaceVersion a_interfaceVersion)
{
    auto api = CombatAI::APIManager::GetSingleton();
    switch (a_interfaceVersion) {
    case ECA_API::InterfaceVersion::V1:
        // APIManager implements IVCombatAI1
        return static_cast<ECA_API::IVCombatAI1 *>(api);
    }
    return nullptr;
}

namespace
{
    // Combat Pathing Revolution patches the same combat movement code this mod drives (the CloseMovement tree, the
    // Circle node, the Backoff / Fallback starts), so the two are not supported together. CPR may not be loaded yet
    // when we are (plugins load in name order), hence the file check as well.
    bool CPRInstalled()
    {
        if (GetModuleHandleW(L"CombatPathingRevolution.dll")) {
            return true;
        }
        std::error_code ec;
        return std::filesystem::exists("Data/SKSE/Plugins/CombatPathingRevolution.dll", ec);
    }
} // namespace

extern "C" __declspec(dllexport) bool __cdecl SKSEPlugin_Load(const SKSE::LoadInterface *a_skse)
{
    // Initialize SKSE
    // One trampoline for every hook (tree builders at load, the rest at kDataLoaded)
    SKSE::Init(a_skse, {.trampoline = true, .trampolineSize = 1024}); // ~30 call hooks at 14 B each plus stubs; headroom for more

    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder)
        SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto logFilePath = *logsFolder / std::format("{}.log", "EnhancedCombatBehavior");
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));

    // Initialize logger
    LOG_INFO("EnhancedCombatBehavior v{} loading...", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string());

    // Load configuration
    auto &config = CombatAI::Config::GetInstance();
    config.Load();

    // Check if plugin is enabled
    if (!config.IsEnabled()) {
        LOG_INFO("EnhancedCombatBehavior is disabled in configuration file");
        return true; // Plugin loaded but disabled
    }

    // Refuse to run next to CPR, before installing anything (SKSE unloads a plugin that returns false)
    if (CPRInstalled()) {
        LOG_ERROR("Combat Pathing Revolution (CombatPathingRevolution.dll) is installed: Enhanced Combat Behavior is not "
                  "compatible with it and does not load this session");
        MessageBoxW(nullptr,
                    L"Enhanced Combat Behavior is not compatible with Combat Pathing Revolution "
                    L"(CombatPathingRevolution.dll).\n\nBoth change how NPCs move in combat. Enhanced Combat Behavior "
                    L"will not load this session. Disable one of the two mods.",
                    L"Enhanced Combat Behavior", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return false;
    }

    // The engine builds its behavior trees before kDataLoaded, so the tree builders are replaced
    // here, not with the other hooks.
    CombatAI::Trees::Install();

    // Set debug log level if enabled
    if (config.GetGeneral().enableDebugLog) {
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
        LOG_DEBUG("Debug logging enabled");
    } else {
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }

    if (!SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message *message) {
            switch (message->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                // Initialize CombatDirector with config after data is loaded
                // kDataLoaded fires after TESDataHandler is available, which is required for mod detection
                // Initialize BEFORE installing hooks so CombatDirector is ready when hooks start calling ProcessActor.
                CombatAI::CombatDirector::GetInstance().Initialize();

                // Install hooks after initialization
                // Hooks will immediately start calling ProcessActor/Update, so CombatDirector must be initialized first
                CombatAI::Hooks::Install();

                // Register the runtime config menu (self-skips if SKSE Menu Framework is absent)
                CombatAI::ConfigMenu::Register();
                break;
            }
        })) {
        logger::error("Failed to register messaging listener");
    }

    LOG_INFO("EnhancedCombatBehavior loaded successfully");

    return true;
}

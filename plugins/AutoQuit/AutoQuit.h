#pragma once

#include <ToolboxUIPlugin.h>
#include <GWCA/Constants/Constants.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/Managers/UIMgr.h>
#include <chrono>
#include <unordered_map>

namespace GW {
    struct AgentLiving;
}

class AutoQuit : public ToolboxPlugin {
public:
    AutoQuit() = default;
    ~AutoQuit() override = default;

    const char* Name() const override { return "Auto Quit"; }

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;
    [[nodiscard]] bool HasSettings() const override { return true; }
    void DrawSettings() override;
    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
    bool CanTerminate() override;
    // Draw user interface. Will be called every frame if the element is visible
    void Draw(IDirect3DDevice9* pDevice) override;
    void Update(float delta) override;

private:
    GW::HookEntry UIMessage_Entry;
    static GW::Constants::MapID last_outpost_id;
    std::chrono::steady_clock::time_point last_travel_time{};
    std::unordered_map<GW::Constants::SkillID, std::chrono::steady_clock::time_point> skill_timers;

    // Settings
    float kilroy_energy_max_threshold = 70.0f;
    float hp_percent_below_level_10 = 0.50f;
    float hp_percent_above_level_10 = 0.40f;

    static void OnPostUIMessage(GW::HookStatus* status, GW::UI::UIMessage message_id, void* wparam, void* lparam);
    static void OnMapLoaded();
    void Travel(GW::AgentLiving* player, bool isKilroy = false);
    void KilroyStandUp(GW::AgentLiving* player);
    bool UseSkillOnTargetWithTimer(GW::Constants::SkillID skill_id, uint32_t target, int32_t delay);
};

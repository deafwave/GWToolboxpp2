#include "AutoQuit.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Skill.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Utilities/Hook.h>

#include <Utils/GuiUtils.h>
#include "GWCA/Managers/ChatMgr.h"
#include <GWCA/include/GWCA/Managers/UIMgr.h>
#include <GWCA/include/GWCA/Managers/SkillbarMgr.h>
#include <GWCA/include/GWCA/Managers/AgentMgr.h>
#include <GWCA/include/GWCA/Managers/EffectMgr.h>
#include <GWCA/include/GWCA/Managers/GameThreadMgr.h>

#include <chrono>
#include <string>
#include <string_view>

namespace {
    bool redirect_slash_ee_to_eee = false;
    GW::HookEntry ChatCmd_HookEntry;

    void LogMessage(std::string_view message)
    {
        const auto wMessage = std::wstring{message.begin(), message.end()};
        const size_t len = 40 + wcslen(wMessage.c_str());
        auto to_send = new wchar_t[len];
        swprintf(to_send, len - 1, L"<a=1>%s</a><c=#%6X>: %s</c>", L"AutoQuit", 0xFFFFFF, wMessage.c_str());
        GW::GameThread::Enqueue([to_send] {
            GW::Chat::WriteChat(GW::Chat::Channel::CHANNEL_GWCA2, to_send, nullptr);
            delete[] to_send;
        });
    }
}

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static AutoQuit instance;
    return &instance;
}

void AutoQuit::LoadSettings(const wchar_t* folder)
{
    ToolboxPlugin::LoadSettings(folder);
    PLUGIN_LOAD_BOOL(redirect_slash_ee_to_eee);
    PLUGIN_LOAD_FLOAT(kilroy_energy_max_threshold);
    PLUGIN_LOAD_FLOAT(hp_percent_below_level_10);
    PLUGIN_LOAD_FLOAT(hp_percent_above_level_10);
}

void AutoQuit::SaveSettings(const wchar_t* folder)
{
    PLUGIN_SAVE_BOOL(redirect_slash_ee_to_eee);
    PLUGIN_SAVE_FLOAT(kilroy_energy_max_threshold);
    PLUGIN_SAVE_FLOAT(hp_percent_below_level_10);
    PLUGIN_SAVE_FLOAT(hp_percent_above_level_10);
    ToolboxPlugin::SaveSettings(folder);
}

void AutoQuit::DrawSettings()
{
    if (!toolbox_handle) {
        return;
    }
    ImGui::Text("Auto Quit Settings");
    ImGui::Separator();

    ImGui::Text("Kilroy");
    ImGui::SliderFloat("Max Energy Threshold", &kilroy_energy_max_threshold, 0.0f, 100.0f, "%.0f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Travel when max energy reaches or exceeds this value");
    }

    ImGui::Spacing();
    ImGui::Text("HP Thresholds");
    float hp_below_10_percent = hp_percent_below_level_10 * 100.0f;
    if (ImGui::SliderFloat("HP% Below Level 10", &hp_below_10_percent, 0.0f, 100.0f, "%.0f%%")) {
        hp_percent_below_level_10 = hp_below_10_percent / 100.0f;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Travel when HP drops below this percentage (for characters below level 10)");
    }

    float hp_above_10_percent = hp_percent_above_level_10 * 100.0f;
    if (ImGui::SliderFloat("HP% Above Level 10", &hp_above_10_percent, 0.0f, 100.0f, "%.0f%%")) {
        hp_percent_above_level_10 = hp_above_10_percent / 100.0f;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Travel when HP drops below this percentage (for characters level 10 and above)");
    }
}

bool AutoQuit::CanTerminate() {
    return true;
}

void AutoQuit::Draw(IDirect3DDevice9*)
{
    if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Loading) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if (ImGui::Begin(Name(), GetVisiblePtr(), ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::Text("Example plugin: area loading...");
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

///
void AutoQuit::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    ToolboxPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    GW::UI::RegisterUIMessageCallback(&UIMessage_Entry, GW::UI::UIMessage::kMapLoaded, OnPostUIMessage);
}

void AutoQuit::SignalTerminate()
{
    ToolboxPlugin::SignalTerminate();
    GW::UI::RemoveUIMessageCallback(&UIMessage_Entry);
}
// TODO: Make this defauilt to any unlocked outpost for that character
GW::Constants::MapID AutoQuit::last_outpost_id = GW::Constants::MapID::Kamadan_Jewel_of_Istan_outpost;

void AutoQuit::OnMapLoaded() {
    // Check if we're in an outpost and store the map ID
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) {
        last_outpost_id = GW::Map::GetMapID();
    }
}

void AutoQuit::OnPostUIMessage(GW::HookStatus* status, GW::UI::UIMessage message_id, void* wparam, void* lparam)
{
    UNREFERENCED_PARAMETER(status);
    UNREFERENCED_PARAMETER(wparam);
    UNREFERENCED_PARAMETER(lparam);

    switch (message_id) {
        case GW::UI::UIMessage::kMapLoaded: {
            OnMapLoaded();
        }
        break;
    }
}

bool AutoQuit::UseSkillOnTargetWithTimer(GW::Constants::SkillID skill_id, uint32_t target, int32_t delay)
{
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    const auto skillbarSkill = skillbar->GetSkillById(skill_id);
    if (!skillbarSkill) return false;

    const auto now = std::chrono::steady_clock::now();
    auto it = skill_timers.find(skill_id);
    if (it != skill_timers.end()) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        if (elapsed < delay) return false;
    }
    if (skillbarSkill->GetRecharge() != 0) return false;

    GW::GameThread::Enqueue([skill_id, target]() -> void {
        GW::SkillbarMgr::UseSkillByID(static_cast<uint32_t>(skill_id), target);
    });
    skill_timers[skill_id] = now;
    return true;
}

void AutoQuit::KilroyStandUp(GW::AgentLiving* player)
{
    if (!player) {
        return;
    }

    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) {
        return;
    }

    if (player->energy < 1.0f) {
        UseSkillOnTargetWithTimer(GW::Constants::SkillID::STAND_UP, 0, 20);
    }
}

void AutoQuit::Travel(GW::AgentLiving* player, bool isKilroy)
{
    if (!player) {
        return;
    }

    if (player->hp == 0.0f)
    {
        // Resigned or Died
        return;
    }

    // Cooldown check - only allow Travel call every 3 seconds
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_travel_time).count();
    constexpr int64_t travel_cooldown = 3000;
    if (elapsed < travel_cooldown) {
        return;
    }

    if (isKilroy) {
        // Kilroy mode: check max energy threshold
        if (player->max_energy >= kilroy_energy_max_threshold) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Player Max Energy: %.2f - Traveling to Outpost ID: %d", static_cast<float>(player->max_energy), static_cast<int>(last_outpost_id));
            LogMessage(msg);
            GW::Map::Travel(last_outpost_id);
            last_travel_time = now;
        }
    } else {
        // Normal mode: check HP percentage based on player level
        const float hp_threshold = (player->level < 10) ? hp_percent_below_level_10 : hp_percent_above_level_10;
        if (player->hp <= hp_threshold) {
            const float playerCurrentHp = player->max_hp * player->hp;
            char msg[128];
            snprintf(msg, sizeof(msg), "Player HP: %.0f%% (%.0f/%.0f) - Traveling to Outpost ID: %d",
                     player->hp * 100.0f, playerCurrentHp, static_cast<float>(player->max_hp), static_cast<int>(last_outpost_id));
            LogMessage(msg);
            GW::Map::Travel(last_outpost_id);
            last_travel_time = now;
        }
    }
}

void AutoQuit::Update(float delta)
{
    UNREFERENCED_PARAMETER(delta);

    GW::AgentLiving* player = GW::Agents::GetControlledCharacter();
    if (!player) return;

    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) return;

    if (last_outpost_id == GW::Constants::MapID::None) return;

    bool isKilroy = false;
    if (GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Brawling) != nullptr) {
        isKilroy = true;
    }

    Travel(player, isKilroy);

    if (isKilroy)
    {
        KilroyStandUp(player);
    }
}

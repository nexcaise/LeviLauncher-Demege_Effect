#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <optional>
#include <array>
#include <algorithm>

#include "imgui.h"

#define LOG_TAG "NoHurtCam"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static bool g_enabled = true;
static bool g_lastDamage = false;

static float g_damageEffectTime = 0.0f;
static constexpr float DAMAGE_EFFECT_DURATION = 0.6f;

static std::optional<std::array<float, 3>> (*g_tryGetDamageBob_orig)(
    void**, void*, float
) = nullptr;

static std::optional<std::array<float, 3>>
VanillaCameraAPI_tryGetDamageBob_hook(
    void** self,
    void* traits,
    float a
) {
    auto ret = g_tryGetDamageBob_orig(self, traits, a);

    g_lastDamage = ret.has_value();

    if (g_lastDamage) {
        g_damageEffectTime = DAMAGE_EFFECT_DURATION;
    }

    if (g_enabled && g_lastDamage) {
        return std::nullopt;
    }

    return ret;
}

static void DrawDamageBorderEffect() {
    if (g_damageEffectTime <= 0.0f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    float alpha = g_damageEffectTime / DAMAGE_EFFECT_DURATION;
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    ImU32 color = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.2f, 0.6f, 1.0f, alpha * 0.8f)
    );

    float thickness = 18.0f;

    ImVec2 p0(0, 0);
    ImVec2 p1(io.DisplaySize.x, io.DisplaySize.y);

    draw->AddRect(p0, p1, color, 0.0f, 0, thickness);
}

void DrawNoHurtCamMenu() {
    ImGui::Begin("NoHurtCam");

    ImGui::Checkbox("Enable NoHurtCam", &g_enabled);

    if (g_lastDamage) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Damage Detected");
    } else {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "No Damage");
    }

    ImGui::End();

    g_lastDamage = false;
}

static bool parseMapsLine(
    const std::string& line,
    uintptr_t& start,
    uintptr_t& end
) {
    return sscanf(line.c_str(), "%lx-%lx", &start, &end) == 2;
}

static bool findAndHookVanillaCameraAPI() {
    void* mcLib = dlopen("libminecraftpe.so", RTLD_NOLOAD);
    if (!mcLib) mcLib = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!mcLib) return false;

    constexpr const char* RTTI_NAME = "16VanillaCameraAPI";
    const size_t RTTI_LEN = strlen(RTTI_NAME);

    uintptr_t rttiNameAddr = 0;
    uintptr_t typeinfoAddr = 0;
    uintptr_t vtableAddr = 0;

    std::ifstream maps;
    std::string line;

    maps.open("/proc/self/maps");
    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        if (line.find("r--p") == std::string::npos &&
            line.find("r-xp") == std::string::npos) continue;

        uintptr_t start, end;
        if (!parseMapsLine(line, start, end)) continue;

        for (uintptr_t p = start; p < end - RTTI_LEN; ++p) {
            if (memcmp((void*)p, RTTI_NAME, RTTI_LEN) == 0) {
                rttiNameAddr = p;
                break;
            }
        }
        if (rttiNameAddr) break;
    }
    maps.close();
    if (!rttiNameAddr) return false;

    maps.open("/proc/self/maps");
    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        if (line.find("r--p") == std::string::npos) continue;

        uintptr_t start, end;
        if (!parseMapsLine(line, start, end)) continue;

        for (uintptr_t p = start; p < end - sizeof(void*); p += sizeof(void*)) {
            if (*(uintptr_t*)p == rttiNameAddr) {
                typeinfoAddr = p - sizeof(void*);
                break;
            }
        }
        if (typeinfoAddr) break;
    }
    maps.close();
    if (!typeinfoAddr) return false;

    maps.open("/proc/self/maps");
    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        if (line.find("r--p") == std::string::npos) continue;

        uintptr_t start, end;
        if (!parseMapsLine(line, start, end)) continue;

        for (uintptr_t p = start; p < end - sizeof(void*); p += sizeof(void*)) {
            if (*(uintptr_t*)p == typeinfoAddr) {
                vtableAddr = p + sizeof(void*);
                break;
            }
        }
        if (vtableAddr) break;
    }
    maps.close();
    if (!vtableAddr) return false;

    void** slot = (void**)(vtableAddr + 2 * sizeof(void*));
    g_tryGetDamageBob_orig =
        (decltype(g_tryGetDamageBob_orig))(*slot);

    if (!g_tryGetDamageBob_orig) return false;

    uintptr_t page = (uintptr_t)slot & ~4095UL;
    mprotect((void*)page, 4096, PROT_READ | PROT_WRITE);
    *slot = (void*)VanillaCameraAPI_tryGetDamageBob_hook;
    mprotect((void*)page, 4096, PROT_READ);

    return true;
}

extern "C" {
__attribute__((visibility("default")))
void LeviMod_Load() {
    findAndHookVanillaCameraAPI();
}
}

__attribute__((constructor))
void NoHurtCam_Init() {
    findAndHookVanillaCameraAPI();
}

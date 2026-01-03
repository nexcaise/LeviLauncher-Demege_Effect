// ===============================
// INCLUDES
// ===============================
#include <jni.h>
#include <android/log.h>
#include <android/input.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>

#include "pl/Hook.h"
#include "pl/Gloss.h"

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"

#include <chrono>
#include <mutex>
#include <fstream>
#include <sstream>
#include <string>
#include <optional>
#include <array>

// ===============================
// LOG
// ===============================
#define LOG_TAG "MoveClient"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ===============================
// IMGUI GLOBALS
// ===============================
static bool g_initialized = false;
static int g_width = 0, g_height = 0;
static EGLContext g_targetcontext = EGL_NO_CONTEXT;
static EGLSurface g_targetsurface = EGL_NO_SURFACE;
static EGLBoolean (*orig_eglswapbuffers)(EGLDisplay, EGLSurface) = nullptr;

// ===============================
// MENU STATE
// ===============================
static int current_tab = 0;

// ===============================
// CROSSHAIR
// ===============================
static bool crosshair_enabled = false;
static float crosshair_length_x = 35.0f;
static float crosshair_length_y = 35.0f;
static float crosshair_thickness = 3.0f;
static ImVec4 crosshair_color = ImVec4(0.f, 1.f, 0.f, 1.f);

// ===============================
// NO HURT CAM
// ===============================
static bool g_nohurtcam_enabled = true;
static bool g_nohurtcam_hooked = false;

static std::optional<std::array<float, 3>> (*orig_tryGetDamageBob)(
    void**, void*, float
) = nullptr;

// ===============================
// NO HURT CAM HOOK
// ===============================
static std::optional<std::array<float, 3>>
VanillaCameraAPI_tryGetDamageBob_hook(
    void** self,
    void* traits,
    float a
) {
    if (g_nohurtcam_enabled)
        return std::nullopt;

    return orig_tryGetDamageBob(self, traits, a);
}

// ===============================
// MAP PARSER
// ===============================
static bool parseMapsLine(const std::string& line, uintptr_t& start, uintptr_t& end) {
    return sscanf(line.c_str(), "%lx-%lx", &start, &end) == 2;
}

// ===============================
// FIND + HOOK NOHURTCAM (ONCE)
// ===============================
static bool hookNoHurtCam() {
    if (g_nohurtcam_hooked) return true;

    void* mc = dlopen("libminecraftpe.so", RTLD_NOLOAD);
    if (!mc) mc = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!mc) return false;

    constexpr const char* RTTI = "16VanillaCameraAPI";
    uintptr_t rttiAddr = 0, typeinfo = 0, vtable = 0;

    std::ifstream maps("/proc/self/maps");
    std::string line;

    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        uintptr_t s, e;
        if (!parseMapsLine(line, s, e)) continue;

        for (uintptr_t p = s; p < e; ++p) {
            if (!memcmp((void*)p, RTTI, strlen(RTTI))) {
                rttiAddr = p;
                break;
            }
        }
        if (rttiAddr) break;
    }
    maps.close();
    if (!rttiAddr) return false;

    maps.open("/proc/self/maps");
    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        uintptr_t s, e;
        if (!parseMapsLine(line, s, e)) continue;

        for (uintptr_t p = s; p < e; p += sizeof(void*)) {
            if (*(uintptr_t*)p == rttiAddr) {
                typeinfo = p - sizeof(void*);
                break;
            }
        }
        if (typeinfo) break;
    }
    maps.close();
    if (!typeinfo) return false;

    maps.open("/proc/self/maps");
    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        uintptr_t s, e;
        if (!parseMapsLine(line, s, e)) continue;

        for (uintptr_t p = s; p < e; p += sizeof(void*)) {
            if (*(uintptr_t*)p == typeinfo) {
                vtable = p + sizeof(void*);
                break;
            }
        }
        if (vtable) break;
    }
    maps.close();
    if (!vtable) return false;

    void** slot = (void**)(vtable + 2 * sizeof(void*));
    orig_tryGetDamageBob = (decltype(orig_tryGetDamageBob))(*slot);

    uintptr_t page = (uintptr_t)slot & ~4095UL;
    mprotect((void*)page, 4096, PROT_READ | PROT_WRITE);
    *slot = (void*)VanillaCameraAPI_tryGetDamageBob_hook;
    mprotect((void*)page, 4096, PROT_READ);

    g_nohurtcam_hooked = true;
    LOGI("NoHurtCam hooked");
    return true;
}

// ===============================
// DRAW CROSSHAIR
// ===============================
static void draw_crosshair_overlay() {
    if (!crosshair_enabled) return;
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    ImVec2 c(g_width * 0.5f, g_height * 0.5f);
    ImU32 col = ImGui::ColorConvertFloat4ToU32(crosshair_color);

    d->AddLine({c.x - crosshair_length_x, c.y}, {c.x + crosshair_length_x, c.y}, col, crosshair_thickness);
    d->AddLine({c.x, c.y - crosshair_length_y}, {c.x, c.y + crosshair_length_y}, col, crosshair_thickness);
}

// ===============================
// IMGUI TABS
// ===============================
static void draw_tab_crosshair() {
    ImGui::Checkbox("Enable Crosshair", &crosshair_enabled);
    ImGui::SliderFloat("Length X", &crosshair_length_x, 5.f, 150.f);
    ImGui::SliderFloat("Length Y", &crosshair_length_y, 5.f, 150.f);
    ImGui::SliderFloat("Thickness", &crosshair_thickness, 1.f, 10.f);
    ImGui::ColorEdit4("Color", (float*)&crosshair_color);
}

static void draw_tab_effect() {
    ImGui::Checkbox("No Hurt Cam", &g_nohurtcam_enabled);
}

// ===============================
// MENU
// ===============================
static void drawmenu() {
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("MoveClient");

    if (ImGui::Button("Crosshair")) current_tab = 0;
    ImGui::SameLine();
    if (ImGui::Button("Effect")) current_tab = 1;

    ImGui::Separator();

    if (current_tab == 0) draw_tab_crosshair();
    if (current_tab == 1) draw_tab_effect();

    ImGui::End();
}

// ===============================
// IMGUI RENDER
// ===============================
static void render() {
    if (!g_initialized) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_width, g_height);
    ImGui::NewFrame();

    drawmenu();
    draw_crosshair_overlay();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ===============================
// EGL HOOK
// ===============================
static EGLBoolean hook_eglswapbuffers(EGLDisplay dpy, EGLSurface surf) {
    if (!orig_eglswapbuffers) return EGL_FALSE;

    EGLint w, h;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w < 400 || h < 400) return orig_eglswapbuffers(dpy, surf);

    g_width = w;
    g_height = h;

    if (!g_initialized) {
        ImGui::CreateContext();
        ImGui_ImplAndroid_Init();
        ImGui_ImplOpenGL3_Init("#version 300 es");
        hookNoHurtCam();
        g_initialized = true;
    }

    render();
    return orig_eglswapbuffers(dpy, surf);
}

// ===============================
// THREAD
// ===============================
static void* mainthread(void*) {
    sleep(3);
    GlossInit(true);

    GHandle egl = GlossOpen("libEGL.so");
    void* swap = GlossSymbol(egl, "eglSwapBuffers", nullptr);
    GlossHook(swap, (void*)hook_eglswapbuffers, (void**)&orig_eglswapbuffers);
    return nullptr;
}

__attribute__((constructor))
void init() {
    pthread_t t;
    pthread_create(&t, nullptr, mainthread, nullptr);
}

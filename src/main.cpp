#include <jni.h>
#include <android/log.h>
#include <android/input.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <fstream>
#include <string>
#include <optional>
#include <array>
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_android.h"

#define LOG_TAG "NoHurtCam"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

/* ============================================================
   GLOBAL
============================================================ */
static bool g_imguiInit = false;
static int g_width = 0, g_height = 0;

static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLSurface g_surf = EGL_NO_SURFACE;

static bool g_enabled = true;
static bool g_lastDamage = false;
static float g_damageEffectTime = 0.0f;
static constexpr float DAMAGE_EFFECT_DURATION = 0.6f;

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

/* ============================================================
   VanillaCameraAPI Hook
============================================================ */
static std::optional<std::array<float,3>> (*orig_tryGetDamageBob)(
    void**, void*, float
) = nullptr;

static std::optional<std::array<float,3>>
hook_tryGetDamageBob(void** self, void* traits, float a) {
    if (!orig_tryGetDamageBob)
        return std::nullopt;

    auto ret = orig_tryGetDamageBob(self, traits, a);

    if (ret.has_value()) {
        g_lastDamage = true;
        g_damageEffectTime = DAMAGE_EFFECT_DURATION;
    }

    if (g_enabled && ret.has_value())
        return std::nullopt;

    return ret;
}

/* ============================================================
   ImGui UI
============================================================ */
static void DrawMenu() {
    ImGui::Begin("NoHurtCam");
    ImGui::Checkbox("Enable NoHurtCam", &g_enabled);
    ImGui::Separator();
    ImGui::Text(g_lastDamage ? "Damage Detected" : "No Damage");
    ImGui::End();
    g_lastDamage = false;
}

static void DrawDamageEffect() {
    if (g_damageEffectTime <= 0.0f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    float a = std::clamp(
        g_damageEffectTime / DAMAGE_EFFECT_DURATION,
        0.0f, 1.0f
    );

    ImU32 col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.2f, 0.6f, 1.0f, a * 0.85f)
    );

    dl->AddRect(
        ImVec2(0,0),
        io.DisplaySize,
        col,
        0.0f,
        0,
        18.0f
    );

    g_damageEffectTime -= io.DeltaTime;
}

/* ============================================================
   ImGui Init
============================================================ */
static void InitImGui() {
    if (g_imguiInit) return;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init("#version 300 es");

    g_imguiInit = true;
}

/* ============================================================
   eglSwapBuffers Hook
============================================================ */
static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT)
        return orig_eglSwapBuffers(dpy, surf);

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

    if (w < 300 || h < 300)
        return orig_eglSwapBuffers(dpy, surf);

    if (g_ctx == EGL_NO_CONTEXT) {
        g_ctx = ctx;
        g_surf = surf;
    }

    if (ctx != g_ctx || surf != g_surf)
        return orig_eglSwapBuffers(dpy, surf);

    g_width = w;
    g_height = h;

    InitImGui();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(w, h);
    ImGui::NewFrame();

    DrawMenu();
    DrawDamageEffect();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return orig_eglSwapBuffers(dpy, surf);
}

/* ============================================================
   VanillaCameraAPI Scan & Hook
============================================================ */
static bool parseMaps(const std::string& l, uintptr_t& s, uintptr_t& e) {
    return sscanf(l.c_str(), "%lx-%lx", &s, &e) == 2;
}

static void HookVanillaCameraAPI() {
    if (orig_tryGetDamageBob)
        return;

    std::ifstream maps("/proc/self/maps");
    std::string line;
    uintptr_t rtti = 0, ti = 0, vt = 0;

    const char* RTTI = "16VanillaCameraAPI";

    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        uintptr_t s,e;
        if (!parseMaps(line,s,e)) continue;
        for (uintptr_t p=s;p<e-16;p++)
            if (!memcmp((void*)p,RTTI,16)) { rtti=p; break; }
        if (rtti) break;
    }
    maps.clear(); maps.seekg(0);

    while (std::getline(maps, line)) {
        uintptr_t s,e;
        if (!parseMaps(line,s,e)) continue;
        for (uintptr_t p=s;p<e-8;p+=8)
            if (*(uintptr_t*)p==rtti) { ti=p-8; break; }
        if (ti) break;
    }
    maps.clear(); maps.seekg(0);

    while (std::getline(maps, line)) {
        uintptr_t s,e;
        if (!parseMaps(line,s,e)) continue;
        for (uintptr_t p=s;p<e-8;p+=8)
            if (*(uintptr_t*)p==ti) { vt=p+8; break; }
        if (vt) break;
    }

    if (!vt) return;

    void** slot = (void**)(vt + 2 * sizeof(void*));
    orig_tryGetDamageBob =
        (decltype(orig_tryGetDamageBob))(*slot);

    if (!orig_tryGetDamageBob) return;

    uintptr_t page = (uintptr_t)slot & ~4095UL;
    mprotect((void*)page,4096,PROT_READ|PROT_WRITE);
    *slot = (void*)hook_tryGetDamageBob;
    mprotect((void*)page,4096,PROT_READ);

    LOGI("VanillaCameraAPI hooked");
}

/* ============================================================
   Thread Init
============================================================ */
static void* init_thread(void*) {
    sleep(3);

    void* egl = dlopen("libEGL.so", RTLD_LAZY);
    void* sym = dlsym(egl, "eglSwapBuffers");

    orig_eglSwapBuffers = (decltype(orig_eglSwapBuffers))sym;
    uintptr_t page = (uintptr_t)sym & ~4095UL;
    mprotect((void*)page,4096,PROT_READ|PROT_WRITE);
    *(void**)sym = (void*)hook_eglSwapBuffers;
    mprotect((void*)page,4096,PROT_READ);

    HookVanillaCameraAPI();
    return nullptr;
}

/* ============================================================
   ENTRY
============================================================ */
__attribute__((constructor))
void main_init() {
    pthread_t t;
    pthread_create(&t, nullptr, init_thread, nullptr);
}

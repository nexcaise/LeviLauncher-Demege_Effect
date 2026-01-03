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

#include <fstream>
#include <sstream>
#include <string>
#include <optional>
#include <array>

// =====================================================
// LOG
// =====================================================
#define LOG_TAG "DisplayCPS"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// =====================================================
// IMGUI / EGL STATE
// =====================================================
static bool g_initialized = false;
static int g_width = 0, g_height = 0;
static EGLContext g_targetcontext = EGL_NO_CONTEXT;
static EGLSurface g_targetsurface = EGL_NO_SURFACE;
static EGLBoolean (*orig_eglswapbuffers)(EGLDisplay, EGLSurface) = nullptr;

// =====================================================
// CROSSHAIR
// =====================================================
static bool crosshair_enabled = false;
static float crosshair_length_x = 35.f;
static float crosshair_length_y = 35.f;
static float crosshair_thickness = 3.f;
static ImVec4 crosshair_color = ImVec4(0.f, 1.f, 0.f, 1.f);

// =====================================================
// NO HURT CAM
// =====================================================
static bool nohurtcam_enabled = true;

static std::optional<std::array<float, 3>> (*orig_tryGetDamageBob)(
    void**, void*, float
) = nullptr;

static std::optional<std::array<float, 3>>
hook_tryGetDamageBob(void** self, void* traits, float a) {
    if (nohurtcam_enabled)
        return std::nullopt;
    return orig_tryGetDamageBob(self, traits, a);
}

// =====================================================
// NO HURT CAM HOOK
// =====================================================
static bool parseMapsLine(const std::string& line, uintptr_t& s, uintptr_t& e) {
    return sscanf(line.c_str(), "%lx-%lx", &s, &e) == 2;
}

static void hook_nohurtcam() {
    void* mc = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!mc) return;

    constexpr const char* RTTI = "16VanillaCameraAPI";
    uintptr_t rtti = 0, typeinfo = 0, vtable = 0;

    std::ifstream maps("/proc/self/maps");
    std::string line;

    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        uintptr_t s, e;
        if (!parseMapsLine(line, s, e)) continue;
        for (uintptr_t p = s; p < e; ++p) {
            if (!memcmp((void*)p, RTTI, strlen(RTTI))) {
                rtti = p;
                break;
            }
        }
        if (rtti) break;
    }
    maps.close();
    if (!rtti) return;

    maps.open("/proc/self/maps");
    while (std::getline(maps, line)) {
        if (line.find("libminecraftpe.so") == std::string::npos) continue;
        uintptr_t s, e;
        if (!parseMapsLine(line, s, e)) continue;
        for (uintptr_t p = s; p < e; p += sizeof(void*)) {
            if (*(uintptr_t*)p == rtti) {
                typeinfo = p - sizeof(void*);
                break;
            }
        }
        if (typeinfo) break;
    }
    maps.close();
    if (!typeinfo) return;

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
    if (!vtable) return;

    void** slot = (void**)(vtable + 2 * sizeof(void*));
    orig_tryGetDamageBob = (decltype(orig_tryGetDamageBob))(*slot);

    uintptr_t page = (uintptr_t)slot & ~4095UL;
    mprotect((void*)page, 4096, PROT_READ | PROT_WRITE);
    *slot = (void*)hook_tryGetDamageBob;
    mprotect((void*)page, 4096, PROT_READ);
}

// =====================================================
// DRAW
// =====================================================
static void draw_crosshair() {
    if (!crosshair_enabled) return;
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    ImVec2 c(g_width * 0.5f, g_height * 0.5f);
    ImU32 col = ImGui::ColorConvertFloat4ToU32(crosshair_color);

    d->AddLine({c.x - crosshair_length_x, c.y},
               {c.x + crosshair_length_x, c.y}, col, crosshair_thickness);
    d->AddLine({c.x, c.y - crosshair_length_y},
               {c.x, c.y + crosshair_length_y}, col, crosshair_thickness);
}

static void draw_menu() {
    ImGui::Begin("Editor");

    ImGui::Checkbox("Enable Crosshair", &crosshair_enabled);
    ImGui::Checkbox("Enable NoHurtCam", &nohurtcam_enabled);

    ImGui::SliderFloat("Length X", &crosshair_length_x, 5, 150);
    ImGui::SliderFloat("Length Y", &crosshair_length_y, 5, 150);
    ImGui::SliderFloat("Thickness", &crosshair_thickness, 1, 10);
    ImGui::ColorEdit4("Color", (float*)&crosshair_color);

    ImGui::End();
}

// =====================================================
// EGL HOOK
// =====================================================
static EGLBoolean hook_eglswapbuffers(EGLDisplay d, EGLSurface s) {
    if (!orig_eglswapbuffers)
        return EGL_FALSE;

    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT)
        return orig_eglswapbuffers(d, s);

    EGLint w, h;
    eglQuerySurface(d, s, EGL_WIDTH, &w);
    eglQuerySurface(d, s, EGL_HEIGHT, &h);

    if (w < 500 || h < 500)
        return orig_eglswapbuffers(d, s);

    if (!g_initialized) {
        g_targetcontext = ctx;
        g_targetsurface = s;

        ImGui::CreateContext();
        ImGui_ImplAndroid_Init();
        ImGui_ImplOpenGL3_Init("#version 300 es");
        g_initialized = true;

        hook_nohurtcam();
    }

    g_width = w;
    g_height = h;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(w, h);
    ImGui::NewFrame();

    draw_menu();
    draw_crosshair();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return orig_eglswapbuffers(d, s);
}

// =====================================================
// THREAD
// =====================================================
static void* mainthread(void*) {
    sleep(3);

    GlossInit(true);
    GHandle egl = GlossOpen("libEGL.so");
    if (!egl) return nullptr;

    void* swap = (void*)GlossSymbol(egl, "eglSwapBuffers", nullptr);
    if (!swap) return nullptr;

    GlossHook(swap, (void*)hook_eglswapbuffers,
              (void**)&orig_eglswapbuffers);
    return nullptr;
}

__attribute__((constructor))
void init() {
    pthread_t t;
    pthread_create(&t, nullptr, mainthread, nullptr);
}

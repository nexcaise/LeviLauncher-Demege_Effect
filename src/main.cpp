#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/input.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"

#include <dlfcn.h>

static bool imgui_init = false;
static int screen_w = 0, screen_h = 0;

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface);

static bool menu_open = true;

static bool enchant_enabled[7];
static int enchant_level[7];

static const char* enchant_names[7] = {
    "Copper Spear",
    "Diamond Spear",
    "Gold Spear",
    "Iron Spear",
    "Netherite Spear",
    "Stone Spear",
    "Wood Spear"
};

static GLuint icon_texture = 0;

static unsigned char icon_rgba[16 * 16 * 4] = {
    255,255,255,0, 255,255,255,0, 255,255,255,0, 255,255,255,0,
    255,255,255,0, 255,255,255,255,255,255,255,255,255,255,255,0,
    255,255,255,0, 255,255,255,255,255,255,255,255,255,255,255,0,
    255,255,255,0, 255,255,255,0, 255,255,255,0, 255,255,255,0,
};

static void InitTexture() {
    glGenTextures(1, &icon_texture);
    glBindTexture(GL_TEXTURE_2D, icon_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        16,
        16,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        icon_rgba
    );
}

static void InitImGui(EGLDisplay display, EGLSurface surface) {
    eglQuerySurface(display, surface, EGL_WIDTH, &screen_w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &screen_h);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(screen_w, screen_h);
    io.FontGlobalScale = 1.3f;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.ScaleAllSizes(1.2f);

    ImGui_ImplAndroid_Init(nullptr);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    InitTexture();

    imgui_init = true;
}

static void DrawMenu() {
    if (!menu_open) return;

    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Once);
    ImGui::Begin("Enchant Menu", &menu_open, ImGuiWindowFlags_NoCollapse);

    if (ImGui::CollapsingHeader("Enchant", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < 7; i++) {
            ImGui::PushID(i);

            ImGui::Image((void*)(intptr_t)icon_texture, ImVec2(32, 32));
            ImGui::SameLine();

            ImGui::Checkbox(enchant_names[i], &enchant_enabled[i]);
            ImGui::SameLine(360);

            if (ImGui::Button("+", ImVec2(40, 32))) {
                if (enchant_level[i] < 10)
                    enchant_level[i]++;
            }

            ImGui::SameLine();
            ImGui::Text("Lv %d", enchant_level[i]);
            ImGui::SameLine();

            if (ImGui::Button("-", ImVec2(40, 32))) {
                if (enchant_level[i] > 0)
                    enchant_level[i]--;
            }

            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::End();
}

extern "C"
EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!imgui_init) {
        InitImGui(display, surface);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    DrawMenu();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return orig_eglSwapBuffers(display, surface);
}

__attribute__((constructor))
static void HookEGL() {
    void* handle = dlopen("libEGL.so", RTLD_LAZY);
    orig_eglSwapBuffers = (EGLBoolean(*)(EGLDisplay, EGLSurface))
        dlsym(handle, "eglSwapBuffers");
}

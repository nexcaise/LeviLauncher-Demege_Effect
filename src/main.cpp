#include <jni.h>
#include <android/input.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <unistd.h>

#include "pl/Hook.h"
#include "pl/Gloss.h"

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"

static bool g_initialized = false;
static int g_width = 0, g_height = 0;
static EGLBoolean (*orig_eglswapbuffers)(EGLDisplay, EGLSurface) = nullptr;

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

const unsigned char epd_bitmap_copper_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9c,0xfe,0x79,0xfe,0xf1,0xff,0x63,0xff,0x43,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_diamond_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9e,0xfe,0x7d,0xfe,0xf9,0xff,0x73,0xff,0x63,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_gold_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9e,0xfe,0x7d,0xfe,0xfd,0xff,0x7b,0xff,0x7b,0xfe,0x17,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_iron_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9c,0xfe,0x79,0xfe,0xf1,0xff,0x63,0xff,0x43,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_netherite_spear[] = {
0xff,0xf8,0xff,0xe0,0xff,0x80,0xfe,0x01,0xfe,0x01,0xff,0x03,0xff,0x03,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_stone_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9c,0xfe,0x39,0xfe,0x71,0xff,0x63,0xff,0x43,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_wood_spear[] = {
0xff,0xf8,0xff,0xe0,0xff,0x80,0xfe,0x01,0xfe,0x01,0xff,0x03,0xff,0x03,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char* epd_bitmap_allArray[7] = {
    epd_bitmap_copper_spear,
    epd_bitmap_diamond_spear,
    epd_bitmap_gold_spear,
    epd_bitmap_iron_spear,
    epd_bitmap_netherite_spear,
    epd_bitmap_stone_spear,
    epd_bitmap_wood_spear
};

static GLuint enchant_textures[7];

static GLuint make_texture(const unsigned char* bmp) {
    unsigned char rgba[16 * 16 * 4] = {};
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int bi = (y * 16 + x) / 8;
            int bt = 7 - (x % 8);
            if ((bmp[bi] >> bt) & 1) {
                int i = (y * 16 + x) * 4;
                rgba[i] = 255;
                rgba[i+1] = 255;
                rgba[i+2] = 255;
                rgba[i+3] = 255;
            }
        }
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

static void draw_menu() {
    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Once);
    ImGui::Begin("Enchant", nullptr, ImGuiWindowFlags_NoCollapse);
    for (int i = 0; i < 7; i++) {
        ImGui::PushID(i);
        ImGui::Image((void*)(intptr_t)enchant_textures[i], ImVec2(32, 32));
        ImGui::SameLine();
        ImGui::Checkbox(enchant_names[i], &enchant_enabled[i]);
        ImGui::SameLine();
        if (ImGui::Button("+")) enchant_level[i]++;
        ImGui::SameLine();
        ImGui::Text("Lv %d", enchant_level[i]);
        ImGui::SameLine();
        if (ImGui::Button("-") && enchant_level[i] > 0) enchant_level[i]--;
        ImGui::PopID();
    }
    ImGui::End();
}

static void setup() {
    if (g_initialized) return;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(g_width, g_height);
    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init("#version 300 es");
    for (int i = 0; i < 7; i++)
        enchant_textures[i] = make_texture(epd_bitmap_allArray[i]);
    g_initialized = true;
}

static void render() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_width, g_height);
    ImGui::NewFrame();
    draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static EGLBoolean hook_eglswapbuffers(EGLDisplay dpy, EGLSurface surf) {
    EGLint w, h;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w > 500 && h > 500) {
        g_width = w;
        g_height = h;
        setup();
        render();
    }
    return orig_eglswapbuffers(dpy, surf);
}

static int32_t (*orig_input)(void*, void*, bool, long, uint32_t*, AInputEvent**) = 0;

static int32_t hook_input(void* thiz, void* a1, bool a2, long a3, uint32_t* a4, AInputEvent** ev) {
    int32_t r = orig_input(thiz, a1, a2, a3, a4, ev);
    if (r == 0 && ev && *ev && g_initialized)
        ImGui_ImplAndroid_HandleInputEvent(*ev);
    return r;
}

static void* mainthread(void*) {
    sleep(3);
    GlossInit(true);
    GHandle egl = GlossOpen("libEGL.so");
    void* swap = (void*)GlossSymbol(egl, "eglSwapBuffers", nullptr);
    GlossHook(swap, (void*)hook_eglswapbuffers, (void**)&orig_eglswapbuffers);

    GHandle inp = GlossOpen("libinput.so");
    void* sym = (void*)GlossSymbol(
        inp,
        "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE",
        nullptr
    );
    GlossHook(sym, (void*)hook_input, (void**)&orig_input);
    return nullptr;
}

__attribute__((constructor))
void init() {
    pthread_t t;
    pthread_create(&t, 0, mainthread, 0);
}

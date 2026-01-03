#include <jni.h>
#include <android/log.h>
#include <android/input.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fstream>
#include <string>
#include <optional>
#include <array>

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "ImGui/backends/imgui_impl_opengl3.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"MoveClient",__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"MoveClient",__VA_ARGS__)

static bool g_imgui_init = false;
static bool g_nohurtcam = true;

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay,EGLSurface);
static int32_t (*orig_AInputQueue_getEvent)(AInputQueue*,AInputEvent**);
static int32_t (*orig_AInputQueue_preDispatchEvent)(AInputQueue*,AInputEvent*);

static std::optional<std::array<float,3>> (*orig_tryGetDamageBob)(void**,void*,float);

static std::optional<std::array<float,3>>
hook_tryGetDamageBob(void** self, void* traits, float a) {
    if (g_nohurtcam) return std::nullopt;
    return orig_tryGetDamageBob(self, traits, a);
}

static bool parseMaps(const std::string& l, uintptr_t& s, uintptr_t& e) {
    return sscanf(l.c_str(), "%lx-%lx", &s, &e) == 2;
}

static bool hook_NoHurtCam() {
    void* lib = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!lib) return false;

    const char* RTTI = "16VanillaCameraAPI";
    size_t RTTI_LEN = strlen(RTTI);

    uintptr_t rtti = 0, typeinfo = 0, vtable = 0;
    std::ifstream maps;
    std::string line;

    maps.open("/proc/self/maps");
    while (std::getline(maps,line)) {
        if (line.find("libminecraftpe.so")==std::string::npos) continue;
        uintptr_t s,e;
        if (!parseMaps(line,s,e)) continue;
        for (uintptr_t p=s;p<e-RTTI_LEN;p++) {
            if (!memcmp((void*)p,RTTI,RTTI_LEN)) {
                rtti=p;
                break;
            }
        }
        if (rtti) break;
    }
    maps.close();
    if (!rtti) return false;

    maps.open("/proc/self/maps");
    while (std::getline(maps,line)) {
        if (line.find("libminecraftpe.so")==std::string::npos) continue;
        uintptr_t s,e;
        if (!parseMaps(line,s,e)) continue;
        for (uintptr_t p=s;p<e-sizeof(void*);p+=sizeof(void*)) {
            if (*(uintptr_t*)p==rtti) {
                typeinfo=p-sizeof(void*);
                break;
            }
        }
        if (typeinfo) break;
    }
    maps.close();
    if (!typeinfo) return false;

    maps.open("/proc/self/maps");
    while (std::getline(maps,line)) {
        if (line.find("libminecraftpe.so")==std::string::npos) continue;
        uintptr_t s,e;
        if (!parseMaps(line,s,e)) continue;
        for (uintptr_t p=s;p<e-sizeof(void*);p+=sizeof(void*)) {
            if (*(uintptr_t*)p==typeinfo) {
                vtable=p+sizeof(void*);
                break;
            }
        }
        if (vtable) break;
    }
    maps.close();
    if (!vtable) return false;

    void** slot = (void**)(vtable + 2*sizeof(void*));
    orig_tryGetDamageBob = (decltype(orig_tryGetDamageBob))*slot;

    uintptr_t page = (uintptr_t)slot & ~4095UL;
    mprotect((void*)page,4096,PROT_READ|PROT_WRITE);
    *slot = (void*)hook_tryGetDamageBob;
    mprotect((void*)page,4096,PROT_READ);

    return true;
}

static EGLBoolean hook_eglSwapBuffers(EGLDisplay d, EGLSurface s) {
    if (!g_imgui_init) {
        ImGui::CreateContext();
        ImGui_ImplAndroid_Init();
        ImGui_ImplOpenGL3_Init("#version 300 es");
        g_imgui_init = true;
    }

    EGLint w,h;
    eglQuerySurface(d,s,EGL_WIDTH,&w);
    eglQuerySurface(d,s,EGL_HEIGHT,&h);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(w,h);
    ImGui::NewFrame();

    ImGui::Begin("MoveClient");
    ImGui::Checkbox("NoHurtCam",&g_nohurtcam);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return orig_eglSwapBuffers(d,s);
}

static int32_t hook_getEvent(AInputQueue* q, AInputEvent** e) {
    int32_t r = orig_AInputQueue_getEvent(q,e);
    if (r>=0 && *e && g_imgui_init)
        ImGui_ImplAndroid_HandleInputEvent(*e);
    return r;
}

static int32_t hook_preDispatch(AInputQueue* q, AInputEvent* e) {
    if (g_imgui_init && ImGui_ImplAndroid_HandleInputEvent(e))
        return 1;
    return orig_AInputQueue_preDispatchEvent(q,e);
}

static void* init_thread(void*) {
    sleep(3);

    void* egl = dlopen("libEGL.so",RTLD_LAZY);
    void* andr = dlopen("libandroid.so",RTLD_LAZY);

    void* swap = dlsym(egl,"eglSwapBuffers");
    void* ge = dlsym(andr,"AInputQueue_getEvent");
    void* pd = dlsym(andr,"AInputQueue_preDispatchEvent");

    orig_eglSwapBuffers = (decltype(orig_eglSwapBuffers))swap;
    orig_AInputQueue_getEvent = (decltype(orig_AInputQueue_getEvent))ge;
    orig_AInputQueue_preDispatchEvent = (decltype(orig_AInputQueue_preDispatchEvent))pd;

    hook_NoHurtCam();

    uintptr_t page;

    page = (uintptr_t)swap & ~4095UL;
    mprotect((void*)page,4096,PROT_READ|PROT_WRITE);
    *(void**)swap = (void*)hook_eglSwapBuffers;

    page = (uintptr_t)ge & ~4095UL;
    mprotect((void*)page,4096,PROT_READ|PROT_WRITE);
    *(void**)ge = (void*)hook_getEvent;

    page = (uintptr_t)pd & ~4095UL;
    mprotect((void*)page,4096,PROT_READ|PROT_WRITE);
    *(void**)pd = (void*)hook_preDispatch;

    return nullptr;
}

__attribute__((constructor))
void entry() {
    pthread_t t;
    pthread_create(&t,0,init_thread,0);
}

// ---------------------------------------------------------------------------
// qtclrbridge — native C bridge implementation (MinGW/GCC friendly, x64).
//
// It boots the .NET runtime and reaches the ManagedBridge dispatcher entirely
// through runtime dynamic loading:
//
//     nethost.dll ── get_hostfxr_path() ──► hostfxr.dll
//         hostfxr_initialize_for_runtime_config()
//         hostfxr_get_runtime_delegate(load_assembly_and_get_function_pointer)
//         → one function pointer per [UnmanagedCallersOnly] method
//
// No import libraries and no .NET SDK headers are needed to compile this: the
// few hosting typedefs we use are declared inline below. You only need the
// .NET 8 runtime installed at run time.
// ---------------------------------------------------------------------------

#define QTCLRBRIDGE_BUILD
#include "qtclrbridge.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Minimal inline copies of the hostfxr / coreclr_delegates contracts.
// (Normally from nethost.h / hostfxr.h / coreclr_delegates.h in the SDK.)
// ---------------------------------------------------------------------------
typedef wchar_t char_t; // On Windows the host APIs use UTF-16 wide chars.

// nethost: get_hostfxr_path
struct get_hostfxr_parameters {
    size_t size;
    const char_t *assembly_path;
    const char_t *dotnet_root;
};
typedef int (__cdecl *get_hostfxr_path_fn)(
    char_t *buffer, size_t *buffer_size,
    const struct get_hostfxr_parameters *parameters);

// hostfxr handles/functions
typedef void *hostfxr_handle;
typedef int (__cdecl *hostfxr_initialize_for_runtime_config_fn)(
    const char_t *runtime_config_path, void *parameters, hostfxr_handle *host_context_handle);
typedef int (__cdecl *hostfxr_get_runtime_delegate_fn)(
    hostfxr_handle host_context_handle, int type, void **delegate);
typedef int (__cdecl *hostfxr_close_fn)(hostfxr_handle host_context_handle);

// enum hostfxr_delegate_type -> load_assembly_and_get_function_pointer == 5
#define HDT_LOAD_ASSEMBLY_AND_GET_FUNCTION_POINTER 5

// coreclr_delegates: load_assembly_and_get_function_pointer
typedef int (__cdecl *load_assembly_and_get_function_pointer_fn)(
    const char_t *assembly_path,
    const char_t *type_name,
    const char_t *method_name,
    const char_t *delegate_type_name, // pass UNMANAGEDCALLERSONLY_METHOD
    void *reserved,
    void **delegate);

// Sentinel telling the host the target is an [UnmanagedCallersOnly] method
// with a custom signature (rather than the default component entry point).
#define UNMANAGEDCALLERSONLY_METHOD ((const char_t *)-1)

// ---------------------------------------------------------------------------
// Managed method signatures (must match Bridge.cs exactly).
// ---------------------------------------------------------------------------
typedef int   (__cdecl *managed_initialize_fn)(const char *assemblyPathUtf8);
typedef int   (__cdecl *managed_add_fn)(int a, int b);
typedef void *(__cdecl *managed_greet_fn)(const char *nameUtf8);   // returns char*
typedef void  (__cdecl *managed_free_fn)(void *p);
typedef void *(__cdecl *managed_last_error_fn)(void);              // returns char*

// ---------------------------------------------------------------------------
// Bridge state.
// ---------------------------------------------------------------------------
static hostfxr_close_fn      g_hostfxr_close = NULL;
static hostfxr_handle        g_ctx = NULL;

static managed_initialize_fn g_initialize = NULL;
static managed_add_fn        g_add        = NULL;
static managed_greet_fn      g_greet      = NULL;
static managed_free_fn       g_free       = NULL;
static managed_last_error_fn g_last_error = NULL;

static wchar_t g_bridge_type[]   = L"ManagedBridge.Bridge, ManagedBridge";
static wchar_t g_managed_dll[MAX_PATH]     = {0};
static char    g_native_error[512]         = {0};

static void set_err(const char *msg) {
    strncpy(g_native_error, msg, sizeof(g_native_error) - 1);
    g_native_error[sizeof(g_native_error) - 1] = '\0';
}

// Resolve one [UnmanagedCallersOnly] method into a function pointer.
static int bind(load_assembly_and_get_function_pointer_fn load_fn,
                const wchar_t *method, void **out) {
    int rc = load_fn(g_managed_dll, g_bridge_type, method,
                     UNMANAGEDCALLERSONLY_METHOD, NULL, out);
    if (rc != 0 || *out == NULL) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Failed to bind managed method (hr=0x%x)", (unsigned)rc);
        set_err(buf);
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// qtclr_start
// ---------------------------------------------------------------------------
QTCLR_API int qtclr_start(const wchar_t *managedBridgeDir) {
    if (g_initialize) return 0; // already started

    // 1) Locate hostfxr via nethost.dll (shipped with the .NET runtime).
    HMODULE nethost = LoadLibraryW(L"nethost.dll");
    if (!nethost) { set_err("nethost.dll not found. Install the .NET 8 runtime."); return -1; }

    get_hostfxr_path_fn get_hostfxr_path =
        (get_hostfxr_path_fn)GetProcAddress(nethost, "get_hostfxr_path");
    if (!get_hostfxr_path) { set_err("get_hostfxr_path missing in nethost.dll"); return -2; }

    wchar_t hostfxr_path[MAX_PATH];
    size_t hostfxr_len = MAX_PATH;
    if (get_hostfxr_path(hostfxr_path, &hostfxr_len, NULL) != 0) {
        set_err("get_hostfxr_path failed"); return -3;
    }

    // 2) Load hostfxr and resolve the entry points we need.
    HMODULE hostfxr = LoadLibraryW(hostfxr_path);
    if (!hostfxr) { set_err("Could not load hostfxr.dll"); return -4; }

    hostfxr_initialize_for_runtime_config_fn init_fn =
        (hostfxr_initialize_for_runtime_config_fn)GetProcAddress(hostfxr, "hostfxr_initialize_for_runtime_config");
    hostfxr_get_runtime_delegate_fn get_delegate_fn =
        (hostfxr_get_runtime_delegate_fn)GetProcAddress(hostfxr, "hostfxr_get_runtime_delegate");
    g_hostfxr_close =
        (hostfxr_close_fn)GetProcAddress(hostfxr, "hostfxr_close");
    if (!init_fn || !get_delegate_fn || !g_hostfxr_close) {
        set_err("hostfxr entry points missing"); return -5;
    }

    // 3) Initialize the runtime from ManagedBridge.runtimeconfig.json.
    wchar_t runtimeconfig[MAX_PATH];
    _snwprintf(runtimeconfig, MAX_PATH, L"%s\\ManagedBridge.runtimeconfig.json", managedBridgeDir);
    _snwprintf(g_managed_dll, MAX_PATH, L"%s\\ManagedBridge.dll", managedBridgeDir);

    if (init_fn(runtimeconfig, NULL, &g_ctx) != 0 || g_ctx == NULL) {
        set_err("hostfxr_initialize_for_runtime_config failed (bad runtimeconfig or missing runtime)");
        return -6;
    }

    // 4) Get the loader delegate.
    load_assembly_and_get_function_pointer_fn load_fn = NULL;
    if (get_delegate_fn(g_ctx, HDT_LOAD_ASSEMBLY_AND_GET_FUNCTION_POINTER, (void **)&load_fn) != 0
        || load_fn == NULL) {
        set_err("get_runtime_delegate failed"); return -7;
    }

    // 5) Bind every managed export.
    if (bind(load_fn, L"Initialize",   (void **)&g_initialize)) return -8;
    if (bind(load_fn, L"Add",          (void **)&g_add))        return -9;
    if (bind(load_fn, L"Greet",        (void **)&g_greet))      return -10;
    if (bind(load_fn, L"FreeString",   (void **)&g_free))       return -11;
    if (bind(load_fn, L"GetLastError", (void **)&g_last_error)) return -12;

    return 0;
}

// ---------------------------------------------------------------------------
// Thin C wrappers over the managed function pointers.
// ---------------------------------------------------------------------------
QTCLR_API int qtclr_init_target(const char *thirdPartyDllPathUtf8) {
    if (!g_initialize) { set_err("qtclr_start not called"); return -1; }
    return g_initialize(thirdPartyDllPathUtf8);
}

QTCLR_API int qtclr_add(int a, int b) {
    if (!g_add) { set_err("bridge not started"); return 0; }
    return g_add(a, b);
}

QTCLR_API char *qtclr_greet(const char *nameUtf8) {
    if (!g_greet) { set_err("bridge not started"); return NULL; }
    return (char *)g_greet(nameUtf8);
}

QTCLR_API void qtclr_free(char *s) {
    if (g_free && s) g_free(s);
}

QTCLR_API const char *qtclr_last_error(void) {
    // Prefer the richer managed message; copy it into the static buffer so the
    // caller never has to reason about which heap owns the string.
    if (g_last_error) {
        char *m = (char *)g_last_error();
        if (m) {
            if (m[0]) set_err(m);
            g_free(m); // managed string -> managed free
        }
    }
    return g_native_error; // static buffer; do NOT free
}

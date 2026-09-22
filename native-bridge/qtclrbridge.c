// ---------------------------------------------------------------------------
// qtclrbridge — native C bridge implementation (MinGW/GCC friendly).
//
// It boots the .NET *Framework* 4.x runtime (the newest CLR available on
// Windows 7 SP1) and reaches the ManagedBridge dispatcher entirely through
// runtime dynamic loading of mscoree.dll:
//
//     mscoree.dll ── CLRCreateInstance() ──► ICLRMetaHost
//         ICLRMetaHost::GetRuntime("v4.0.30319")   ──► ICLRRuntimeInfo
//         ICLRRuntimeInfo::GetInterface(CLRRuntimeHost) ──► ICLRRuntimeHost
//         ICLRRuntimeHost::Start()
//         ICLRRuntimeHost::ExecuteInDefaultAppDomain(Bridge.Bootstrap, &registrar)
//
// ExecuteInDefaultAppDomain can only call `static int Method(string)`, so we use
// that one call to hand the managed side the address of native_register() below.
// Bootstrap then calls native_register() once per dispatcher method, giving us a
// raw cdecl function pointer for each. No [UnmanagedCallersOnly] (which does not
// exist in .NET Framework) and no export-by-name lookup are involved.
//
// No import libraries and no .NET SDK headers are needed to compile this: the
// COM hosting interfaces we use are declared inline below. At run time you only
// need the .NET Framework 4.8 runtime, which ships with / installs on Win 7 SP1
// and provides mscoree.dll.
// ---------------------------------------------------------------------------

#define QTCLRBRIDGE_BUILD
#include "qtclrbridge.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Minimal inline copies of the mscoree / metahost COM contracts.
// (Normally from mscoree.h / metahost.h in the Windows SDK.) Only the methods
// we actually call are typed; earlier vtable slots are kept as void* so the
// layout/offsets stay correct.
// ---------------------------------------------------------------------------

static const GUID CLSID_CLRMetaHost =
    {0x9280188D,0x0E8E,0x4867,{0xB3,0x0C,0x7F,0xA8,0x38,0x84,0xE8,0xDE}};
static const GUID IID_ICLRMetaHost =
    {0xD332DB9E,0xB9B3,0x4125,{0x82,0x07,0xA1,0x48,0x84,0xF5,0x32,0x16}};
static const GUID IID_ICLRRuntimeInfo =
    {0xBD39D1D2,0xBA2F,0x486A,{0x89,0xB0,0xB4,0xB0,0xCB,0x46,0x68,0x91}};
static const GUID CLSID_CLRRuntimeHost =
    {0x90F1A06E,0x7712,0x4762,{0x86,0xB5,0x7A,0x5E,0xBA,0x6B,0xDB,0x02}};
static const GUID IID_ICLRRuntimeHost =
    {0x90F1A06C,0x7712,0x4762,{0x86,0xB5,0x7A,0x5E,0xBA,0x6B,0xDB,0x02}};

typedef struct ICLRMetaHost    ICLRMetaHost;
typedef struct ICLRRuntimeInfo ICLRRuntimeInfo;
typedef struct ICLRRuntimeHost ICLRRuntimeHost;

// ICLRMetaHost: we need GetRuntime (vtable slot 3).
typedef struct ICLRMetaHostVtbl {
    HRESULT (__stdcall *QueryInterface)(ICLRMetaHost*, const GUID*, void**);
    ULONG   (__stdcall *AddRef)(ICLRMetaHost*);
    ULONG   (__stdcall *Release)(ICLRMetaHost*);
    HRESULT (__stdcall *GetRuntime)(ICLRMetaHost*, LPCWSTR, const GUID*, void**);
    void *GetVersionFromFile;
    void *EnumerateInstalledRuntimes;
    void *EnumerateLoadedRuntimes;
    void *RequestRuntimeLoadedNotification;
    void *QueryLegacyV2RuntimeBinding;
    void *ExitProcess;
} ICLRMetaHostVtbl;
struct ICLRMetaHost { ICLRMetaHostVtbl *lpVtbl; };

// ICLRRuntimeInfo: we need GetInterface (vtable slot 9).
typedef struct ICLRRuntimeInfoVtbl {
    HRESULT (__stdcall *QueryInterface)(ICLRRuntimeInfo*, const GUID*, void**);
    ULONG   (__stdcall *AddRef)(ICLRRuntimeInfo*);
    ULONG   (__stdcall *Release)(ICLRRuntimeInfo*);
    void *GetVersionString;
    void *GetRuntimeDirectory;
    void *IsLoaded;
    void *LoadErrorString;
    void *LoadLibrarySlot;    // real name LoadLibrary; renamed to dodge the Win32 macro
    void *GetProcAddressSlot; // real name GetProcAddress; renamed for symmetry
    HRESULT (__stdcall *GetInterface)(ICLRRuntimeInfo*, const GUID*, const GUID*, void**);
    void *IsLoadable;
    void *SetDefaultStartupFlags;
    void *GetDefaultStartupFlags;
    void *BindAsLegacyV2Runtime;
    void *IsStarted;
} ICLRRuntimeInfoVtbl;
struct ICLRRuntimeInfo { ICLRRuntimeInfoVtbl *lpVtbl; };

// ICLRRuntimeHost: we need Start (slot 3) and ExecuteInDefaultAppDomain (slot 11).
typedef struct ICLRRuntimeHostVtbl {
    HRESULT (__stdcall *QueryInterface)(ICLRRuntimeHost*, const GUID*, void**);
    ULONG   (__stdcall *AddRef)(ICLRRuntimeHost*);
    ULONG   (__stdcall *Release)(ICLRRuntimeHost*);
    HRESULT (__stdcall *Start)(ICLRRuntimeHost*);
    void *Stop;
    void *SetHostControl;
    void *GetCLRControl;
    void *UnloadAppDomain;
    void *ExecuteInAppDomain;
    void *GetCurrentAppDomainId;
    void *ExecuteApplication;
    HRESULT (__stdcall *ExecuteInDefaultAppDomain)(
        ICLRRuntimeHost*, LPCWSTR pwzAssemblyPath, LPCWSTR pwzTypeName,
        LPCWSTR pwzMethodName, LPCWSTR pwzArgument, DWORD *pReturnValue);
} ICLRRuntimeHostVtbl;
struct ICLRRuntimeHost { ICLRRuntimeHostVtbl *lpVtbl; };

typedef HRESULT (__stdcall *CLRCreateInstance_fn)(const GUID*, const GUID*, void**);

// ---------------------------------------------------------------------------
// Managed method signatures (must match the delegate types in Bridge.cs).
// ---------------------------------------------------------------------------
typedef int   (__cdecl *managed_initialize_fn)(const char *assemblyPathUtf8);
typedef int   (__cdecl *managed_add_fn)(int a, int b);
typedef void *(__cdecl *managed_greet_fn)(const char *nameUtf8);   // returns char*
typedef void  (__cdecl *managed_free_fn)(void *p);
typedef void *(__cdecl *managed_last_error_fn)(void);              // returns char*

// Slot ids shared with Bridge.cs (keep in sync with the SLOT_* consts there).
#define SLOT_INITIALIZE    0
#define SLOT_ADD           1
#define SLOT_GREET         2
#define SLOT_FREESTRING    3
#define SLOT_GETLASTERROR  4

// ---------------------------------------------------------------------------
// Bridge state.
// ---------------------------------------------------------------------------
static ICLRRuntimeHost      *g_host = NULL;      // kept alive for the process

static managed_initialize_fn g_initialize = NULL;
static managed_add_fn        g_add        = NULL;
static managed_greet_fn      g_greet      = NULL;
static managed_free_fn       g_free       = NULL;
static managed_last_error_fn g_last_error = NULL;

static wchar_t g_managed_dll[MAX_PATH] = {0};
static char    g_native_error[512]     = {0};

static void set_err(const char *msg) {
    strncpy(g_native_error, msg, sizeof(g_native_error) - 1);
    g_native_error[sizeof(g_native_error) - 1] = '\0';
}

static void set_err_hr(const char *msg, HRESULT hr) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s (hr=0x%08lx)", msg, (unsigned long)hr);
    set_err(buf);
}

// ---------------------------------------------------------------------------
// native_register — called BY managed code (Bootstrap) to hand us one cdecl
// function pointer per dispatcher method. Its address is passed to Bootstrap as
// a decimal string; managed marshals it back to a delegate and invokes it.
// ---------------------------------------------------------------------------
static void __cdecl native_register(int slot, void *fn) {
    switch (slot) {
        case SLOT_INITIALIZE:   g_initialize = (managed_initialize_fn)fn; break;
        case SLOT_ADD:          g_add        = (managed_add_fn)fn;        break;
        case SLOT_GREET:        g_greet      = (managed_greet_fn)fn;      break;
        case SLOT_FREESTRING:   g_free       = (managed_free_fn)fn;       break;
        case SLOT_GETLASTERROR: g_last_error = (managed_last_error_fn)fn; break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// qtclr_start
// ---------------------------------------------------------------------------
QTCLR_API int qtclr_start(const wchar_t *managedBridgeDir) {
    if (g_initialize) return 0; // already started

    // 1) mscoree.dll ships with the .NET Framework (present on Win 7 SP1).
    HMODULE mscoree = LoadLibraryW(L"mscoree.dll");
    if (!mscoree) { set_err("mscoree.dll not found. Install the .NET Framework 4.8 runtime."); return -1; }

    CLRCreateInstance_fn CLRCreateInstance =
        (CLRCreateInstance_fn)GetProcAddress(mscoree, "CLRCreateInstance");
    if (!CLRCreateInstance) { set_err("CLRCreateInstance missing in mscoree.dll"); return -2; }

    // 2) ICLRMetaHost.
    ICLRMetaHost *metahost = NULL;
    HRESULT hr = CLRCreateInstance(&CLSID_CLRMetaHost, &IID_ICLRMetaHost, (void **)&metahost);
    if (FAILED(hr) || !metahost) { set_err_hr("CLRCreateInstance failed", hr); return -3; }

    // 3) The v4 runtime (covers .NET Framework 4.0 .. 4.8).
    ICLRRuntimeInfo *rtinfo = NULL;
    hr = metahost->lpVtbl->GetRuntime(metahost, L"v4.0.30319", &IID_ICLRRuntimeInfo, (void **)&rtinfo);
    if (FAILED(hr) || !rtinfo) {
        set_err_hr("ICLRMetaHost::GetRuntime(v4.0.30319) failed", hr);
        metahost->lpVtbl->Release(metahost);
        return -4;
    }

    // 4) ICLRRuntimeHost.
    ICLRRuntimeHost *host = NULL;
    hr = rtinfo->lpVtbl->GetInterface(rtinfo, &CLSID_CLRRuntimeHost, &IID_ICLRRuntimeHost, (void **)&host);
    if (FAILED(hr) || !host) {
        set_err_hr("ICLRRuntimeInfo::GetInterface(CLRRuntimeHost) failed", hr);
        rtinfo->lpVtbl->Release(rtinfo);
        metahost->lpVtbl->Release(metahost);
        return -5;
    }

    // 5) Start the CLR.
    hr = host->lpVtbl->Start(host);
    if (FAILED(hr)) {
        set_err_hr("ICLRRuntimeHost::Start failed", hr);
        host->lpVtbl->Release(host);
        rtinfo->lpVtbl->Release(rtinfo);
        metahost->lpVtbl->Release(metahost);
        return -6;
    }
    g_host = host; // keep the host alive; do NOT release it

    // 6) Call Bridge.Bootstrap, passing the address of native_register as text.
    _snwprintf(g_managed_dll, MAX_PATH, L"%s\\ManagedBridge.dll", managedBridgeDir);

    wchar_t arg[32];
    _ui64tow((unsigned __int64)(uintptr_t)&native_register, arg, 10);

    DWORD ret = 0;
    hr = host->lpVtbl->ExecuteInDefaultAppDomain(host, g_managed_dll,
             L"ManagedBridge.Bridge", L"Bootstrap", arg, &ret);

    // The infos are no longer needed once Bootstrap has run.
    rtinfo->lpVtbl->Release(rtinfo);
    metahost->lpVtbl->Release(metahost);

    if (FAILED(hr)) { set_err_hr("ExecuteInDefaultAppDomain(Bootstrap) failed (bad path or missing ManagedBridge.dll?)", hr); return -7; }
    if (ret != 0)   { set_err("Managed Bootstrap returned failure"); return -8; }

    if (!g_initialize || !g_add || !g_greet || !g_free || !g_last_error) {
        set_err("Bootstrap did not register all methods"); return -9;
    }
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

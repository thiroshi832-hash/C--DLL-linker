// ---------------------------------------------------------------------------
// qtclrbridge — public C ABI consumed by the Qt (MinGW) application.
//
// Load this DLL with QLibrary and resolve the symbols below, or link against it
// directly. Every function is plain C with a stable ABI, so MinGW/GCC has no
// trouble with it even though a .NET runtime lives behind the scenes.
// ---------------------------------------------------------------------------
#ifndef QTCLRBRIDGE_H
#define QTCLRBRIDGE_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#  ifdef QTCLRBRIDGE_BUILD
#    define QTCLR_API __declspec(dllexport)
#  else
#    define QTCLR_API __declspec(dllimport)
#  endif
#else
#  define QTCLR_API
#endif

// Boot the .NET runtime and load ManagedBridge.dll.
//   managedBridgeDir : folder containing ManagedBridge.dll +
//                      ManagedBridge.runtimeconfig.json (wide/UTF-16 path).
// Returns 0 on success, negative on failure.
QTCLR_API int qtclr_start(const wchar_t *managedBridgeDir);

// Load your fixed C# DLL and bind its methods (calls ManagedBridge.Initialize).
//   thirdPartyDllPathUtf8 : path to the fixed DLL, UTF-8 encoded.
// Returns 0 on success, negative on failure (see qtclr_last_error).
QTCLR_API int qtclr_init_target(const char *thirdPartyDllPathUtf8);

// --- one wrapper per method you call; mirror these for your real DLL --------
QTCLR_API int   qtclr_add(int a, int b);
// Returns a heap string owned by the bridge; release it with qtclr_free.
QTCLR_API char *qtclr_greet(const char *nameUtf8);
// ---------------------------------------------------------------------------

// Free a string returned by the bridge (qtclr_greet, qtclr_last_error).
QTCLR_API void qtclr_free(char *s);

// Last error text (UTF-8), managed message preferred. Points to an internal
// static buffer valid until the next bridge call — do NOT free it.
QTCLR_API const char *qtclr_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // QTCLRBRIDGE_H

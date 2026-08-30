# Qt ⇄ C# DLL linker (in-process, MinGW)

Call a **fixed** (non-recompilable) C# DLL from a **MinGW-built Qt** program,
in-process. Because C++/CLI needs MSVC and you cannot add native exports to the
fixed DLL, the only in-process route is to **host the .NET runtime yourself** and
reach the DLL through **reflection**. This repo does exactly that with a plain-C
ABI that MinGW/GCC links against cleanly.

```
Qt app (MinGW/GCC)
   │  QLibrary → plain C ABI (qtclrbridge.h)
   ▼
qtclrbridge.dll     native C; boots the CLR via nethost/hostfxr
   │                (all loaded dynamically — no import libs, no SDK headers)
   │  UnmanagedCallersOnly function pointers
   ▼
ManagedBridge.dll   C# dispatcher YOU compile (allowed — not the fixed DLL)
   │  System.Reflection
   ▼
ThirdParty.dll      your FIXED C# binary — never modified
```

## Layout

| Path | What it is |
|------|-----------|
| `sample-thirdparty-dll/` | A stand-in for your fixed DLL so the demo runs end-to-end. **Delete once you wire in the real one.** |
| `managed-bridge/`  | `ManagedBridge.dll` — the C# dispatcher. **The only C# you edit.** |
| `native-bridge/`   | `qtclrbridge.dll` — native C bridge (MinGW). Hosting typedefs are inlined; no SDK headers needed. |
| `qt-example/`      | A tiny Qt console app using `QLibrary` to drive the bridge. |
| `build.ps1`        | Builds all four pieces into `./dist`. |

## Prerequisites

- **.NET SDK 8.x** (to build the C# parts). *This machine currently has only the
  .NET 8 runtime — install the SDK to build.*
- **CMake ≥ 3.16**
- **MinGW-w64 GCC** — the same kit your Qt was built with
- **Qt 5 or 6 for MinGW**
- At **run time**: the **.NET 8 runtime** (already present here). It ships
  `nethost.dll`, which the bridge uses to find the runtime.

## Build & run

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -QtPrefix "C:\Qt\6.7.0\mingw_64"
```

Then, from a shell where Qt's `bin` is on `PATH` (so the Qt DLLs resolve):

```powershell
.\dist\qt_clr_demo.exe
```

Expected output:

```
Add(2, 40)   = 42
Greet("Qt") = "Hello, Qt! (from ThirdParty.Calculator)"
```

Everything the demo needs sits in `./dist`: `qt_clr_demo.exe`,
`qtclrbridge.dll`, `ManagedBridge.dll`, `ManagedBridge.runtimeconfig.json`, and
`ThirdParty.dll`.

## Swapping in your real DLL

You told me you already know the exact methods to call, so this is mechanical:

1. **Drop your DLL** in `./dist` (or wherever, and pass its path to
   `initTarget`). Remove the `sample-thirdparty-dll` project.
2. **Edit `managed-bridge/Bridge.cs`** — this is the whole job:
   - In `Initialize`, change the type name (`"ThirdParty.Calculator"`), the
     `Activator.CreateInstance` (or use a static class — skip the instance), and
     the `GetMethod` lookups to your real members.
   - For each method, copy the `Add` / `Greet` pattern into a new
     `[UnmanagedCallersOnly]` wrapper. Keep every parameter and return value
     **blittable or a UTF-8 `char*`** (marshal anything richer yourself).
3. **Mirror each new method** in `native-bridge/qtclrbridge.c` (a function-pointer
   typedef + a `bind(...)` call + a thin `qtclr_*` wrapper) and declare it in
   `qtclrbridge.h`.
4. **Expose it in Qt** by adding a resolver + wrapper in
   `qt-example/ClrBridge.{h,cpp}`.

The three native/Qt edits are boilerplate; the real thinking is only in
`Bridge.cs`.

## How the CLR gets hosted (no import libraries)

`qtclrbridge.c` does everything through `LoadLibrary`/`GetProcAddress`, which is
what makes it painless under MinGW:

1. `LoadLibraryW("nethost.dll")` → `get_hostfxr_path()` locates `hostfxr.dll`.
2. `hostfxr_initialize_for_runtime_config("ManagedBridge.runtimeconfig.json")`
   starts the runtime (that JSON is emitted automatically because the csproj sets
   `EnableDynamicLoading`).
3. `hostfxr_get_runtime_delegate(..._load_assembly_and_get_function_pointer)`.
4. For each dispatcher method, that delegate is called with the
   `UNMANAGEDCALLERSONLY_METHOD` sentinel, yielding a raw function pointer with
   your chosen C signature.

No `.lib` files, no `nethost.h`. The only hosting types used are declared inline
at the top of `qtclrbridge.c`.

## Notes, limits, gotchas

- **x64 assumed.** On x64 Windows there is a single calling convention, so the
  `CallConvCdecl` annotations are effectively free. For x86 you'd need to keep
  cdecl consistent on both sides.
- **String ownership.** `qtclr_greet` returns a bridge-owned string; release it
  with `qtclr_free`. `qtclr_last_error` returns an internal static buffer — do
  **not** free it.
- **Errors.** Any managed exception is caught in `Bridge.cs`, stored, and
  surfaced through `qtclr_last_error()`. Check it whenever a call returns a
  failure sentinel.
- **The fixed DLL must be loadable by .NET 8.** If it targets .NET Standard or
  .NET Core/5+, `LoadFromAssemblyPath` just works. If it is a genuine **.NET
  Framework 4.x** assembly that touches Framework-only APIs (WPF, WCF, remoting,
  AppDomains…), a .NET 8 host may not load it — see below.

## If your fixed DLL is .NET Framework 4.x only

You picked "all runtimes", so to be straight with you: a single in-process host
cannot cover both worlds at once. This repo hosts **.NET (Core/5+/8)**, which
loads the large majority of modern and .NET Standard DLLs. A DLL that is
Framework-only needs a **.NET Framework** host instead, via `mscoree.dll`
(`CLRCreateInstance` → `ICLRRuntimeHost::ExecuteInDefaultAppDomain`), with the
same dispatcher idea compiled against .NET Framework. That is a separate host and
a fair bit more code. If you confirm the DLL is Framework-only, say so and I'll
build that variant; if it targets .NET Standard/Core/5+, this repo already covers
you.

# Qt ⇄ C# DLL linker (in-process, MinGW, Windows 7 compatible)

Call a **fixed** (non-recompilable) C# DLL from a **MinGW-built Qt** program,
in-process, on **Windows 7 SP1** and up. Because C++/CLI needs MSVC and you
cannot add native exports to the fixed DLL, the only in-process route is to
**host the .NET runtime yourself** and reach the DLL through **reflection**.
This repo does exactly that with a plain-C ABI that MinGW/GCC links against
cleanly.

```
Qt app (MinGW/GCC)
   │  QLibrary → plain C ABI (qtclrbridge.h)
   ▼
qtclrbridge.dll     native C; boots the .NET Framework CLR via mscoree.dll
   │                (loaded dynamically — no import libs, no SDK headers)
   │  cdecl function pointers (handed over by Bootstrap)
   ▼
ManagedBridge.dll   C# dispatcher YOU compile (allowed — not the fixed DLL)
   │  System.Reflection
   ▼
ThirdParty.dll      your FIXED C# binary — never modified
```

## Why .NET Framework 4.8 (this is the Windows 7 part)

.NET (Core) 6/7/8 **do not run on Windows 7** — their minimum is Windows 10
1607 / Server 2012. The newest CLR that installs and runs on **Windows 7 SP1**
is **.NET Framework 4.8**. So the runtime is hosted through the classic
**`mscoree.dll`** COM API (`CLRCreateInstance` → `ICLRRuntimeHost`), not the
.NET Core `nethost`/`hostfxr` path.

A 4.8 host loads fixed DLLs that target **.NET Framework 4.x** or **.NET
Standard 2.0**. A DLL that is genuinely **.NET Core/5+ only** cannot run on
Windows 7 at all, regardless of host — that is a limitation of Windows 7, not of
this bridge.

## Layout

| Path | What it is |
|------|-----------|
| `sample-thirdparty-dll/` | A stand-in for your fixed DLL so the demo runs end-to-end (net48). **Delete once you wire in the real one.** |
| `managed-bridge/`  | `ManagedBridge.dll` — the C# dispatcher (net48). **The only C# you edit.** |
| `native-bridge/`   | `qtclrbridge.dll` — native C bridge (MinGW). CLR hosting COM interfaces are inlined; no SDK headers needed. |
| `qt-example/`      | A tiny Qt console app using `QLibrary` to drive the bridge. |
| `build.ps1`        | Builds all four pieces into `./dist`. |

## Prerequisites

Build these on a normal Windows 10/11 dev box, then copy `./dist` to the Win 7
machine.

**Build machine:**

- **.NET Framework 4.8 targeting pack** (a.k.a. the 4.8 Developer Pack) so the
  C# parts compile to net48. With that installed, `dotnet build` handles the
  `net48` projects; MSBuild from Visual Studio / Build Tools works too.
- **CMake ≥ 3.16**
- **MinGW-w64 GCC** — the same kit your Qt was built with
- **Qt 5 or 6 for MinGW**

**Windows 7 SP1 target (run time):**

- **.NET Framework 4.8 runtime** — installs on Win 7 SP1 (ships `mscoree.dll`,
  which the bridge uses to boot the CLR).
- The **Qt runtime DLLs** (`Qt6Core.dll` / `Qt5Core.dll`, platform plugins if
  you later use GUI) and the **MinGW runtime DLLs** (`libgcc_s_seh-1.dll` or
  `libgcc_s_dw2-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`) deployed next
  to `qt_clr_demo.exe`. Copy them from your Qt `bin` folder. No MSVC redist is
  needed — MinGW does not use it.

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
`qtclrbridge.dll`, `ManagedBridge.dll`, and `ThirdParty.dll`. (No
`runtimeconfig.json` — that is a .NET Core artifact and does not apply to the
Framework host.)

## Swapping in your real DLL

You told me you already know the exact methods to call, so this is mechanical:

1. **Drop your DLL** in `./dist` (or wherever, and pass its path to
   `initTarget`). Remove the `sample-thirdparty-dll` project.
2. **Edit `managed-bridge/Bridge.cs`** — this is the whole job:
   - In `Initialize`, change the type name (`"ThirdParty.Calculator"`), the
     `Activator.CreateInstance` (or use a static class — skip the instance), and
     the `GetMethod` lookups to your real members.
   - For each method, add: a `[UnmanagedFunctionPointer(Cdecl)]` **delegate
     type**, a static keep-alive **field**, a static **method** (copy the `Add`
     / `Greet` pattern), and one **`register(SLOT_x, ...)`** call in `Bootstrap`.
     Keep every parameter and return value **blittable or a UTF-8 `char*`**
     (marshal anything richer yourself). The keep-alive field is not optional —
     if the delegate is collected, the native pointer dangles.
3. **Mirror each new method** in `native-bridge/qtclrbridge.c`: add a matching
   `SLOT_x` define, a `managed_*_fn` typedef + `g_*` global, a `case` in
   `native_register`, and a thin `qtclr_*` wrapper; declare it in
   `qtclrbridge.h`.
4. **Expose it in Qt** by adding a resolver + wrapper in
   `qt-example/ClrBridge.{h,cpp}`.

The slot bookkeeping is boilerplate; the real thinking is only in `Bridge.cs`.

## How the CLR gets hosted (no import libraries)

`qtclrbridge.c` does everything through `LoadLibrary`/`GetProcAddress` plus
hand-declared COM vtables, which is what makes it painless under MinGW:

1. `LoadLibraryW("mscoree.dll")` → `CLRCreateInstance` → **`ICLRMetaHost`**.
2. `ICLRMetaHost::GetRuntime("v4.0.30319")` → **`ICLRRuntimeInfo`** (the v4
   runtime, covering .NET Framework 4.0–4.8).
3. `ICLRRuntimeInfo::GetInterface(CLRRuntimeHost)` → **`ICLRRuntimeHost`**;
   `Start()`.
4. `ICLRRuntimeHost::ExecuteInDefaultAppDomain(...)` invokes
   `ManagedBridge.Bridge.Bootstrap`, the one method the host can call directly
   (its signature is fixed to `static int Method(string)`).

`Bootstrap` receives the address of the native `native_register` function as a
string, then hands back one **cdecl function pointer per dispatcher method**
(via `Marshal.GetFunctionPointerForDelegate`). That replaces .NET Core's
`[UnmanagedCallersOnly]`, which does not exist in .NET Framework.

No `.lib` files, no `mscoree.h`. The only hosting types used are declared inline
at the top of `qtclrbridge.c`.

## Notes, limits, gotchas

- **Bitness must match.** The Qt exe, `qtclrbridge.dll`, and the CLR it boots
  must all be x86 or all x64 — set by your MinGW/Qt kit. `ManagedBridge` and the
  fixed DLL should be AnyCPU (or match). On x86, everything crossing the
  boundary is consistently `cdecl`, so it stays correct there too.
- **String ownership.** `qtclr_greet` returns a bridge-owned string; release it
  with `qtclr_free`. `qtclr_last_error` returns an internal static buffer — do
  **not** free it.
- **Errors.** Any managed exception is caught in `Bridge.cs`, stored, and
  surfaced through `qtclr_last_error()`. Check it whenever a call returns a
  failure sentinel.
- **Delegate lifetime.** The static `_*Del` fields in `Bridge.cs` keep the
  callbacks alive for the process. Do not remove them.
- **The fixed DLL must load under .NET Framework 4.8.** .NET Framework 4.x and
  .NET Standard 2.0 assemblies load directly. A .NET Core/5+ only assembly does
  not — and could not run on Windows 7 anyway.

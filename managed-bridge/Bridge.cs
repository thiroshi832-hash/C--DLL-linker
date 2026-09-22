// ---------------------------------------------------------------------------
// ManagedBridge — the C# dispatcher (.NET Framework 4.8, for Windows 7 SP1).
//
// This assembly IS allowed to be recompiled (it is ours, not the fixed
// third-party DLL). Its job:
//   1. Load the fixed C# DLL at runtime (Assembly.LoadFrom, no recompile).
//   2. Cache the Type / MethodInfo of the members you told me you already know.
//   3. Expose each one to the native bridge (qtclrbridge.dll) as a raw function
//      pointer.
//
// .NET Framework has no [UnmanagedCallersOnly], so instead of the host pulling
// function pointers out by name (the .NET Core way), the runtime is booted with
// ICLRRuntimeHost::ExecuteInDefaultAppDomain, which can only call one method:
//
//     public static int Bootstrap(string argument)
//
// We use that single call to hand our function pointers to native. The native
// bridge passes us the address of its own "registrar" function as the argument;
// Bootstrap turns each managed method into a cdecl delegate and registers it.
//
// Everything crossing the native boundary is blittable or a UTF-8 char*.
// Strings we return are allocated with Marshal and MUST be released by the
// caller via FreeString.
// ---------------------------------------------------------------------------

using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace ManagedBridge
{
    public static class Bridge
    {
        // --- delegate types crossing the native boundary (cdecl) -----------
        // These mirror the managed_*_fn typedefs in qtclrbridge.c exactly.

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void RegisterDelegate(int slot, IntPtr fn);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int InitializeDelegate(IntPtr assemblyPathUtf8);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int AddDelegate(int a, int b);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr GreetDelegate(IntPtr nameUtf8);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void FreeStringDelegate(IntPtr p);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr GetLastErrorDelegate();

        // Keep the delegate instances alive for the life of the process. If they
        // are collected the native function pointers dangle. THIS IS REQUIRED.
        private static InitializeDelegate _initDel;
        private static AddDelegate _addDel;
        private static GreetDelegate _greetDel;
        private static FreeStringDelegate _freeDel;
        private static GetLastErrorDelegate _lastErrDel;

        // Slot ids shared with native (must match the SLOT_* defines in the C).
        private const int SLOT_INITIALIZE   = 0;
        private const int SLOT_ADD          = 1;
        private const int SLOT_GREET        = 2;
        private const int SLOT_FREESTRING   = 3;
        private const int SLOT_GETLASTERROR = 4;

        // --- cached reflection handles for the fixed DLL --------------------
        private static Assembly _target;
        private static object _calculator;      // instance of ThirdParty.Calculator
        private static MethodInfo _add;
        private static MethodInfo _greet;

        // Last error message, retrievable from native side for diagnostics.
        private static string _lastError = "";

        // -------------------------------------------------------------------
        // Bootstrap(argument): the ONLY entry point the CLR host calls directly
        // (via ICLRRuntimeHost::ExecuteInDefaultAppDomain). Its signature is
        // fixed by the host API: `static int Method(string)`.
        //
        // `argument` is the decimal address of the native registrar function.
        // We build a delegate for each managed method and register its function
        // pointer back with native. Returns 0 on success.
        // -------------------------------------------------------------------
        public static int Bootstrap(string argument)
        {
            try
            {
                IntPtr registrar = new IntPtr(unchecked((long)ulong.Parse(argument)));
                var register = (RegisterDelegate)Marshal.GetDelegateForFunctionPointer(
                    registrar, typeof(RegisterDelegate));

                _initDel    = Initialize;
                _addDel     = Add;
                _greetDel   = Greet;
                _freeDel    = FreeString;
                _lastErrDel = GetLastError;

                register(SLOT_INITIALIZE,   Marshal.GetFunctionPointerForDelegate(_initDel));
                register(SLOT_ADD,          Marshal.GetFunctionPointerForDelegate(_addDel));
                register(SLOT_GREET,        Marshal.GetFunctionPointerForDelegate(_greetDel));
                register(SLOT_FREESTRING,   Marshal.GetFunctionPointerForDelegate(_freeDel));
                register(SLOT_GETLASTERROR, Marshal.GetFunctionPointerForDelegate(_lastErrDel));

                _lastError = "";
                return 0;
            }
            catch (Exception ex)
            {
                _lastError = ex.ToString();
                return -1;
            }
        }

        // -------------------------------------------------------------------
        // Initialize(assemblyPathUtf8): load the fixed DLL and bind methods.
        // Returns 0 on success, negative on failure (call GetLastError for text).
        //
        // >>> THIS IS THE ONE METHOD YOU EDIT when swapping in your real DLL. <<<
        // Change the type name, the method names, and their argument types to
        // match the members you already know you need to call.
        // -------------------------------------------------------------------
        public static int Initialize(IntPtr assemblyPathUtf8)
        {
            try
            {
                string path = Utf8ToString(assemblyPathUtf8);
                path = System.IO.Path.GetFullPath(path);

                // Load the fixed DLL without recompiling it. LoadFrom also
                // resolves dependencies sitting next to it.
                _target = Assembly.LoadFrom(path);

                // ---- adapt this block to your real DLL -----------------
                Type calcType = _target.GetType("ThirdParty.Calculator", throwOnError: true);
                _calculator = Activator.CreateInstance(calcType);

                _add = calcType.GetMethod("Add", new[] { typeof(int), typeof(int) })
                       ?? throw new MissingMethodException("ThirdParty.Calculator.Add(int,int)");
                _greet = calcType.GetMethod("Greet", new[] { typeof(string) })
                       ?? throw new MissingMethodException("ThirdParty.Calculator.Greet(string)");
                // --------------------------------------------------------

                _lastError = "";
                return 0;
            }
            catch (Exception ex)
            {
                _lastError = ex.ToString();
                return -1;
            }
        }

        // -------------------------------------------------------------------
        // One managed method + one delegate type per member you call. Follow
        // this pattern for each member of your real DLL (add a delegate type
        // above, a static keep-alive field, and a register() call in Bootstrap).
        // -------------------------------------------------------------------

        public static int Add(int a, int b)
        {
            try
            {
                return (int)_add.Invoke(_calculator, new object[] { a, b });
            }
            catch (Exception ex)
            {
                _lastError = ex.ToString();
                return 0;
            }
        }

        // Returns a Marshal-allocated UTF-8 string; native side must FreeString it.
        public static IntPtr Greet(IntPtr nameUtf8)
        {
            try
            {
                string name = Utf8ToString(nameUtf8) ?? "";
                string result = (string)_greet.Invoke(_calculator, new object[] { name });
                return StringToUtf8(result);
            }
            catch (Exception ex)
            {
                _lastError = ex.ToString();
                return IntPtr.Zero;
            }
        }

        // -------------------------------------------------------------------
        // Housekeeping.
        // -------------------------------------------------------------------

        public static void FreeString(IntPtr p)
        {
            if (p != IntPtr.Zero)
                Marshal.FreeHGlobal(p);
        }

        public static IntPtr GetLastError()
        {
            return StringToUtf8(_lastError ?? "");
        }

        // -------------------------------------------------------------------
        // UTF-8 marshalling helpers.
        // -------------------------------------------------------------------

        private static string Utf8ToString(IntPtr utf8)
        {
            if (utf8 == IntPtr.Zero) return null;
            int len = 0;
            while (Marshal.ReadByte(utf8, len) != 0) len++;
            if (len == 0) return "";
            byte[] buf = new byte[len];
            Marshal.Copy(utf8, buf, 0, len);
            return Encoding.UTF8.GetString(buf);
        }

        private static IntPtr StringToUtf8(string s)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(s ?? "");
            IntPtr mem = Marshal.AllocHGlobal(bytes.Length + 1);
            Marshal.Copy(bytes, 0, mem, bytes.Length);
            Marshal.WriteByte(mem, bytes.Length, 0); // NUL terminator
            return mem;
        }
    }
}

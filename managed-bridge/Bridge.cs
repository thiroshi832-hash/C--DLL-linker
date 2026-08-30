// ---------------------------------------------------------------------------
// ManagedBridge — the C# dispatcher.
//
// This assembly IS allowed to be recompiled (it is ours, not the fixed
// third-party DLL). Its job:
//   1. Load the fixed C# DLL at runtime (AssemblyLoadContext, no recompile).
//   2. Cache the Type / MethodInfo of the members you told me you already know.
//   3. Expose each one as an [UnmanagedCallersOnly] function that the native
//      bridge (qtclrbridge.dll) can call through a raw function pointer.
//
// Everything crossing the native boundary is blittable or a UTF-8 char*.
// Strings we return are allocated with Marshal and MUST be released by the
// caller via FreeString.
//
// x64 is assumed (single calling convention). The CallConvCdecl annotation is
// harmless there and keeps the intent explicit.
// ---------------------------------------------------------------------------

using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace ManagedBridge
{
    public static class Bridge
    {
        // --- cached reflection handles for the fixed DLL --------------------
        private static Assembly _target;
        private static object _calculator;      // instance of ThirdParty.Calculator
        private static MethodInfo _add;
        private static MethodInfo _greet;

        // Last error message, retrievable from native side for diagnostics.
        private static string _lastError = "";

        // -------------------------------------------------------------------
        // Initialize(assemblyPathUtf8): load the fixed DLL and bind methods.
        // Returns 0 on success, negative on failure (call GetLastError for text).
        //
        // >>> THIS IS THE ONE METHOD YOU EDIT when swapping in your real DLL. <<<
        // Change the type name, the method names, and their argument types to
        // match the members you already know you need to call.
        // -------------------------------------------------------------------
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Initialize(IntPtr assemblyPathUtf8)
        {
            try
            {
                string path = Utf8ToString(assemblyPathUtf8);
                path = System.IO.Path.GetFullPath(path);

                // Load the fixed DLL without recompiling it.
                _target = AssemblyLoadContext.Default.LoadFromAssemblyPath(path);

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
        // One [UnmanagedCallersOnly] wrapper per method you call. Follow this
        // pattern for each member of your real DLL.
        // -------------------------------------------------------------------

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
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
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
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
        // Housekeeping exports.
        // -------------------------------------------------------------------

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void FreeString(IntPtr p)
        {
            if (p != IntPtr.Zero)
                Marshal.FreeHGlobal(p);
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
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
            unsafe
            {
                byte* p = (byte*)utf8;
                while (p[len] != 0) len++;
                return Encoding.UTF8.GetString(p, len);
            }
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

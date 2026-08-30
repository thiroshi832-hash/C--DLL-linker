// ---------------------------------------------------------------------------
// SAMPLE "third-party" C# library.
//
// This stands in for the FIXED C# DLL you cannot recompile. It is only here so
// the whole bridge can be built and tested end-to-end. Delete this project and
// point the bridge at your real DLL once the demo works (see README, section
// "Swapping in your real DLL").
//
// The bridge calls these members by name via reflection, so the important
// contract is: namespace + type name + method names + signatures.
// ---------------------------------------------------------------------------

using System;

namespace ThirdParty
{
    public class Calculator
    {
        public int Add(int a, int b) => a + b;

        public string Greet(string name) => $"Hello, {name}! (from ThirdParty.Calculator)";
    }
}

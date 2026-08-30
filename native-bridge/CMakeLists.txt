cmake_minimum_required(VERSION 3.16)
project(qtclrbridge C)

# Native C bridge DLL. Buildable with MinGW gcc — no import libs, no SDK headers.
add_library(qtclrbridge SHARED qtclrbridge.c)

set_target_properties(qtclrbridge PROPERTIES
    C_STANDARD 11
    PREFIX ""            # produce qtclrbridge.dll, not libqtclrbridge.dll
)

# nethost/hostfxr are loaded at runtime via LoadLibrary — nothing to link.
# kernel32 (LoadLibraryW/GetProcAddress) is linked implicitly on Windows.

install(TARGETS qtclrbridge RUNTIME DESTINATION bin)

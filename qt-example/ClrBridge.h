// ---------------------------------------------------------------------------
// ClrBridge — a small C++/Qt wrapper that loads qtclrbridge.dll with QLibrary
// and resolves its C entry points. Using QLibrary means MinGW needs no import
// library and the DLL is bound purely at runtime.
// ---------------------------------------------------------------------------
#ifndef CLRBRIDGE_H
#define CLRBRIDGE_H

#include <QString>
#include <QLibrary>

class ClrBridge
{
public:
    // bridgeDllPath   : path to qtclrbridge.dll
    // managedBridgeDir: folder holding ManagedBridge.dll
    bool load(const QString &bridgeDllPath, const QString &managedBridgeDir);

    // Load your fixed C# DLL (path to ThirdParty.dll).
    bool initTarget(const QString &thirdPartyDllPath);

    // Wrappers matching the methods you know you need.
    int     add(int a, int b);
    QString greet(const QString &name);

    QString lastError() const;

private:
    QLibrary m_lib;

    // Function pointer typedefs mirroring qtclrbridge.h.
    typedef int         (*start_fn)(const wchar_t *);
    typedef int         (*init_target_fn)(const char *);
    typedef int         (*add_fn)(int, int);
    typedef char *      (*greet_fn)(const char *);
    typedef void        (*free_fn)(char *);
    typedef const char *(*last_error_fn)(void);

    start_fn       m_start       = nullptr;
    init_target_fn m_initTarget  = nullptr;
    add_fn         m_add         = nullptr;
    greet_fn       m_greet       = nullptr;
    free_fn        m_free        = nullptr;
    last_error_fn  m_lastError   = nullptr;
};

#endif // CLRBRIDGE_H

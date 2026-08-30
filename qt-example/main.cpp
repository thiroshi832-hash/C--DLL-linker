// ---------------------------------------------------------------------------
// Demo: call the fixed C# DLL from a MinGW-built Qt program via the bridge.
//
// Run it with the three paths as arguments, or rely on the defaults that assume
// the layout produced by the README build steps (everything under ./dist).
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include "ClrBridge.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Resolve default paths relative to the executable's folder (./dist).
    const QString base = QCoreApplication::applicationDirPath();
    const QString bridgeDll    = base + "/qtclrbridge.dll";
    const QString managedDir   = base;                        // ManagedBridge.* live here
    const QString thirdPartyDll = base + "/ThirdParty.dll";

    ClrBridge bridge;
    if (!bridge.load(bridgeDll, managedDir)) {
        qCritical() << "Failed to start bridge:" << bridge.lastError();
        return 1;
    }
    if (!bridge.initTarget(thirdPartyDll)) {
        qCritical() << "Failed to load fixed DLL:" << bridge.lastError();
        return 2;
    }

    qInfo() << "Add(2, 40)   =" << bridge.add(2, 40);
    qInfo() << "Greet(\"Qt\") =" << bridge.greet("Qt");

    return 0;
}

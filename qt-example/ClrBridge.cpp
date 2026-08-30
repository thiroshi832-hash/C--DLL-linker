#include "ClrBridge.h"

bool ClrBridge::load(const QString &bridgeDllPath, const QString &managedBridgeDir)
{
    m_lib.setFileName(bridgeDllPath);
    if (!m_lib.load())
        return false;

    m_start      = reinterpret_cast<start_fn>(m_lib.resolve("qtclr_start"));
    m_initTarget = reinterpret_cast<init_target_fn>(m_lib.resolve("qtclr_init_target"));
    m_add        = reinterpret_cast<add_fn>(m_lib.resolve("qtclr_add"));
    m_greet      = reinterpret_cast<greet_fn>(m_lib.resolve("qtclr_greet"));
    m_free       = reinterpret_cast<free_fn>(m_lib.resolve("qtclr_free"));
    m_lastError  = reinterpret_cast<last_error_fn>(m_lib.resolve("qtclr_last_error"));

    if (!m_start || !m_initTarget || !m_add || !m_greet || !m_free || !m_lastError)
        return false;

    // Boot the CLR + load ManagedBridge.dll.
    const std::wstring dir = managedBridgeDir.toStdWString();
    return m_start(dir.c_str()) == 0;
}

bool ClrBridge::initTarget(const QString &thirdPartyDllPath)
{
    if (!m_initTarget) return false;
    const QByteArray utf8 = thirdPartyDllPath.toUtf8();
    return m_initTarget(utf8.constData()) == 0;
}

int ClrBridge::add(int a, int b)
{
    return m_add ? m_add(a, b) : 0;
}

QString ClrBridge::greet(const QString &name)
{
    if (!m_greet) return QString();
    const QByteArray utf8 = name.toUtf8();
    char *result = m_greet(utf8.constData());
    if (!result) return QString();
    const QString out = QString::fromUtf8(result);
    m_free(result); // release the bridge-owned string
    return out;
}

QString ClrBridge::lastError() const
{
    if (!m_lastError) return QStringLiteral("bridge not loaded");
    const char *e = m_lastError();
    return e ? QString::fromUtf8(e) : QString();
}

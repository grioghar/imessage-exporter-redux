#include "secret_store.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {
const char* kService = "imessage-exporter";
}

#if defined(Q_OS_MACOS)
#include <QProcess>
namespace secret {

bool store(const QString& key, const QString& value) {
    QProcess p;
    p.start("security",
            {"add-generic-password", "-U", "-s", kService, "-a", key, "-w", value});
    p.waitForFinished(5000);
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QString retrieve(const QString& key) {
    QProcess p;
    p.start("security", {"find-generic-password", "-s", kService, "-a", key, "-w"});
    p.waitForFinished(5000);
    if (p.exitCode() != 0) return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void remove(const QString& key) {
    QProcess p;
    p.start("security", {"delete-generic-password", "-s", kService, "-a", key});
    p.waitForFinished(5000);
}

}  // namespace secret

#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
namespace secret {

static std::wstring target(const QString& key) {
    return (QString(kService) + ":" + key).toStdWString();
}

bool store(const QString& key, const QString& value) {
    std::wstring t = target(key);
    QByteArray blob = value.toUtf8();
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(t.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&cred, 0) == TRUE;
}

QString retrieve(const QString& key) {
    std::wstring t = target(key);
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(t.c_str(), CRED_TYPE_GENERIC, 0, &cred)) return {};
    QString out = QString::fromUtf8(reinterpret_cast<const char*>(cred->CredentialBlob),
                                    static_cast<int>(cred->CredentialBlobSize));
    CredFree(cred);
    return out;
}

void remove(const QString& key) {
    std::wstring t = target(key);
    CredDeleteW(t.c_str(), CRED_TYPE_GENERIC, 0);
}

}  // namespace secret

#else  // Linux/other: 0600 file fallback under the user data dir.
namespace secret {

static QString path(const QString& key) {
    QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                      .filePath("secrets");
    QDir().mkpath(dir);
    return QDir(dir).filePath(key);
}

bool store(const QString& key, const QString& value) {
    QFile f(path(key));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    f.write(value.toUtf8());
    return true;
}

QString retrieve(const QString& key) {
    QFile f(path(key));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

void remove(const QString& key) { QFile::remove(path(key)); }

}  // namespace secret
#endif

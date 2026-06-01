#include "updater.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>

#include <cstdlib>

#include "imsg/version.hpp"

namespace {

constexpr const char* kLatestRelease =
    "https://api.github.com/repos/grioghar/imessage-exporter-redux/releases/latest";

// Path to the running AppImage, or empty when not run from one.
QString appImagePath() {
    const char* p = std::getenv("APPIMAGE");
    return (p && *p) ? QString::fromLocal8Bit(p) : QString();
}

// Parses "1.2.3" (a leading 'v' is tolerated) into comparable integer parts.
QList<int> versionParts(QString v) {
    if (v.startsWith('v') || v.startsWith('V')) v.remove(0, 1);
    QList<int> parts;
    for (const QString& s : v.split('.')) parts << s.section('-', 0, 0).toInt();
    while (parts.size() < 3) parts << 0;
    return parts;
}

bool isNewer(const QString& candidate, const QString& current) {
    const QList<int> a = versionParts(candidate), b = versionParts(current);
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

// The release-asset filename suffix that matches this platform's installer.
const char* assetSuffixForPlatform() {
#if defined(Q_OS_WIN)
    return "Setup.exe";
#elif defined(Q_OS_MACOS)
    return ".dmg";
#else
    return ".AppImage";  // only meaningful when running as an AppImage
#endif
}

}  // namespace

Updater::Updater(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

bool Updater::installSupported() {
#if defined(Q_OS_WIN)
    return true;
#elif defined(Q_OS_MACOS)
    return false;  // unsigned self-replace is unsafe; we open the .dmg instead
#else
    return !appImagePath().isEmpty();  // AppImage yes; deb/rpm/snap no
#endif
}

void Updater::checkForUpdates() {
    QNetworkRequest req((QUrl(kLatestRelease)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "imessage-exporter");
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        const QJsonObject obj =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = obj.value("tag_name").toString();
        if (tag.isEmpty()) {
            emit failed("No release information was returned.");
            return;
        }
        if (!isNewer(tag, IMSG_VERSION)) {
            emit upToDate();
            return;
        }
        const QString suffix = assetSuffixForPlatform();
        for (const QJsonValue& a : obj.value("assets").toArray()) {
            const QJsonObject asset = a.toObject();
            const QString name = asset.value("name").toString();
            if (name.endsWith(suffix, Qt::CaseInsensitive)) {
                emit updateAvailable(tag, obj.value("body").toString(),
                                     QUrl(asset.value("browser_download_url").toString()),
                                     name);
                return;
            }
        }
        emit failed("A newer version (" + tag +
                    ") is available, but no installer for this platform was found.");
    });
}

void Updater::download(const QUrl& asset, const QString& version) {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString dest = QDir(dir).filePath(asset.fileName());

    QNetworkRequest req(asset);
    req.setHeader(QNetworkRequest::UserAgentHeader, "imessage-exporter");
    // GitHub asset URLs redirect to storage; Qt6 follows redirects for GET.
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, dest, version] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit failed("Could not write the downloaded update.");
            return;
        }
        out.write(reply->readAll());
        out.close();
        emit updateDownloaded(dest, version);
    });
}

bool Updater::installAndRestart(const QString& path) {
#if defined(Q_OS_WIN)
    // Inno Setup: install silently and let the Restart Manager relaunch us.
    QProcess::startDetached(
        path, {"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/RESTARTAPPLICATIONS"});
    return true;
#elif defined(Q_OS_MACOS)
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));  // user drags to /Applications
    return false;
#else
    const QString target = appImagePath();
    if (target.isEmpty()) {  // deb/rpm/snap: hand off to the file manager
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return false;
    }
    // Replace the running AppImage via a same-directory rename (the running
    // process keeps its old inode), then re-exec the new one.
    const QString staged = target + ".new";
    QFile::remove(staged);
    if (!QFile::copy(path, staged)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return false;
    }
    QFile::setPermissions(staged, QFile::permissions(staged) | QFileDevice::ExeOwner |
                                      QFileDevice::ExeGroup | QFileDevice::ExeOther);
    QFile::remove(target);
    if (!QFile::rename(staged, target)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return false;
    }
    QProcess::startDetached(target, {});
    return true;
#endif
}

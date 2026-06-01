// Self-update: checks the GitHub Releases API for a newer version, quietly
// downloads the right installer for this platform, and (on confirmation) runs it
// silently and restarts. Install/restart is supported on Windows (the Inno
// installer run with /VERYSILENT) and Linux AppImage (self-replace + re-exec);
// on macOS and distro packages (deb/rpm/snap) it falls back to opening the
// download so the user/package manager finishes the job.
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

class Updater : public QObject {
    Q_OBJECT

   public:
    explicit Updater(QObject* parent = nullptr);

    // True when this build can install an update itself (Windows / AppImage).
    static bool installSupported();

    // Queries the latest release. Emits upToDate / updateAvailable / failed.
    void checkForUpdates();

    // Downloads `asset` to a temp file. Emits updateDownloaded / failed.
    void download(const QUrl& asset, const QString& version);

    // Launches the downloaded installer/package silently. Returns true if the
    // caller should now quit so the installer can restart the app; false means
    // it was only opened for the user to finish (e.g. macOS .dmg).
    bool installAndRestart(const QString& path);

   signals:
    void upToDate();
    void updateAvailable(const QString& version, const QString& notes, QUrl asset,
                         QString assetName);
    void updateDownloaded(const QString& path, const QString& version);
    void failed(const QString& error);

   private:
    QNetworkAccessManager* nam_;
};

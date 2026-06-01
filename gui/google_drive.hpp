// Google Drive connector: OAuth 2.0 (PKCE + loopback) to obtain a Drive
// refresh token, persisted in the OS keychain, plus a synchronous uploader that
// pushes an export directory into a user-named Drive folder. Shares the OAuth
// client (id/secret) with the Contacts connector via google_auth.
//
// Scope: drive.file — the app can only create and manage files it makes itself,
// which is all the export upload needs (least privilege).
#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QNetworkAccessManager;

class GoogleDrive : public QObject {
    Q_OBJECT

   public:
    explicit GoogleDrive(QObject* parent = nullptr);

    static bool isConnected();  // a Drive refresh token is stored

    // For-this-run client override (empty falls back to the stored/env client).
    void setClient(const QString& id, const QString& secret);

    // Browser OAuth; on success stores the refresh token and emits connected().
    void connectInteractive();

   signals:
    void status(const QString& message);
    void connected();
    void failed(const QString& error);

   private:
    void exchangeCode(const QString& code);
    QString clientId();
    QString clientSecret();

    QNetworkAccessManager* nam_;
    QTcpServer* server_ = nullptr;
    QString clientId_;
    QString clientSecret_;
    QString verifier_;
    QString redirectUri_;
};

namespace drive {

struct UploadResult {
    bool ok = false;
    int files = 0;
    QString error;
};

// Synchronous: refresh an access token from the stored Drive refresh token,
// ensure `folderName` exists under "My Drive", then upload everything under
// `localDir` (recreating its subfolder tree). Must be called off the GUI thread
// (it runs its own event loop per request). Reports failure in the result.
UploadResult uploadDirectory(const QString& localDir, const QString& folderName);

}  // namespace drive

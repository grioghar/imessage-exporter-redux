// Connects to Google Contacts (People API) via OAuth 2.0 with PKCE and a
// loopback redirect, downloads the user's connections, and saves handle->name
// rows into the persistent ContactStore. The refresh token is kept in the OS
// keychain (secret_store).
//
// The OAuth client ID/secret come from the environment so the app ships without
// embedded credentials: IMSG_GOOGLE_CLIENT_ID (required) and, for a Google
// "Desktop app" client, IMSG_GOOGLE_CLIENT_SECRET (optional). configured()
// reports whether a client ID is present.
#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QNetworkAccessManager;

class GoogleContacts : public QObject {
    Q_OBJECT

   public:
    explicit GoogleContacts(QObject* parent = nullptr);

    static bool configured();  // a client ID is set in the environment

    // Runs the full flow: browser auth -> token -> download -> save to store.
    void connectAndDownload();

   signals:
    void status(const QString& message);
    void finished(int imported);          // contacts saved to the store
    void failed(const QString& error);

   private:
    void exchangeCode(const QString& code);
    void fetchPage(const QString& pageToken);
    void finish();

    QNetworkAccessManager* nam_;
    QTcpServer* server_ = nullptr;
    QString verifier_;
    QString redirectUri_;
    QString accessToken_;
    int imported_ = 0;
};

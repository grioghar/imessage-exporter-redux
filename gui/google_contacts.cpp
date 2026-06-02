#include "google_contacts.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include "google_auth.hpp"
#include "imsg/contact_store.hpp"
#include "secret_store.hpp"

namespace {
const char* kAuth = "https://accounts.google.com/o/oauth2/v2/auth";
const char* kToken = "https://oauth2.googleapis.com/token";
const char* kScope = "https://www.googleapis.com/auth/contacts.readonly";
const char* kPeople =
    "https://people.googleapis.com/v1/people/me/connections";

QString b64url(const QByteArray& b) {
    return QString::fromLatin1(
        b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
}  // namespace

GoogleContacts::GoogleContacts(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

bool GoogleContacts::configured() { return googleauth::configured(); }

void GoogleContacts::setClient(const QString& id, const QString& secret) {
    clientId_ = id;
    clientSecret_ = secret;
}

// Prefer the for-this-run override entered in the dialog; else the stored/env
// client (so an env-only setup still works).
QString GoogleContacts::clientId() {
    return clientId_.isEmpty() ? googleauth::clientId() : clientId_;
}
QString GoogleContacts::clientSecret() {
    return clientSecret_.isEmpty() ? googleauth::clientSecret() : clientSecret_;
}

void GoogleContacts::connectAndDownload() {
    if (clientId().isEmpty()) {  // includes the for-this-run override
        emit failed(
            "No Google client configured. Enter or import an OAuth client "
            "(Desktop app) in the Connect dialog.");
        return;
    }

    // PKCE verifier/challenge.
    QByteArray raw(32, '\0');
    for (char& c : raw) c = static_cast<char>(QRandomGenerator::global()->bounded(256));
    verifier_ = b64url(raw);
    const QString challenge =
        b64url(QCryptographicHash::hash(verifier_.toUtf8(), QCryptographicHash::Sha256));

    // Loopback redirect server on a random port.
    server_ = new QTcpServer(this);
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        emit failed("Could not open a local port for the OAuth redirect.");
        return;
    }
    redirectUri_ = QString("http://127.0.0.1:%1").arg(server_->serverPort());

    connect(server_, &QTcpServer::newConnection, this, [this] {
        QTcpSocket* sock = server_->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            const QByteArray req = sock->readAll();
            const QString line = QString::fromUtf8(req).section("\r\n", 0, 0);
            QString code;
            const int q = line.indexOf("code=");
            if (q >= 0) code = line.mid(q + 5).section('&', 0, 0).section(' ', 0, 0);
            sock->write(
                "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                "<html><body style='font-family:sans-serif'>Signed in. You can "
                "close this window and return to iMessage Exporter.</body></html>");
            sock->flush();
            sock->disconnectFromHost();
            server_->close();
            if (code.isEmpty())
                emit failed("Google did not return an authorization code.");
            else
                exchangeCode(QUrl::fromPercentEncoding(code.toUtf8()));
        });
    });

    QUrl url(kAuth);
    QUrlQuery qy;
    qy.addQueryItem("client_id", clientId());
    qy.addQueryItem("redirect_uri", redirectUri_);
    qy.addQueryItem("response_type", "code");
    qy.addQueryItem("scope", kScope);
    qy.addQueryItem("code_challenge", challenge);
    qy.addQueryItem("code_challenge_method", "S256");
    qy.addQueryItem("access_type", "offline");
    url.setQuery(qy);

    emit status("Opening your browser to sign in to Google…");
    QDesktopServices::openUrl(url);
}

void GoogleContacts::exchangeCode(const QString& code) {
    emit status("Exchanging the authorization code…");
    QUrlQuery form;
    form.addQueryItem("client_id", clientId());
    if (!clientSecret().isEmpty()) form.addQueryItem("client_secret", clientSecret());
    form.addQueryItem("code", code);
    form.addQueryItem("code_verifier", verifier_);
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("redirect_uri", redirectUri_);

    QNetworkRequest req((QUrl(kToken)));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");
    QNetworkReply* reply =
        nam_->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        accessToken_ = o.value("access_token").toString();
        if (accessToken_.isEmpty()) {
            emit failed("Google token exchange failed: " +
                        o.value("error_description").toString("no access token"));
            return;
        }
        const QString refresh = o.value("refresh_token").toString();
        if (!refresh.isEmpty()) secret::store("google_refresh_token", refresh);
        fetchPage(QString());
    });
}

void GoogleContacts::fetchPage(const QString& pageToken) {
    emit status("Downloading contacts…");
    QUrl url(kPeople);
    QUrlQuery qy;
    qy.addQueryItem("personFields", "names,phoneNumbers,emailAddresses,photos");
    qy.addQueryItem("pageSize", "1000");
    if (!pageToken.isEmpty()) qy.addQueryItem("pageToken", pageToken);
    url.setQuery(qy);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", ("Bearer " + accessToken_).toUtf8());
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed("People API request failed: " + reply->errorString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();

        imsg::ContactStore store(imsg::default_contact_store_path());
        if (!store.open()) {
            emit failed("Could not open the contacts database.");
            return;
        }
        for (const QJsonValue& pv : root.value("connections").toArray()) {
            const QJsonObject person = pv.toObject();
            const QJsonArray names = person.value("names").toArray();
            if (names.isEmpty()) continue;
            const std::string name =
                names.first().toObject().value("displayName").toString().toStdString();
            if (name.empty()) continue;
            // First non-default (non-silhouette) photo URL, if any. Stored as an
            // https URL — it shows when the export is viewed online.
            std::string photo;
            for (const QJsonValue& pv2 : person.value("photos").toArray()) {
                const QJsonObject po = pv2.toObject();
                if (po.value("default").toBool()) continue;  // skip the gray silhouette
                photo = po.value("url").toString().toStdString();
                if (!photo.empty()) break;
            }
            for (const QJsonValue& ph : person.value("phoneNumbers").toArray()) {
                const std::string v = ph.toObject().value("value").toString().toStdString();
                if (!v.empty()) { store.upsert(v, name, "google", photo); ++imported_; }
            }
            for (const QJsonValue& em : person.value("emailAddresses").toArray()) {
                const std::string v = em.toObject().value("value").toString().toStdString();
                if (!v.empty()) { store.upsert(v, name, "google", photo); ++imported_; }
            }
        }
        const QString next = root.value("nextPageToken").toString();
        if (!next.isEmpty())
            fetchPage(next);
        else
            finish();
    });
}

void GoogleContacts::finish() { emit finished(imported_); }

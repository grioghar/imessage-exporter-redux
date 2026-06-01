#include "google_drive.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "google_auth.hpp"
#include "secret_store.hpp"

namespace {
const char* kAuth = "https://accounts.google.com/o/oauth2/v2/auth";
const char* kToken = "https://oauth2.googleapis.com/token";
const char* kScope = "https://www.googleapis.com/auth/drive.file";
const char* kRefreshKey = "google_drive_refresh_token";
const char* kFolderMime = "application/vnd.google-apps.folder";

QString b64url(const QByteArray& b) {
    return QString::fromLatin1(
        b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// Block on a reply with a timeout; returns the body and HTTP status code.
QByteArray sendBlocking(QNetworkReply* reply, int timeoutMs, int* statusOut) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (statusOut)
        *statusOut = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return body;
}
}  // namespace

GoogleDrive::GoogleDrive(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

bool GoogleDrive::isConnected() { return !secret::retrieve(kRefreshKey).isEmpty(); }

void GoogleDrive::setClient(const QString& id, const QString& secret) {
    clientId_ = id;
    clientSecret_ = secret;
}

QString GoogleDrive::clientId() {
    return clientId_.isEmpty() ? googleauth::clientId() : clientId_;
}
QString GoogleDrive::clientSecret() {
    return clientSecret_.isEmpty() ? googleauth::clientSecret() : clientSecret_;
}

void GoogleDrive::connectInteractive() {
    if (clientId().isEmpty()) {
        emit failed(
            "No Google client configured. Enter or import an OAuth client first "
            "(see Help → Google Contacts setup).");
        return;
    }

    QByteArray raw(32, '\0');
    for (char& c : raw) c = static_cast<char>(QRandomGenerator::global()->bounded(256));
    verifier_ = b64url(raw);
    const QString challenge =
        b64url(QCryptographicHash::hash(verifier_.toUtf8(), QCryptographicHash::Sha256));

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
                "<html><body style='font-family:sans-serif'>Google Drive connected. "
                "You can close this window and return to iMessage Exporter.</body></html>");
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
    qy.addQueryItem("prompt", "consent");  // force a refresh token to be returned
    url.setQuery(qy);

    emit status("Opening your browser to authorize Google Drive…");
    QDesktopServices::openUrl(url);
}

void GoogleDrive::exchangeCode(const QString& code) {
    emit status("Finishing Google Drive sign-in…");
    QUrlQuery form;
    form.addQueryItem("client_id", clientId());
    if (!clientSecret().isEmpty()) form.addQueryItem("client_secret", clientSecret());
    form.addQueryItem("code", code);
    form.addQueryItem("code_verifier", verifier_);
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("redirect_uri", redirectUri_);

    QNetworkRequest req((QUrl(kToken)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply* reply = nam_->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.value("access_token").toString().isEmpty()) {
            emit failed("Google token exchange failed: " +
                        o.value("error_description").toString("no access token"));
            return;
        }
        const QString refresh = o.value("refresh_token").toString();
        if (refresh.isEmpty()) {
            emit failed(
                "Google did not return a refresh token. Remove this app's access "
                "at myaccount.google.com → Security and try connecting again.");
            return;
        }
        secret::store(kRefreshKey, refresh);
        // Persist the client too, so background uploads work after a restart.
        googleauth::storeClient(clientId(), clientSecret());
        emit connected();
    });
}

// ---------------------------------------------------------------------------
// Synchronous uploader (runs on a worker thread).
// ---------------------------------------------------------------------------
namespace drive {
namespace {

// Turn a Drive/token API error response into an actionable message.
QString driveError(const QJsonObject& obj, int code, const QString& what) {
    QString msg = obj.value("error").toObject().value("message").toString();
    if (msg.isEmpty()) msg = obj.value("error_description").toString();
    if (msg.isEmpty()) msg = obj.value("error").toString();
    if (msg.isEmpty()) msg = code ? QString("HTTP %1").arg(code) : "no response";
    QString out = "Google Drive could not " + what + ": " + msg + ".";
    if (code == 401 || code == 403 || msg.contains("scope", Qt::CaseInsensitive) ||
        msg.contains("insufficient", Qt::CaseInsensitive) ||
        msg.contains("permission", Qt::CaseInsensitive))
        out += "\n\nFix: in Google Cloud Console → Data access, add the scope "
               "https://www.googleapis.com/auth/drive.file, then click "
               "\"Reconnect Google Drive…\" so the new permission is granted.";
    return out;
}

// Exchange the stored refresh token for a fresh access token. "" + `error` on
// failure.
QString refreshAccessToken(QNetworkAccessManager& nam, QString& error) {
    const QString refresh = secret::retrieve(kRefreshKey);
    if (refresh.isEmpty()) {
        error = "Google Drive isn't connected. Click \"Connect Google Drive…\" first.";
        return QString();
    }
    const QString id = googleauth::clientId();
    if (id.isEmpty()) {
        error = "No Google OAuth client is configured (enter or import one first).";
        return QString();
    }

    QUrlQuery form;
    form.addQueryItem("client_id", id);
    if (!googleauth::clientSecret().isEmpty())
        form.addQueryItem("client_secret", googleauth::clientSecret());
    form.addQueryItem("refresh_token", refresh);
    form.addQueryItem("grant_type", "refresh_token");

    QNetworkRequest req((QUrl(kToken)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    int code = 0;
    const QByteArray body = sendBlocking(
        nam.post(req, form.toString(QUrl::FullyEncoded).toUtf8()), 30000, &code);
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString tok = obj.value("access_token").toString();
    if (tok.isEmpty()) error = driveError(obj, code, "refresh the saved sign-in");
    return tok;
}

QString driveQueryEscape(QString s) { return s.replace('\\', "\\\\").replace('\'', "\\'"); }

// Find a folder named `name` under `parentId`, or create it. "" + `error` on
// failure. A parentId of "root" means the user's My Drive root.
QString findOrCreateFolder(QNetworkAccessManager& nam, const QString& token,
                           const QString& name, const QString& parentId,
                           QString& error) {
    QUrl url("https://www.googleapis.com/drive/v3/files");
    QUrlQuery qy;
    qy.addQueryItem("q", QString("name = '%1' and mimeType = '%2' and trashed = false "
                                 "and '%3' in parents")
                             .arg(driveQueryEscape(name), kFolderMime, parentId));
    qy.addQueryItem("fields", "files(id,name)");
    qy.addQueryItem("spaces", "drive");
    url.setQuery(qy);
    QNetworkRequest greq(url);
    greq.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    int code = 0;
    const QByteArray body = sendBlocking(nam.get(greq), 30000, &code);
    if (code >= 200 && code < 300) {  // only trust a successful listing
        const QJsonArray files =
            QJsonDocument::fromJson(body).object().value("files").toArray();
        if (!files.isEmpty()) return files.first().toObject().value("id").toString();
    }

    // Not found (or listing not permitted under drive.file): create it. Omit
    // `parents` for a root-level folder so we don't depend on the "root" alias.
    QJsonObject meta;
    meta["name"] = name;
    meta["mimeType"] = QString(kFolderMime);
    if (!parentId.isEmpty() && parentId != "root")
        meta["parents"] = QJsonArray{parentId};
    QNetworkRequest creq((QUrl("https://www.googleapis.com/drive/v3/files")));
    creq.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    creq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    code = 0;
    const QByteArray cbody =
        sendBlocking(nam.post(creq, QJsonDocument(meta).toJson(QJsonDocument::Compact)),
                     30000, &code);
    const QJsonObject obj = QJsonDocument::fromJson(cbody).object();
    const QString id = obj.value("id").toString();
    if (id.isEmpty()) error = driveError(obj, code, "create the folder \"" + name + "\"");
    return id;
}

// Multipart upload of one file into `parentId`. Returns true on 2xx.
bool uploadFile(QNetworkAccessManager& nam, const QString& token,
                const QString& localPath, const QString& parentId) {
    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonObject meta;
    meta["name"] = QFileInfo(localPath).fileName();
    meta["parents"] = QJsonArray{parentId};

    const QByteArray boundary = "imsgexporter7e3f9a1b2c";
    QByteArray body;
    body += "--" + boundary + "\r\n";
    body += "Content-Type: application/json; charset=UTF-8\r\n\r\n";
    body += QJsonDocument(meta).toJson(QJsonDocument::Compact) + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body += bytes + "\r\n";
    body += "--" + boundary + "--\r\n";

    QNetworkRequest req(
        (QUrl("https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart")));
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    req.setRawHeader("Content-Type", "multipart/related; boundary=" + boundary);
    int code = 0;
    sendBlocking(nam.post(req, body), 120000, &code);
    return code >= 200 && code < 300;
}

// Recursively upload localDir's contents under parentId.
bool uploadTree(QNetworkAccessManager& nam, const QString& token,
                const QString& localDir, const QString& parentId, int& count,
                QString& error) {
    const QFileInfoList entries = QDir(localDir).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo& fi : entries) {
        if (fi.isDir()) {
            const QString childId =
                findOrCreateFolder(nam, token, fi.fileName(), parentId, error);
            if (childId.isEmpty()) {
                if (error.isEmpty())
                    error = "Could not create the Drive subfolder \"" + fi.fileName() + "\".";
                return false;
            }
            if (!uploadTree(nam, token, fi.absoluteFilePath(), childId, count, error))
                return false;
        } else if (fi.isFile()) {
            if (uploadFile(nam, token, fi.absoluteFilePath(), parentId)) ++count;
            // Individual file failures are skipped rather than aborting the run.
        }
    }
    return true;
}

}  // namespace

UploadResult uploadDirectory(const QString& localDir, const QString& folderName) {
    UploadResult r;
    QNetworkAccessManager nam;
    const QString token = refreshAccessToken(nam, r.error);
    if (token.isEmpty()) {
        if (r.error.isEmpty())
            r.error = "Could not get a Google Drive access token. Reconnect Google Drive.";
        return r;
    }
    const QString folder = folderName.trimmed().isEmpty() ? "iMessage Export"
                                                          : folderName.trimmed();
    const QString folderId = findOrCreateFolder(nam, token, folder, "root", r.error);
    if (folderId.isEmpty()) {
        if (r.error.isEmpty())
            r.error = "Could not create the Drive folder \"" + folder + "\".";
        return r;
    }
    if (!uploadTree(nam, token, localDir, folderId, r.files, r.error)) return r;
    r.ok = true;
    return r;
}

}  // namespace drive

#include "google_auth.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QtGlobal>

#include "secret_store.hpp"

namespace googleauth {

QString clientId() {
    QString v = secret::retrieve("google_client_id");
    if (v.isEmpty()) v = QSettings().value("google/clientId").toString();  // legacy
    if (v.isEmpty()) v = qEnvironmentVariable("IMSG_GOOGLE_CLIENT_ID");
    return v;
}

QString clientSecret() {
    QString v = secret::retrieve("google_client_secret");
    if (v.isEmpty()) v = qEnvironmentVariable("IMSG_GOOGLE_CLIENT_SECRET");
    return v;
}

bool configured() { return !clientId().isEmpty(); }

void storeClient(const QString& id, const QString& secret) {
    if (!id.isEmpty()) secret::store("google_client_id", id);
    if (!secret.isEmpty()) secret::store("google_client_secret", secret);
    // Remove any earlier plaintext copy so the ID isn't visible in settings.
    QSettings().remove("google/clientId");
}

void clearClient() {
    secret::remove("google_client_id");
    secret::remove("google_client_secret");
    QSettings().remove("google/clientId");
}

bool parseClientJson(const QByteArray& json, QString& id, QString& secret,
                     QString& error) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (doc.isNull() || !doc.isObject()) {
        error = "Not a valid JSON file: " + pe.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    // Google wraps the credentials under "installed" (Desktop app) or "web".
    QJsonObject cred;
    if (root.contains("installed"))
        cred = root.value("installed").toObject();
    else if (root.contains("web"))
        cred = root.value("web").toObject();
    else
        cred = root;  // some exports are already unwrapped

    id = cred.value("client_id").toString();
    secret = cred.value("client_secret").toString();
    if (id.isEmpty()) {
        error =
            "This file has no client_id. Download the OAuth client JSON from "
            "Google Cloud Console → Clients → your Desktop app client.";
        return false;
    }
    return true;
}

}  // namespace googleauth

// Shared Google OAuth client credentials, used by both the Contacts (People
// API) and Drive connectors. The client ID + secret are kept encrypted in the
// OS keychain (secret_store) rather than plaintext settings, and can be loaded
// from the OAuth "client_secret_*.json" that Google Cloud Console hands out.
#pragma once

#include <QByteArray>
#include <QString>

namespace googleauth {

// Client ID / secret. Read from the OS keychain first (encrypted), then a legacy
// QSettings value (migrated away from), then the environment
// (IMSG_GOOGLE_CLIENT_ID / IMSG_GOOGLE_CLIENT_SECRET). Empty when unset.
QString clientId();
QString clientSecret();

// True when a client ID is available from any source.
bool configured();

// Persist the client credentials encrypted in the keychain (and drop any legacy
// plaintext settings copy), or clear them entirely.
void storeClient(const QString& id, const QString& secret);
void clearClient();

// Parse a downloaded Google Cloud OAuth client JSON (the "installed" desktop or
// "web" form) and extract client_id + client_secret. Returns false with `error`
// set when the file isn't a recognizable client JSON.
bool parseClientJson(const QByteArray& json, QString& id, QString& secret,
                     QString& error);

}  // namespace googleauth

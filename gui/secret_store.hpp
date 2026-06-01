// Small cross-platform secret store for OAuth tokens. Uses the real OS keychain
// where practical (macOS Keychain via the `security` tool; Windows Credential
// Manager), and a 0600 file under the user data dir as a fallback (Linux; a
// libsecret backend could replace it later). Values are short strings (tokens).
#pragma once

#include <QString>

namespace secret {

bool store(const QString& key, const QString& value);
QString retrieve(const QString& key);  // empty when absent
void remove(const QString& key);

}  // namespace secret

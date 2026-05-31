// Fetches iCloud contacts over CardDAV using an Apple ID + app-specific
// password (account.apple.com -> Sign-In and Security -> App-Specific
// Passwords). This is the supported, robust way for a third-party app to read
// iCloud contacts — no reverse-engineered Apple-ID login / 2FA. The returned
// vCard text is fed to imsg::parse_vcards.
//
// fetchContacts() is synchronous (it blocks on network I/O) and is intended to
// run on a worker thread, not the GUI thread.
#pragma once

#include <QString>

namespace icloud {

struct Result {
    bool ok = false;
    QString vcards;  // concatenated vCard text on success
    QString error;   // human-readable message on failure
};

Result fetchContacts(const QString& appleId, const QString& appPassword);

}  // namespace icloud

#include "icloud_contacts.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QXmlStreamReader>

namespace icloud {
namespace {

constexpr const char* kRoot = "https://contacts.icloud.com";

struct Resp {
    int status = 0;
    QByteArray body;
    QString error;
};

// Issues one (possibly non-standard, e.g. PROPFIND/REPORT) HTTPS request and
// blocks until it completes, following a couple of redirects manually since
// custom verbs don't auto-follow. Auth is HTTP Basic on every request.
Resp send(QNetworkAccessManager& nam, const QByteArray& verb, QUrl url,
          const QByteArray& body, const QByteArray& basicAuth, int depth) {
    for (int redirects = 0; redirects < 4; ++redirects) {
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", "Basic " + basicAuth);
        req.setRawHeader("Depth", QByteArray::number(depth));
        req.setRawHeader("Content-Type", "application/xml; charset=utf-8");
        req.setHeader(QNetworkRequest::UserAgentHeader, "imessage-exporter");

        QNetworkReply* reply = nam.sendCustomRequest(req, verb, body);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 301 || status == 302 || status == 307 || status == 308) {
            QUrl next = reply->attribute(QNetworkRequest::RedirectionTargetAttribute)
                            .toUrl();
            reply->deleteLater();
            if (next.isEmpty()) return {status, {}, "redirect without a target"};
            url = url.resolved(next);
            continue;
        }

        Resp out;
        out.status = status;
        out.body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && status == 0)
            out.error = reply->errorString();
        reply->deleteLater();
        return out;
    }
    return {0, {}, "too many redirects"};
}

// Resolves an href (often a bare path) returned by the server against the URL
// it came from, so we follow iCloud's partition hosts (pNN-contacts.icloud.com).
QUrl resolveHref(const QUrl& base, const QString& href) {
    return base.resolved(QUrl(href));
}

// --- Tiny CardDAV/WebDAV XML extractors (namespace-aware) ------------------

// First <href> nested under an element named `parentLocal` (any namespace).
QString hrefUnder(const QByteArray& xml, const QString& parentLocal) {
    QXmlStreamReader r(xml);
    int depthInParent = -1;
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            const QString name = r.name().toString();
            if (depthInParent >= 0) {
                ++depthInParent;
                if (name == "href") return r.readElementText().trimmed();
            } else if (name == parentLocal) {
                depthInParent = 0;
            }
        } else if (t == QXmlStreamReader::EndElement) {
            if (depthInParent >= 0) {
                if (depthInParent == 0) depthInParent = -1;
                else --depthInParent;
            }
        }
    }
    return {};
}

// hrefs of <response> elements whose <resourcetype> contains <addressbook>.
QStringList addressbookCollections(const QByteArray& xml) {
    QStringList out;
    QXmlStreamReader r(xml);
    QString currentHref;
    bool isAddressbook = false;
    bool inResponse = false;
    bool sawHref = false;
    while (!r.atEnd()) {
        auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            const QString name = r.name().toString();
            if (name == "response") {
                inResponse = true;
                currentHref.clear();
                isAddressbook = false;
                sawHref = false;
            } else if (inResponse && name == "href" && !sawHref) {
                currentHref = r.readElementText().trimmed();
                sawHref = true;
            } else if (inResponse && name == "addressbook") {
                isAddressbook = true;
            }
        } else if (t == QXmlStreamReader::EndElement) {
            if (r.name().toString() == "response") {
                if (inResponse && isAddressbook && !currentHref.isEmpty())
                    out << currentHref;
                inResponse = false;
            }
        }
    }
    return out;
}

// Concatenated text of every <address-data> element (the vCards).
QString allAddressData(const QByteArray& xml) {
    QString out;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::StartElement &&
            r.name().toString() == "address-data") {
            const QString card = r.readElementText().trimmed();
            if (!card.isEmpty()) {
                out += card;
                if (!card.endsWith('\n')) out += '\n';
            }
        }
    }
    return out;
}

}  // namespace

Result fetchContacts(const QString& appleId, const QString& appPassword) {
    if (appleId.isEmpty() || appPassword.isEmpty())
        return {false, {}, "Enter your Apple ID and an app-specific password."};

    QNetworkAccessManager nam;
    const QByteArray auth =
        (appleId + ":" + appPassword).toUtf8().toBase64();

    const QByteArray principalBody =
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<d:propfind xmlns:d="DAV:"><d:prop><d:current-user-principal/></d:prop></d:propfind>)";
    Resp r1 = send(nam, "PROPFIND", QUrl(kRoot), principalBody, auth, 0);
    if (r1.status == 401)
        return {false, {}, "iCloud rejected the credentials. Use an app-specific "
                           "password from account.apple.com, not your Apple ID password."};
    if (r1.status < 200 || r1.status >= 300)
        return {false, {}, QString("CardDAV connect failed (HTTP %1). %2")
                               .arg(r1.status).arg(r1.error)};
    const QString principal = hrefUnder(r1.body, "current-user-principal");
    if (principal.isEmpty())
        return {false, {}, "Could not find the iCloud account principal."};

    const QByteArray homeBody =
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<d:propfind xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:carddav">)"
        R"(<d:prop><c:addressbook-home-set/></d:prop></d:propfind>)";
    QUrl principalUrl = resolveHref(QUrl(kRoot), principal);
    Resp r2 = send(nam, "PROPFIND", principalUrl, homeBody, auth, 0);
    if (r2.status < 200 || r2.status >= 300)
        return {false, {}, QString("Could not read the address-book home (HTTP %1).")
                               .arg(r2.status)};
    const QString home = hrefUnder(r2.body, "addressbook-home-set");
    if (home.isEmpty())
        return {false, {}, "Could not find the iCloud address-book home."};

    const QByteArray collBody =
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<d:propfind xmlns:d="DAV:"><d:prop><d:resourcetype/><d:displayname/></d:prop></d:propfind>)";
    QUrl homeUrl = resolveHref(principalUrl, home);
    Resp r3 = send(nam, "PROPFIND", homeUrl, collBody, auth, 1);
    if (r3.status < 200 || r3.status >= 300)
        return {false, {}, QString("Could not list address books (HTTP %1).")
                               .arg(r3.status)};
    const QStringList books = addressbookCollections(r3.body);
    if (books.isEmpty())
        return {false, {}, "No address books were found in this iCloud account."};

    const QByteArray reportBody =
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<c:addressbook-query xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:carddav">)"
        R"(<d:prop><c:address-data/></d:prop></c:addressbook-query>)";

    QString vcards;
    for (const QString& book : books) {
        QUrl bookUrl = resolveHref(homeUrl, book);
        Resp rep = send(nam, "REPORT", bookUrl, reportBody, auth, 1);
        if (rep.status >= 200 && rep.status < 300) vcards += allAddressData(rep.body);
    }
    if (vcards.isEmpty())
        return {false, {}, "Connected, but no contacts were returned."};
    return {true, vcards, {}};
}

}  // namespace icloud

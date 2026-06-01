#include "link_preview.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <initializer_list>

#include "imsg/version.hpp"

namespace linkpreview {
namespace {

// Budgets: keep a single link's previews bounded so a slow/huge page can't stall
// an export. One round-trip for the page, one for its hero image.
constexpr int kTimeoutMs = 6000;
constexpr qint64 kMaxPageBytes = 2 * 1024 * 1024;
constexpr qint64 kMaxImageBytes = 3 * 1024 * 1024;

QString htmlEscape(const QString& s) {
    QString o = s;
    o.replace('&', "&amp;");  // first, so we don't double-escape the entities below
    o.replace('<', "&lt;");
    o.replace('>', "&gt;");
    o.replace('"', "&quot;");
    return o;
}

// Decode the handful of HTML entities that commonly appear in OG meta content.
QString decodeEntities(QString s) {
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&quot;", "\"");
    s.replace("&#34;", "\"");
    s.replace("&#039;", "'");
    s.replace("&#39;", "'");
    s.replace("&#x27;", "'", Qt::CaseInsensitive);
    s.replace("&apos;", "'");
    s.replace("&nbsp;", " ");
    s.replace("&amp;", "&");  // last, so a literal "&amp;amp;" survives correctly
    return s;
}

QByteArray guessImageMime(const QUrl& url) {
    const QString p = url.path().toLower();
    if (p.endsWith(".png")) return "image/png";
    if (p.endsWith(".gif")) return "image/gif";
    if (p.endsWith(".webp")) return "image/webp";
    if (p.endsWith(".svg")) return "image/svg+xml";
    return "image/jpeg";  // .jpg/.jpeg and the common default
}

struct Fetched {
    QByteArray body;
    QByteArray mime;
    QUrl finalUrl;
    bool ok = false;
};

// Synchronous GET with a hard timeout and a download-size cap. Runs its own
// QEventLoop, so it must be called from a thread that isn't already running one
// for these objects — i.e. the export worker thread, never the GUI thread.
Fetched httpGet(QNetworkAccessManager& nam, const QUrl& url, qint64 maxBytes) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QByteArray("Mozilla/5.0 (compatible; iMessageExporter/" IMSG_VERSION
                             "; +https://github.com/grioghar/imessage-exporter-redux)"));
    req.setRawHeader("Accept",
                     "text/html,application/xhtml+xml,image/*;q=0.9,*/*;q=0.8");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(5);

    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                     [reply, maxBytes](qint64 received, qint64) {
                         if (maxBytes > 0 && received > maxBytes) reply->abort();
                     });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(kTimeoutMs);
    loop.exec();

    Fetched f;
    if (reply->error() == QNetworkReply::NoError) {
        f.body = reply->readAll();
        if (f.body.size() <= maxBytes) {
            f.mime = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
            f.finalUrl = reply->url();
            f.ok = true;
        }
    }
    reply->deleteLater();  // also reaped by `nam` going out of scope at the call site
    return f;
}

// Pull og:* / twitter:* meta values out of a page's HTML. First value wins.
QHash<QString, QString> parseMeta(const QString& html) {
    static const QRegularExpression metaRe(
        "<meta\\b[^>]*>",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression keyRe(
        "(?:property|name)\\s*=\\s*[\"']\\s*((?:og|twitter):[^\"']+?)\\s*[\"']",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression valRe(
        "content\\s*=\\s*([\"'])(.*?)\\1",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);

    QHash<QString, QString> meta;
    auto it = metaRe.globalMatch(html);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        const auto km = keyRe.match(tag);
        if (!km.hasMatch()) continue;
        const auto vm = valRe.match(tag);
        if (!vm.hasMatch()) continue;
        const QString key = km.captured(1).toLower();
        const QString val = decodeEntities(vm.captured(2)).trimmed();
        if (!val.isEmpty() && !meta.contains(key)) meta.insert(key, val);
    }
    return meta;
}

QString first(const QHash<QString, QString>& m, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        auto it = m.find(QString::fromLatin1(k));
        if (it != m.end() && !it.value().isEmpty()) return it.value();
    }
    return QString();
}

QString docTitle(const QString& html) {
    static const QRegularExpression re(
        "<title[^>]*>(.*?)</title>",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(html);
    return m.hasMatch() ? decodeEntities(m.captured(1)).simplified() : QString();
}

QString hostLabel(const QUrl& url) {
    QString h = url.host();
    if (h.startsWith("www.", Qt::CaseInsensitive)) h = h.mid(4);
    return h;
}

std::string buildCard(const QString& url, const QString& title, const QString& desc,
                      const QString& label, const QString& imgDataUri) {
    QString card = "<a class=\"ogcard\" href=\"" + htmlEscape(url) +
                   "\" target=\"_blank\" rel=\"noopener noreferrer\">";
    if (!imgDataUri.isEmpty())
        card += "<img class=\"ogcard-img\" loading=\"lazy\" alt=\"\" src=\"" + imgDataUri +
                "\">";
    card += "<div class=\"ogcard-body\">";
    card += "<div class=\"ogcard-title\">" + htmlEscape(title) + "</div>";
    if (!desc.isEmpty())
        card += "<div class=\"ogcard-desc\">" + htmlEscape(desc) + "</div>";
    if (!label.isEmpty())
        card += "<div class=\"ogcard-host\">" + htmlEscape(label) + "</div>";
    card += "</div></a>";
    return card.toStdString();
}

std::string buildCardUncached(const std::string& url) {
    const QUrl pageUrl = QUrl::fromUserInput(QString::fromStdString(url));
    const QString scheme = pageUrl.scheme().toLower();
    if (scheme != "http" && scheme != "https") return std::string();

    QNetworkAccessManager nam;
    const Fetched page = httpGet(nam, pageUrl, kMaxPageBytes);
    if (!page.ok || page.body.isEmpty()) return std::string();

    const QString html = QString::fromUtf8(page.body);
    const QHash<QString, QString> meta = parseMeta(html);

    QString title = first(meta, {"og:title", "twitter:title"});
    if (title.isEmpty()) title = docTitle(html);
    if (title.isEmpty()) return std::string();  // no usable preview → favicon card
    if (title.size() > 200) title = title.left(197) + "…";

    QString desc = first(meta, {"og:description", "twitter:description"});
    if (desc.size() > 300) desc = desc.left(297) + "…";

    QString label = first(meta, {"og:site_name"});
    if (label.isEmpty()) label = hostLabel(page.finalUrl.isValid() ? page.finalUrl : pageUrl);

    // Hero image → embedded data URI so the export stays self-contained offline.
    QString imgDataUri;
    const QString img = first(meta, {"og:image:secure_url", "og:image:url", "og:image",
                                     "twitter:image", "twitter:image:src"});
    if (!img.isEmpty()) {
        const QUrl base = page.finalUrl.isValid() ? page.finalUrl : pageUrl;
        const QUrl imgUrl = base.resolved(QUrl(img));
        const QString is = imgUrl.scheme().toLower();
        if (is == "http" || is == "https") {
            const Fetched pic = httpGet(nam, imgUrl, kMaxImageBytes);
            if (pic.ok && !pic.body.isEmpty()) {
                QByteArray mime = pic.mime.split(';').value(0).trimmed();
                if (mime.isEmpty() || !mime.startsWith("image"))
                    mime = guessImageMime(imgUrl);
                imgDataUri = "data:" + QString::fromLatin1(mime) + ";base64," +
                             QString::fromLatin1(pic.body.toBase64());
            }
        }
    }

    return buildCard(QString::fromStdString(url), title, desc, label, imgDataUri);
}

}  // namespace

std::string fetch_og_card(const std::string& url) {
    static QMutex mutex;
    static QHash<QString, std::string> cache;  // url → card HTML ("" = negative)
    const QString key = QString::fromStdString(url);
    {
        QMutexLocker lock(&mutex);
        auto it = cache.find(key);
        if (it != cache.end()) return it.value();
    }
    std::string card = buildCardUncached(url);  // network; outside the lock
    {
        QMutexLocker lock(&mutex);
        cache.insert(key, card);
    }
    return card;
}

}  // namespace linkpreview

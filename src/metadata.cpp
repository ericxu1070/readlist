#include "metadata.h"

#include "paperlist.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace {

QString collapse(const QString &s) { return s.simplified(); }

QNetworkRequest request(const QUrl &url) {
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("readlist/0.1"));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(15000);
    return r;
}

QString decodeEntities(QString s) {
    static const QRegularExpression numeric(R"(&#(x?)([0-9a-fA-F]+);)");
    QString out;
    qsizetype last = 0;
    auto it = numeric.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        bool ok = false;
        const uint code = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
        out += ok ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1) : m.captured(0);
        last = m.capturedEnd();
    }
    out += s.mid(last);
    out.replace("&quot;", "\"").replace("&#39;", "'").replace("&apos;", "'")
       .replace("&lt;", "<").replace("&gt;", ">").replace("&nbsp;", " ").replace("&amp;", "&");
    return out;
}

QString metaContent(const QString &html, const QString &name) {
    // Attribute order varies between sites, so try both orders.
    const QString n = QRegularExpression::escape(name);
    const QRegularExpression a(QStringLiteral(R"(<meta[^>]+(?:property|name)\s*=\s*["']%1["'][^>]*content\s*=\s*["']([^"']*)["'])").arg(n),
                               QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression b(QStringLiteral(R"(<meta[^>]+content\s*=\s*["']([^"']*)["'][^>]*(?:property|name)\s*=\s*["']%1["'])").arg(n),
                               QRegularExpression::CaseInsensitiveOption);
    for (const auto &re : {a, b}) {
        const auto m = re.match(html);
        if (m.hasMatch())
            return collapse(decodeEntities(m.captured(1)));
    }
    return {};
}

} // namespace

MetadataStore::MetadataStore(const QString &directory, QObject *parent)
    : QObject(parent),
      m_directory(directory.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/readlist"
                      : directory) {}

QString MetadataStore::cachePath(const QString &key) const {
    // arXiv ids are readable file names; urls get hashed.
    static const QRegularExpression safe(R"(^[A-Za-z0-9.\-/]+$)");
    const QString name = !key.startsWith(QStringLiteral("http")) && safe.match(key).hasMatch()
        ? QString(key).replace('/', '_')
        : QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
    return m_directory + "/" + name + ".json";
}

bool MetadataStore::has(const QString &key) const { return cached(key).isValid(); }

PaperInfo MetadataStore::cached(const QString &key) const {
    const auto hit = m_memory.constFind(key);
    if (hit != m_memory.constEnd())
        return *hit;
    PaperInfo info;
    QFile file(cachePath(key));
    if (!file.open(QIODevice::ReadOnly))
        return info;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    info.key = key;
    info.url = o.value("url").toString();
    info.title = o.value("title").toString();
    for (const auto &a : o.value("authors").toArray())
        info.authors << a.toString();
    info.published = o.value("published").toString();
    info.abstract = o.value("abstract").toString();
    m_memory.insert(key, info);
    return info;
}

void MetadataStore::store(const PaperInfo &info) {
    if (!info.isValid())
        return;
    m_memory.insert(info.key, info);
    QDir().mkpath(m_directory);
    QSaveFile file(cachePath(info.key));
    if (!file.open(QIODevice::WriteOnly))
        return; // The cache is an optimisation; the next view fetches again.
    QJsonObject o{{"url", info.url}, {"title", info.title}, {"published", info.published},
                  {"abstract", info.abstract}, {"authors", QJsonArray::fromStringList(info.authors)}};
    file.write(QJsonDocument(o).toJson());
    file.commit();
}

QList<PaperInfo> MetadataStore::parseArxivFeed(const QByteArray &xml) {
    QList<PaperInfo> out;
    QXmlStreamReader r(xml);
    PaperInfo current;
    bool inEntry = false, inAuthor = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const auto name = r.name();
            if (name == QLatin1String("entry")) {
                inEntry = true;
                current = PaperInfo();
            } else if (inEntry && name == QLatin1String("author")) {
                inAuthor = true;
            } else if (inEntry && inAuthor && name == QLatin1String("name")) {
                current.authors << collapse(r.readElementText());
            } else if (inEntry && !inAuthor && name == QLatin1String("id")) {
                const QString id = PaperList::arxivIdFromUrl(r.readElementText().trimmed());
                current.key = id;
                current.url = QStringLiteral("https://arxiv.org/abs/") + id;
            } else if (inEntry && name == QLatin1String("title")) {
                current.title = collapse(r.readElementText());
            } else if (inEntry && name == QLatin1String("summary")) {
                current.abstract = collapse(r.readElementText());
            } else if (inEntry && name == QLatin1String("published")) {
                current.published = r.readElementText().left(10);
            }
        } else if (r.isEndElement()) {
            if (r.name() == QLatin1String("author"))
                inAuthor = false;
            else if (r.name() == QLatin1String("entry")) {
                inEntry = false;
                // arXiv answers a malformed id with an entry titled "Error".
                if (!current.key.isEmpty() && current.title != QLatin1String("Error"))
                    out << current;
            }
        }
    }
    return out;
}

PaperInfo MetadataStore::parseHtml(const QByteArray &bytes, const QString &url) {
    const QString html = QString::fromUtf8(bytes);
    PaperInfo info;
    info.url = url;
    info.key = url;
    info.title = metaContent(html, "citation_title");
    if (info.title.isEmpty())
        info.title = metaContent(html, "og:title");
    if (info.title.isEmpty()) {
        static const QRegularExpression title(R"(<title[^>]*>([^<]*)</title>)", QRegularExpression::CaseInsensitiveOption);
        info.title = collapse(decodeEntities(title.match(html).captured(1)));
    }
    // "Post title | Site name": the site name is noise in a reading list.
    const int bar = info.title.lastIndexOf(QStringLiteral(" | "));
    if (bar > 0)
        info.title = info.title.left(bar).trimmed();
    info.abstract = metaContent(html, "citation_abstract");
    if (info.abstract.isEmpty())
        info.abstract = metaContent(html, "og:description");
    if (info.abstract.isEmpty())
        info.abstract = metaContent(html, "description");
    static const QRegularExpression author(R"(<meta[^>]+name\s*=\s*["']citation_author["'][^>]*content\s*=\s*["']([^"']*)["'])",
                                           QRegularExpression::CaseInsensitiveOption);
    auto it = author.globalMatch(html);
    while (it.hasNext())
        info.authors << collapse(decodeEntities(it.next().captured(1)));
    info.published = metaContent(html, "citation_publication_date").replace('/', '-').left(10);
    if (info.title.isEmpty())
        info.error = QStringLiteral("no title found on the page");
    return info;
}

void MetadataStore::fetch(const QStringList &urls) {
    QStringList ids;
    for (const QString &url : urls) {
        const QString id = PaperList::arxivIdFromUrl(url);
        if (id.isEmpty())
            fetchPage(url);
        else
            ids << id;
    }
    // arXiv asks clients to batch; 50 ids per call keeps the URL short.
    for (int i = 0; i < ids.size(); i += 50)
        fetchArxiv(ids.mid(i, 50));
}

void MetadataStore::fetchArxiv(const QStringList &ids) {
    QUrl url(QStringLiteral("https://export.arxiv.org/api/query"));
    QUrlQuery q;
    q.addQueryItem("id_list", ids.join(','));
    q.addQueryItem("max_results", QString::number(ids.size()));
    url.setQuery(q);
    QNetworkReply *reply = m_network.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, ids] {
        reply->deleteLater();
        QHash<QString, PaperInfo> found;
        QString error;
        if (reply->error() != QNetworkReply::NoError)
            error = QStringLiteral("arXiv: %1").arg(reply->errorString());
        else
            for (const PaperInfo &info : parseArxivFeed(reply->readAll()))
                found.insert(info.key, info);
        for (const QString &id : ids) {
            PaperInfo info = found.value(id);
            if (info.key.isEmpty()) {
                info.key = id;
                info.url = QStringLiteral("https://arxiv.org/abs/") + id;
                info.error = error.isEmpty() ? QStringLiteral("arXiv has no paper %1").arg(id) : error;
            } else {
                store(info);
            }
            emit fetched(info);
        }
    });
}

void MetadataStore::fetchPage(const QString &url) {
    QNetworkReply *reply = m_network.get(request(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        PaperInfo info;
        if (reply->error() != QNetworkReply::NoError) {
            info.key = info.url = url;
            info.error = reply->errorString();
        } else {
            info = parseHtml(reply->read(2 * 1024 * 1024), url);
            store(info);
        }
        emit fetched(info);
    });
}

QList<PaperInfo> MetadataStore::fetchBlocking(const QStringList &urls, int timeoutMs) {
    QHash<QString, PaperInfo> results;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto connection = connect(this, &MetadataStore::fetched, &loop, [&](const PaperInfo &info) {
        results.insert(info.key, info);
        if (results.size() >= urls.size())
            loop.quit();
    });
    fetch(urls);
    if (results.size() < urls.size()) {
        timeout.start(timeoutMs);
        loop.exec();
    }
    disconnect(connection);

    QList<PaperInfo> out;
    for (const QString &url : urls) {
        const QString id = PaperList::arxivIdFromUrl(url);
        const QString key = id.isEmpty() ? url : id;
        PaperInfo info = results.value(key);
        if (info.key.isEmpty()) {
            info.key = key;
            info.url = url;
            info.error = QStringLiteral("timed out");
        }
        out << info;
    }
    return out;
}

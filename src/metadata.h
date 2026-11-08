#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

// What the app knows about a paper beyond its list line: authors, date, and
// the abstract. Fetched once from arXiv (or the page's meta tags for other
// links) and cached on disk, so the abstract view opens instantly offline.
struct PaperInfo {
    QString key;   // Paper::key(): arXiv id or url
    QString url;
    QString title;
    QStringList authors;
    QString published; // YYYY-MM-DD
    QString abstract;
    QString error;     // Set when the fetch failed
    bool isValid() const { return error.isEmpty() && !title.isEmpty(); }
};

class MetadataStore : public QObject {
    Q_OBJECT

public:
    explicit MetadataStore(const QString &cacheDirectory = {}, QObject *parent = nullptr);

    bool has(const QString &key) const;
    PaperInfo cached(const QString &key) const;
    void store(const PaperInfo &info);

    // Fetches each url (arXiv ids in one batched API call), emitting
    // fetched() once per url, with info.error set when it failed.
    void fetch(const QStringList &urls);
    // The same, for the CLI: waits up to timeoutMs and returns the results
    // in the order given.
    QList<PaperInfo> fetchBlocking(const QStringList &urls, int timeoutMs = 20000);

    static QList<PaperInfo> parseArxivFeed(const QByteArray &xml);
    static PaperInfo parseHtml(const QByteArray &html, const QString &url);

signals:
    void fetched(const PaperInfo &info);

private:
    QString cachePath(const QString &key) const;
    void fetchArxiv(const QStringList &ids);
    void fetchPage(const QString &url);

    QString m_directory;
    QNetworkAccessManager m_network;
    mutable QHash<QString, PaperInfo> m_memory;
};

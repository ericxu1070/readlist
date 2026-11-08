#include "backend.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>

namespace {

// "Kochkov et al. · 2023" from the cached authors and date.
QString byline(const PaperInfo &info) {
    QStringList parts;
    auto last = [](const QString &name) { return name.section(' ', -1); };
    if (info.authors.size() == 1)
        parts << last(info.authors[0]);
    else if (info.authors.size() == 2)
        parts << last(info.authors[0]) + " & " + last(info.authors[1]);
    else if (info.authors.size() > 2)
        parts << last(info.authors[0]) + " et al.";
    if (!info.published.isEmpty())
        parts << info.published.left(4);
    return parts.join(QStringLiteral(" · "));
}

} // namespace

Backend::Backend(const QString &path, const QString &cacheDirectory, QObject *parent)
    : QObject(parent), m_path(path.isEmpty() ? ListFile::defaultPath() : path), m_metadata(cacheDirectory) {
    launcher = &Browser::open;
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &Backend::reload);
    m_statusClear.setSingleShot(true);
    m_statusClear.setInterval(5000);
    connect(&m_statusClear, &QTimer::timeout, this, [this] { setStatus({}); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_metadata, &MetadataStore::fetched, this, &Backend::onFetched);
    reload();
}

void Backend::setStatus(const QString &text, bool error) {
    m_status = text;
    m_statusIsError = error;
    emit statusChanged();
    // Confirmations fade after a few seconds; errors stay until the next action.
    if (error || text.isEmpty())
        m_statusClear.stop();
    else
        m_statusClear.start();
}

void Backend::reload() {
    // Atomic saves (ours, Syncthing's, Obsidian's) replace the file, which
    // drops it from the watch, so re-arm on every reload.
    const auto watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    const QString dir = QFileInfo(m_path).absolutePath();
    if (QFileInfo::exists(dir))
        m_watcher.addPath(dir);
    if (QFileInfo::exists(m_path))
        m_watcher.addPath(m_path);

    QString text;
    const QString error = ListFile::readText(m_path, text);
    if (!error.isEmpty()) {
        setStatus(error, true);
        return;
    }
    PaperList list = PaperList::parse(text);
    // Papers ticked in Obsidian get filed under Read right away.
    if (list.normalize()) {
        const QString saveError = ListFile::save(m_path, list);
        if (!saveError.isEmpty())
            setStatus(saveError, true);
    }
    rebuild(list);
}

QVariantMap Backend::row(const Paper &p) const {
    const PaperInfo info = m_metadata.cached(p.key());
    QString title = p.title;
    if (title.isEmpty())
        title = info.isValid() ? info.title : p.url;
    QString sub = byline(info);
    const QString id = p.arxivId();
    if (!id.isEmpty())
        sub = sub.isEmpty() ? id : sub + QStringLiteral(" · ") + id;
    else
        sub = sub.isEmpty() ? QUrl(p.url).host() : sub + QStringLiteral(" · ") + QUrl(p.url).host();
    return {{"key", p.key()}, {"title", title}, {"subtitle", sub}, {"url", p.url},
            {"pending", p.title.isEmpty()}, {"read", p.read.isValid() ? p.read.toString(Qt::ISODate) : QString()}};
}

void Backend::rebuild(const PaperList &list) {
    m_queue.clear();
    m_done.clear();
    m_papers.clear();
    QStringList missing;
    for (const Paper &p : list.queue) {
        m_queue << row(p);
        m_papers.insert(p.key(), p);
    }
    for (const Paper &p : list.done) {
        m_done << row(p);
        m_papers.insert(p.key(), p);
    }
    // Queued papers get their abstracts ahead of time, so browsing them is
    // instant; a failed fetch is not retried until the app restarts.
    for (const Paper &p : list.queue)
        if ((!m_metadata.has(p.key()) || p.needsTitle()) && !m_inFlight.contains(p.key()) && !m_failed.contains(p.key()))
            missing << p.url;
    for (const QString &url : missing) {
        const QString id = PaperList::arxivIdFromUrl(url);
        m_inFlight.insert(id.isEmpty() ? url : id);
    }
    if (!missing.isEmpty())
        m_metadata.fetch(missing);
    emit listChanged();
}

void Backend::onFetched(const PaperInfo &info) {
    m_inFlight.remove(info.key);
    if (!info.isValid()) {
        m_failed.insert(info.key);
        m_errors.insert(info.key, info.error);
        // Only a paper still waiting for its title needs to hear about it;
        // a missing abstract shows up in the abstract view instead.
        const auto paper = m_papers.constFind(info.key);
        if (paper != m_papers.constEnd() && paper->needsTitle())
            setStatus(tr("Could not fetch the title for %1: %2").arg(info.key, info.error), true);
        ++m_infoRevision;
        emit infoChanged();
        return;
    }
    const auto paper = m_papers.constFind(info.key);
    if (paper != m_papers.constEnd() && paper->needsTitle()) {
        const QString key = info.key, title = info.title;
        // Not undoable on its own: it completes the add that started it.
        mutate([key, title](PaperList &l) {
            int i = l.indexOf(key, false);
            if (i >= 0 && l.queue[i].title.isEmpty())
                l.queue[i].title = title;
            i = l.indexOf(key, true);
            if (i >= 0 && l.done[i].title.isEmpty())
                l.done[i].title = title;
            return QString();
        });
        if (m_undo.size() > 0)
            m_undo.removeLast();
        emit historyChanged();
        setStatus(tr("Added “%1”").arg(info.title));
    } else {
        reload();
    }
    ++m_infoRevision;
    emit infoChanged();
}

bool Backend::mutate(const std::function<QString(PaperList &)> &edit, const QString &doneMessage) {
    QString before;
    QString error = ListFile::readText(m_path, before);
    if (!error.isEmpty()) {
        setStatus(error, true);
        return false;
    }
    PaperList list = PaperList::parse(before);
    list.normalize();
    error = edit(list);
    if (!error.isEmpty()) {
        setStatus(error, true);
        return false;
    }
    error = ListFile::save(m_path, list);
    if (!error.isEmpty()) {
        setStatus(error, true);
        return false;
    }
    m_undo << before;
    if (m_undo.size() > 100)
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
    if (!doneMessage.isEmpty())
        setStatus(doneMessage);
    rebuild(list);
    return true;
}

void Backend::add(const QString &text) {
    const QString url = PaperList::canonicalUrl(text);
    if (url.isEmpty()) {
        setStatus(tr("Not an arXiv link, arXiv id, or web address: %1").arg(text.trimmed()), true);
        return;
    }
    Paper paper;
    paper.url = url;
    paper.added = QDate::currentDate();
    const PaperInfo info = m_metadata.cached(paper.key());
    if (info.isValid())
        paper.title = info.title;
    if (mutate([paper](PaperList &l) { return l.add(paper, true); }))
        setStatus(paper.title.isEmpty() ? tr("Fetching the title…") : tr("Added “%1”").arg(paper.title));
}

void Backend::markRead(const QString &key) {
    const QString title = m_papers.value(key).title;
    mutate([key](PaperList &l) { return l.markRead(key); }, tr("Marked read: %1").arg(title));
}

void Backend::markUnread(const QString &key) {
    const QString title = m_papers.value(key).title;
    mutate([key](PaperList &l) { return l.markUnread(key); }, tr("Back in the queue: %1").arg(title));
}

void Backend::remove(const QString &key) {
    const QString title = m_papers.value(key).title;
    mutate([key](PaperList &l) { return l.remove(key); }, tr("Removed “%1” (Ctrl+Z brings it back)").arg(title));
}

void Backend::move(const QString &key, const QString &where) {
    mutate([key, where](PaperList &l) { return l.move(key, where); });
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    QString current;
    QString error = ListFile::readText(m_path, current);
    if (error.isEmpty())
        error = ListFile::writeText(m_path, m_undo.last());
    if (!error.isEmpty()) {
        setStatus(error, true);
        return;
    }
    m_undo.removeLast();
    m_redo << current;
    emit historyChanged();
    setStatus(tr("Undone"));
    reload();
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    QString current;
    QString error = ListFile::readText(m_path, current);
    if (error.isEmpty())
        error = ListFile::writeText(m_path, m_redo.last());
    if (!error.isEmpty()) {
        setStatus(error, true);
        return;
    }
    m_redo.removeLast();
    m_undo << current;
    emit historyChanged();
    setStatus(tr("Redone"));
    reload();
}

QVariantMap Backend::info(const QString &key) {
    const Paper paper = m_papers.value(key);
    const PaperInfo info = m_metadata.cached(key);
    QVariantMap out{{"key", key}, {"url", paper.url}, {"title", paper.title.isEmpty() ? info.title : paper.title},
                    {"authors", info.authors.join(QStringLiteral(", "))}, {"published", info.published},
                    {"abstract", info.abstract}, {"arxivId", paper.arxivId()},
                    {"loading", false}, {"error", QString()}};
    if (out["title"].toString().isEmpty())
        out["title"] = paper.url;
    if (info.isValid())
        return out;
    if (m_failed.contains(key)) {
        out["error"] = m_errors.value(key);
    } else if (!paper.url.isEmpty()) {
        if (!m_inFlight.contains(key)) {
            m_inFlight.insert(key);
            m_metadata.fetch({paper.url});
        }
        out["loading"] = true;
    }
    return out;
}

QString Backend::clipboardLink() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};
    const QString text = clipboard->text().trimmed();
    return !text.contains('\n') && !PaperList::canonicalUrl(text).isEmpty() ? text : QString();
}

bool Backend::openPaper(const QString &key) {
    const auto paper = m_papers.constFind(key);
    if (paper == m_papers.constEnd())
        return false;
    if (!launcher(paper->openUrl())) {
        setStatus(tr("No browser launcher found (omarchy-launch-browser or xdg-open)"), true);
        return false;
    }
    return true;
}

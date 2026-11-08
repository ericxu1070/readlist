#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <functional>

#include "metadata.h"
#include "paperlist.h"

// The window's view of the reading list. Every change re-reads the note,
// applies one edit, and writes it back, so edits made meanwhile in Obsidian,
// on another machine, or by an agent through the CLI are never lost.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList queue READ queue NOTIFY listChanged)
    Q_PROPERTY(QVariantList done READ done NOTIFY listChanged)
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(int infoRevision READ infoRevision NOTIFY infoChanged)

public:
    explicit Backend(const QString &path = {}, const QString &cacheDirectory = {}, QObject *parent = nullptr);

    QVariantList queue() const { return m_queue; }
    QVariantList done() const { return m_done; }
    QString path() const { return m_path; }
    QString status() const { return m_status; }
    bool statusIsError() const { return m_statusIsError; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int infoRevision() const { return m_infoRevision; }

    Q_INVOKABLE bool openPaper(const QString &key);
    Q_INVOKABLE void add(const QString &text);
    Q_INVOKABLE void markRead(const QString &key);
    Q_INVOKABLE void markUnread(const QString &key);
    Q_INVOKABLE void remove(const QString &key);
    Q_INVOKABLE void move(const QString &key, const QString &where);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Title, authors, date, and abstract for the abstract view; fetches them
    // the first time and bumps infoRevision when they arrive.
    Q_INVOKABLE QVariantMap info(const QString &key);
    // A link or arXiv id on the clipboard, to prefill the add field.
    Q_INVOKABLE QString clipboardLink() const;
    Q_INVOKABLE void reload();

    // Replaces the browser launcher, so tests never open a browser.
    std::function<bool(const QString &url)> launcher;

signals:
    void listChanged();
    void statusChanged();
    void historyChanged();
    void infoChanged();

private:
    bool mutate(const std::function<QString(PaperList &)> &edit, const QString &done = {});
    void setStatus(const QString &text, bool error = false);
    void rebuild(const PaperList &list);
    void onFetched(const PaperInfo &info);
    QVariantMap row(const Paper &p) const;

    QString m_path;
    MetadataStore m_metadata;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce, m_statusClear;
    QVariantList m_queue, m_done;
    QHash<QString, Paper> m_papers;
    QSet<QString> m_inFlight, m_failed;
    QHash<QString, QString> m_errors;
    QStringList m_undo, m_redo;
    QString m_status;
    bool m_statusIsError = false;
    int m_infoRevision = 0;
};

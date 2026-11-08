#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

// One line of the reading list: "- [ ] [Title](url) · added 2026-11-08".
struct Paper {
    QString title;
    QString url;
    QDate added;
    QDate read;
    QStringList extra; // Any other " · " suffixes, kept as written.
    bool ticked = false; // Ticked under Queue in Obsidian; normalize() files it as read.

    // The arXiv id ("2204.07137") when the url is an arXiv link, else empty.
    QString arxivId() const;
    // Stable identity across edits: the arXiv id, or the url itself.
    QString key() const;
    // Where Enter takes you: the PDF for arXiv papers, the url otherwise.
    QString openUrl() const;
    // A bare link someone pasted in Obsidian, still waiting for its title.
    bool needsTitle() const { return title.isEmpty(); }
};

// The reading list note. The app owns the "## Queue" and "## Read" sections;
// everything above them, any later "## ..." section, and lines it does not
// recognise inside its sections are written back exactly as they were, so
// hand edits in Obsidian survive.
class PaperList {
public:
    QList<Paper> queue;
    QList<Paper> done;

    static PaperList parse(const QString &text);
    QString serialize() const;

    // Moves papers ticked in Obsidian ("- [x]" under Queue) into Read.
    // Returns true when anything changed, so callers know to save.
    bool normalize(const QDate &today = QDate::currentDate());

    // Index into queue or done, or -1.
    int indexOf(const QString &key, bool inDone) const;
    bool contains(const QString &key) const { return indexOf(key, false) >= 0 || indexOf(key, true) >= 0; }

    // Each returns an empty string on success, else what went wrong.
    QString add(const Paper &paper, bool atTop);
    QString markRead(const QString &key, const QDate &today = QDate::currentDate());
    QString markUnread(const QString &key);
    QString remove(const QString &key);
    // where: "up", "down", "top", "bottom", or a 1-based position.
    QString move(const QString &key, const QString &where);

    // Turns "2204.07137", "arXiv:2204.07137v2", ".../abs/2204.07137",
    // ".../pdf/2204.07137v1.pdf" or any http(s) url into a canonical url.
    // Returns an empty string for anything else.
    static QString canonicalUrl(const QString &input);
    static QString arxivIdFromUrl(const QString &url);

    QString preamble;
    QStringList queueLoose, doneLoose;
    QString postamble;
    bool hadSections = false;
};

// Reads and writes the note on disk. Writes are atomic (QSaveFile), so
// Syncthing and Obsidian never see a half-written file.
namespace ListFile {
QString defaultPath();
QString load(const QString &path, PaperList &list); // error or empty
QString save(const QString &path, const PaperList &list);
QString readText(const QString &path, QString &text);
QString writeText(const QString &path, const QString &text);
}

namespace Browser {
// Opens a url with Omarchy's browser launcher (which focuses the browser),
// or xdg-open without Omarchy. False when neither exists.
bool open(const QString &url);
}

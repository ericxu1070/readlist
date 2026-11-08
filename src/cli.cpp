#include "cli.h"

#include "metadata.h"
#include "paperlist.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <cstdio>

namespace {

QTextStream &out() { static QTextStream s(stdout); return s; }
QTextStream &err() { static QTextStream s(stderr); return s; }

int fail(const QString &message) {
    err() << "readlist: " << message << Qt::endl;
    return 1;
}

const char *kUsage = R"(readlist - a keyboard-first arXiv reading list

Usage:
  readlist                         open the window
  readlist list [--read|--all] [--json]
  readlist add <link|id>... [--bottom] [--json]
                                   fetches each title from arXiv (or the page)
  readlist add <link> --title "<title>"
                                   names it yourself, skipping the fetch
  readlist show <paper> [--json]   title, authors, date, abstract
  readlist read <paper>...         mark read
  readlist unread <paper>...       back to the top of the queue
  readlist move <paper> up|down|top|bottom|<position>
  readlist remove <paper>...
  readlist open <paper>            open the PDF in the browser
  readlist path                    print the list's file
  readlist skill install [--dir <skills dir>]
                                   install the agent skill (default ~/.claude/skills)

<paper> is an arXiv id (2204.07137), an arXiv or web link, or a queue
position (1 is the top). The list is a Markdown note, set by the first line
of ~/.config/readlist/path (or $READLIST_FILE). Errors go to stderr with exit status 1.
)";

// Resolves a <paper> argument to the paper's key in the list.
QString resolve(const PaperList &list, const QString &arg, QString &error) {
    bool isNumber = false;
    const int position = arg.toInt(&isNumber);
    if (isNumber) {
        if (position < 1 || position > list.queue.size()) {
            error = QStringLiteral("no paper at position %1 (the queue has %2)").arg(arg).arg(list.queue.size());
            return {};
        }
        return list.queue[position - 1].key();
    }
    for (bool inDone : {false, true}) {
        const int i = list.indexOf(arg, inDone);
        if (i >= 0)
            return (inDone ? list.done : list.queue)[i].key();
    }
    error = QStringLiteral("%1 is not in the list").arg(arg);
    return {};
}

QJsonObject toJson(const Paper &p, int position, const PaperInfo &info) {
    QJsonObject o{{"key", p.key()}, {"title", p.title.isEmpty() ? info.title : p.title}, {"url", p.url},
                  {"pdf", p.openUrl()}, {"status", p.read.isValid() ? "read" : "queued"}};
    if (position > 0)
        o["position"] = position;
    if (!p.arxivId().isEmpty())
        o["arxiv_id"] = p.arxivId();
    if (p.added.isValid())
        o["added"] = p.added.toString(Qt::ISODate);
    if (p.read.isValid())
        o["read"] = p.read.toString(Qt::ISODate);
    if (!info.authors.isEmpty())
        o["authors"] = QJsonArray::fromStringList(info.authors);
    if (!info.published.isEmpty())
        o["published"] = info.published;
    return o;
}

void printJson(const QJsonValue &value) {
    out() << (value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject()))
                 .toJson(QJsonDocument::Indented);
    out().flush();
}

// Loads the list, applies an edit, and saves it, for the editing commands.
int edit(const QString &path, const std::function<QString(PaperList &)> &apply) {
    PaperList list;
    QString error = ListFile::load(path, list);
    if (!error.isEmpty())
        return fail(error);
    error = apply(list);
    if (!error.isEmpty())
        return fail(error);
    error = ListFile::save(path, list);
    return error.isEmpty() ? 0 : fail(error);
}

QString skillText() {
    QFile file(":/skill/SKILL.md");
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

// Any argument means the CLI: an unknown command should fail with a message,
// not pop up a window in front of a script or an agent.
bool Cli::isCommand(const QStringList &args) { return args.size() > 1; }

int Cli::run(const QStringList &argv) {
    QStringList args = argv.mid(1);
    const QString command = args.takeFirst();
    const bool json = args.removeAll("--json") > 0;
    const QString path = ListFile::defaultPath();
    MetadataStore metadata;

    if (command == "help" || command == "--help" || command == "-h") {
        out() << kUsage;
        return 0;
    }
    if (command == "path") {
        out() << path << Qt::endl;
        return 0;
    }

    if (command == "list") {
        const bool all = args.removeAll("--all") > 0, readOnly = args.removeAll("--read") > 0;
        if (!args.isEmpty())
            return fail("list takes only --read, --all, and --json");
        PaperList list;
        const QString error = ListFile::load(path, list);
        if (!error.isEmpty())
            return fail(error);
        QJsonArray rows;
        if (!readOnly)
            for (int i = 0; i < list.queue.size(); ++i) {
                const Paper &p = list.queue[i];
                if (json)
                    rows << toJson(p, i + 1, metadata.cached(p.key()));
                else
                    out() << QString::number(i + 1).rightJustified(3) << "  "
                          << (p.title.isEmpty() ? p.url : p.title) << "  [" << p.key() << "]\n";
            }
        if (readOnly || all)
            for (const Paper &p : list.done) {
                if (json)
                    rows << toJson(p, 0, metadata.cached(p.key()));
                else
                    out() << "  ✓  " << p.title << "  [" << p.key() << "] read "
                          << p.read.toString(Qt::ISODate) << "\n";
            }
        if (json)
            printJson(rows);
        out().flush();
        return 0;
    }

    if (command == "add") {
        const bool bottom = args.removeAll("--bottom") > 0;
        // --title names the paper yourself, for a link whose page has no
        // usable title (a bare PDF, a login wall); it skips the fetch.
        QString givenTitle;
        const int titleAt = args.indexOf("--title");
        if (titleAt >= 0) {
            if (titleAt + 1 >= args.size() || args[titleAt + 1].trimmed().isEmpty())
                return fail("--title needs a title");
            givenTitle = args[titleAt + 1].trimmed();
            args.remove(titleAt, 2);
            if (args.size() != 1)
                return fail("--title goes with exactly one link");
        }
        if (args.isEmpty())
            return fail("add needs at least one arXiv link, id, or web address");
        QStringList urls;
        for (const QString &a : args) {
            const QString url = PaperList::canonicalUrl(a);
            if (url.isEmpty())
                return fail(QStringLiteral("not an arXiv link, arXiv id, or web address: %1").arg(a));
            urls << url;
        }
        // Skip what is already listed before touching the network.
        PaperList current;
        QString error = ListFile::load(path, current);
        if (!error.isEmpty())
            return fail(error);
        QStringList toFetch;
        for (const QString &url : urls) {
            Paper probe;
            probe.url = url;
            if (givenTitle.isEmpty() && !current.contains(probe.key()) && !metadata.has(probe.key()))
                toFetch << url;
        }
        const QList<PaperInfo> fetched = metadata.fetchBlocking(toFetch);
        QHash<QString, PaperInfo> byKey;
        for (const PaperInfo &info : fetched)
            byKey.insert(info.key, info);

        int failures = 0;
        QJsonArray rows;
        // Adding to the top one by one would reverse the order given, so walk
        // the arguments backwards when prepending.
        QStringList ordered = urls;
        if (!bottom)
            std::reverse(ordered.begin(), ordered.end());
        QList<QPair<Paper, QString>> outcomes;
        // Fetching can take seconds; re-read so edits made meanwhile survive.
        error = ListFile::load(path, current);
        if (!error.isEmpty())
            return fail(error);
        for (const QString &url : ordered) {
            Paper paper;
            paper.url = url;
            paper.added = QDate::currentDate();
            PaperInfo info = byKey.contains(paper.key()) ? byKey.value(paper.key()) : metadata.cached(paper.key());
            QString problem;
            if (current.contains(paper.key()))
                problem = current.indexOf(paper.key(), false) >= 0 ? "already in the queue" : "already marked read";
            else if (!givenTitle.isEmpty()) {
                paper.title = givenTitle;
                problem = current.add(paper, !bottom);
            } else if (!info.isValid())
                problem = QStringLiteral("could not fetch the title: %1").arg(info.error.isEmpty() ? "unknown error" : info.error);
            else {
                paper.title = info.title;
                problem = current.add(paper, !bottom);
            }
            outcomes << qMakePair(paper, problem);
        }
        error = ListFile::save(path, current);
        if (!error.isEmpty())
            return fail(error);
        if (!bottom)
            std::reverse(outcomes.begin(), outcomes.end());
        for (const auto &[paper, problem] : outcomes) {
            const int position = current.indexOf(paper.key(), false) + 1;
            if (!problem.isEmpty()) {
                ++failures;
                err() << "readlist: " << paper.key() << ": " << problem << Qt::endl;
                continue;
            }
            if (json)
                rows << toJson(paper, position, metadata.cached(paper.key()));
            else
                out() << "added " << QString::number(position) << ": " << paper.title << "  [" << paper.key() << "]\n";
        }
        if (json)
            printJson(rows);
        out().flush();
        return failures ? 1 : 0;
    }

    PaperList list;
    QString error = ListFile::load(path, list);
    if (!error.isEmpty())
        return fail(error);

    if (command == "show" || command == "open") {
        if (args.size() != 1)
            return fail(command + " takes one paper");
        const QString key = resolve(list, args[0], error);
        if (key.isEmpty())
            return fail(error);
        const bool done = list.indexOf(key, false) < 0;
        const Paper paper = done ? list.done[list.indexOf(key, true)] : list.queue[list.indexOf(key, false)];
        if (command == "open") {
            if (!Browser::open(paper.openUrl()))
                return fail("no browser launcher found (omarchy-launch-browser or xdg-open)");
            return 0;
        }
        PaperInfo info = metadata.cached(key);
        if (!info.isValid()) {
            info = metadata.fetchBlocking({paper.url}).value(0);
            if (!info.isValid())
                return fail(QStringLiteral("could not fetch details for %1: %2").arg(key, info.error));
        }
        if (json) {
            QJsonObject o = toJson(paper, done ? 0 : list.indexOf(key, false) + 1, info);
            o["abstract"] = info.abstract;
            printJson(o);
            return 0;
        }
        out() << (paper.title.isEmpty() ? info.title : paper.title) << "\n";
        if (!info.authors.isEmpty())
            out() << info.authors.join(", ") << "\n";
        out() << (info.published.isEmpty() ? QString() : info.published + "  ") << paper.url << "\n\n"
              << (info.abstract.isEmpty() ? QStringLiteral("(no abstract)") : info.abstract) << "\n";
        out().flush();
        return 0;
    }

    if (command == "read" || command == "unread" || command == "remove") {
        if (args.isEmpty())
            return fail(command + " needs at least one paper");
        // Resolve every argument first, so positions refer to the list as it
        // was before this command started moving things.
        QStringList keys;
        for (const QString &a : args) {
            const QString key = resolve(list, a, error);
            if (key.isEmpty())
                return fail(error);
            keys << key;
        }
        return edit(path, [&](PaperList &l) {
            for (const QString &key : keys) {
                const QString problem = command == "read" ? l.markRead(key)
                                      : command == "unread" ? l.markUnread(key) : l.remove(key);
                if (!problem.isEmpty())
                    return key + ": " + problem;
            }
            return QString();
        });
    }

    if (command == "move") {
        if (args.size() != 2)
            return fail("move takes a paper and up, down, top, bottom, or a position");
        const QString key = resolve(list, args[0], error);
        if (key.isEmpty())
            return fail(error);
        return edit(path, [&](PaperList &l) { return l.move(key, args[1]); });
    }

    if (command == "skill") {
        if (args.value(0) != "install")
            return fail("usage: readlist skill install [--dir <skills dir>]");
        QString dir = QDir::homePath() + "/.claude/skills";
        const int at = args.indexOf("--dir");
        if (at >= 0) {
            if (at + 1 >= args.size())
                return fail("--dir needs a directory");
            dir = args[at + 1];
        }
        const QString target = dir + "/readlist/SKILL.md";
        QDir().mkpath(dir + "/readlist");
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(skillText().toUtf8()) < 0 || !file.commit())
            return fail(QStringLiteral("cannot write %1: %2").arg(target, file.errorString()));
        out() << "installed " << target << Qt::endl;
        return 0;
    }

    return fail(QStringLiteral("unknown command %1 (try readlist help)").arg(command));
}

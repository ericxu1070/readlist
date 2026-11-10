#include "paperlist.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const QString kQueue = QStringLiteral("Queue");
const QString kRead = QStringLiteral("Read");

const QRegularExpression &newStyleId() {
    static const QRegularExpression re(R"(^(\d{4}\.\d{4,5})(v\d+)?$)");
    return re;
}
const QRegularExpression &oldStyleId() {
    static const QRegularExpression re(R"(^([a-z][a-z\-]*(?:\.[A-Z]{2})?/\d{7})(v\d+)?$)");
    return re;
}

QString escapeTitle(QString title) {
    title.replace('\\', "\\\\").replace('[', "\\[").replace(']', "\\]");
    return title;
}

QString unescapeTitle(const QString &title) {
    QString out;
    out.reserve(title.size());
    for (int i = 0; i < title.size(); ++i) {
        if (title[i] == '\\' && i + 1 < title.size())
            ++i;
        out += title[i];
    }
    return out;
}

QString trimTrailingBlankLines(QString text) {
    while (text.endsWith('\n') || text.endsWith(' ') || text.endsWith('\t') || text.endsWith('\r'))
        text.chop(1);
    return text;
}

// True when the text is nothing but a YAML frontmatter block, whose closing
// "---" must stay put rather than be read as a section rule.
bool isFrontmatterOnly(const QString &text) {
    if (!text.startsWith(QStringLiteral("---\n")))
        return false;
    const int close = text.indexOf(QStringLiteral("\n---"), 3);
    return close >= 0 && close + 4 == text.size();
}

QString itemLine(const Paper &p, bool done) {
    QString line = QStringLiteral("- [%1] ").arg(done ? "x" : " ");
    if (p.title.isEmpty())
        line += p.url;
    else
        line += QStringLiteral("[%1](%2)").arg(escapeTitle(p.title), p.url);
    QStringList suffix;
    if (p.added.isValid())
        suffix << QStringLiteral("added ") + p.added.toString(Qt::ISODate);
    if (done && p.read.isValid())
        suffix << QStringLiteral("read ") + p.read.toString(Qt::ISODate);
    suffix += p.extra;
    if (!suffix.isEmpty())
        line += QStringLiteral(" · ") + suffix.join(QStringLiteral(" · "));
    return line;
}

// Parses one list line into a paper. Returns false for anything that is not
// a reading-list item, which the caller then keeps verbatim.
bool parseItem(const QString &line, Paper &paper, bool &ticked) {
    static const QRegularExpression linked(
        R"(^\s*[-*]\s+\[( |x|X)\]\s+\[((?:\\.|[^\]\\])*)\]\(([^)\s]+)\)\s*(.*)$)");
    // Also matches the "url · added …" line itemLine() writes for a paper whose title is still pending.
    static const QRegularExpression bare(R"(^\s*[-*]\s+(?:\[( |x|X)\]\s+)?<?([^\s>]+)>?(?:\s+(·.*))?\s*$)");

    QString rest;
    auto m = linked.match(line);
    if (m.hasMatch()) {
        ticked = m.captured(1).compare("x", Qt::CaseInsensitive) == 0;
        paper.title = unescapeTitle(m.captured(2)).trimmed();
        const QString url = PaperList::canonicalUrl(m.captured(3));
        paper.url = url.isEmpty() ? m.captured(3) : url;
        rest = m.captured(4).trimmed();
    } else if ((m = bare.match(line)).hasMatch()) {
        // A bare link or arXiv id typed or pasted by hand; its title is fetched later.
        const QString url = PaperList::canonicalUrl(m.captured(2));
        if (url.isEmpty())
            return false;
        ticked = m.captured(1).compare("x", Qt::CaseInsensitive) == 0;
        paper.url = url;
        rest = m.captured(3).trimmed();
    } else {
        return false;
    }
    if (rest.startsWith(QStringLiteral("·")))
        rest = rest.mid(1);
    for (const QString &raw : rest.split(QStringLiteral("·"), Qt::SkipEmptyParts)) {
        const QString token = raw.trimmed();
        if (token.startsWith(QStringLiteral("added ")))
            paper.added = QDate::fromString(token.mid(6).trimmed(), Qt::ISODate);
        else if (token.startsWith(QStringLiteral("read ")))
            paper.read = QDate::fromString(token.mid(5).trimmed(), Qt::ISODate);
        else if (!token.isEmpty())
            paper.extra << token;
    }
    return true;
}

} // namespace

QString PaperList::arxivIdFromUrl(const QString &url) {
    static const QRegularExpression re(
        R"(^https?://(?:www\.|export\.)?arxiv\.org/(?:abs|pdf)/([^?#]+?)(?:v\d+)?(?:\.pdf)?/?(?:[?#].*)?$)",
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(url);
    if (!m.hasMatch())
        return {};
    const QString id = m.captured(1);
    return newStyleId().match(id).hasMatch() || oldStyleId().match(id).hasMatch() ? id : QString();
}

QString PaperList::canonicalUrl(const QString &input) {
    QString s = input.trimmed();
    if (s.startsWith('<') && s.endsWith('>'))
        s = s.mid(1, s.size() - 2);
    if (s.startsWith(QStringLiteral("arxiv:"), Qt::CaseInsensitive))
        s = s.mid(6).trimmed();

    for (const auto *re : {&newStyleId(), &oldStyleId()}) {
        const auto m = re->match(s);
        if (m.hasMatch())
            return QStringLiteral("https://arxiv.org/abs/") + m.captured(1);
    }
    if (s.startsWith(QStringLiteral("arxiv.org/"), Qt::CaseInsensitive)
        || s.startsWith(QStringLiteral("www.arxiv.org/"), Qt::CaseInsensitive))
        s.prepend(QStringLiteral("https://"));
    if (!s.startsWith(QStringLiteral("http://")) && !s.startsWith(QStringLiteral("https://")))
        return {};
    if (s.contains(' '))
        return {};
    const QString id = arxivIdFromUrl(s);
    return id.isEmpty() ? s : QStringLiteral("https://arxiv.org/abs/") + id;
}

QString Paper::arxivId() const { return PaperList::arxivIdFromUrl(url); }

QString Paper::key() const {
    const QString id = arxivId();
    return id.isEmpty() ? url : id;
}

QString Paper::openUrl() const {
    const QString id = arxivId();
    return id.isEmpty() ? url : QStringLiteral("https://arxiv.org/pdf/") + id;
}

PaperList PaperList::parse(const QString &text) {
    PaperList list;
    const QStringList lines = text.split('\n');
    static const QRegularExpression header(R"(^##\s+(.+?)\s*$)");

    enum { Pre, InQueue, InDone, Post } state = Pre;
    QStringList pre, post;
    for (const QString &rawLine : lines) {
        QString line = rawLine;
        if (line.endsWith('\r'))
            line.chop(1);

        if (state == Post) {
            post << line;
            continue;
        }
        const auto h = header.match(line);
        if (h.hasMatch()) {
            const QString name = h.captured(1);
            if (name.compare(kQueue, Qt::CaseInsensitive) == 0) {
                state = InQueue;
                list.hadSections = true;
                continue;
            }
            if (name.compare(kRead, Qt::CaseInsensitive) == 0) {
                state = InDone;
                list.hadSections = true;
                continue;
            }
            if (state != Pre) {
                state = Post;
                post << line;
                continue;
            }
        }
        if (state == Pre) {
            pre << line;
            continue;
        }

        // Inside Queue or Read: blank lines and rules are layout, which
        // serialize() writes back itself.
        // "_Empty._" is the placeholder serialize() writes for an empty queue.
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed == QStringLiteral("---") || trimmed == QStringLiteral("_Empty._"))
            continue;
        Paper paper;
        bool ticked = false;
        if (parseItem(line, paper, ticked)) {
            if (state == InQueue) {
                paper.ticked = ticked;
                list.queue << paper;
            } else {
                list.done << paper;
            }
        } else {
            (state == InQueue ? list.queueLoose : list.doneLoose) << line;
        }
    }

    list.preamble = trimTrailingBlankLines(pre.join('\n'));
    // A rule just above "## Queue" belongs to the layout, not the preamble,
    // unless it closes the frontmatter.
    if (list.hadSections && list.preamble.endsWith(QStringLiteral("\n---")) && !isFrontmatterOnly(list.preamble))
        list.preamble = trimTrailingBlankLines(list.preamble.chopped(4));
    list.postamble = trimTrailingBlankLines(post.join('\n'));
    return list;
}

bool PaperList::normalize(const QDate &today) {
    bool changed = false;
    for (int i = queue.size() - 1; i >= 0; --i) {
        if (queue[i].ticked) {
            Paper p = queue.takeAt(i);
            p.ticked = false;
            p.read = today;
            done.prepend(p);
            changed = true;
        }
    }
    return changed;
}

QString PaperList::serialize() const {
    QString out;
    if (!preamble.isEmpty()) {
        out += preamble;
        out += isFrontmatterOnly(preamble) ? QStringLiteral("\n\n") : QStringLiteral("\n\n---\n\n");
    }

    out += QStringLiteral("## Queue\n\n");
    for (const Paper &p : queue)
        out += itemLine(p, false) + '\n';
    for (const QString &l : queueLoose)
        out += l + '\n';
    if (queue.isEmpty() && queueLoose.isEmpty())
        out += QStringLiteral("_Empty._\n");

    out += QStringLiteral("\n---\n\n## Read\n\n");
    for (const Paper &p : done)
        out += itemLine(p, true) + '\n';
    for (const QString &l : doneLoose)
        out += l + '\n';

    if (!postamble.isEmpty())
        out += QStringLiteral("\n---\n\n") + postamble + '\n';
    return out;
}

int PaperList::indexOf(const QString &key, bool inDone) const {
    const QList<Paper> &l = inDone ? done : queue;
    QString wanted = key;
    // Let callers pass a url or an id interchangeably.
    const QString canonical = canonicalUrl(key);
    if (!canonical.isEmpty()) {
        const QString id = arxivIdFromUrl(canonical);
        wanted = id.isEmpty() ? canonical : id;
    }
    for (int i = 0; i < l.size(); ++i)
        if (l[i].key() == wanted || l[i].url == key)
            return i;
    return -1;
}

QString PaperList::add(const Paper &paper, bool atTop) {
    if (indexOf(paper.key(), false) >= 0)
        return QStringLiteral("already in the queue");
    if (indexOf(paper.key(), true) >= 0)
        return QStringLiteral("already marked read (use unread to queue it again)");
    if (atTop)
        queue.prepend(paper);
    else
        queue.append(paper);
    return {};
}

QString PaperList::markRead(const QString &key, const QDate &today) {
    const int i = indexOf(key, false);
    if (i < 0)
        return indexOf(key, true) >= 0 ? QStringLiteral("already marked read") : QStringLiteral("not in the list");
    Paper p = queue.takeAt(i);
    p.read = today;
    done.prepend(p);
    return {};
}

QString PaperList::markUnread(const QString &key) {
    const int i = indexOf(key, true);
    if (i < 0)
        return indexOf(key, false) >= 0 ? QStringLiteral("already in the queue") : QStringLiteral("not in the list");
    Paper p = done.takeAt(i);
    p.read = QDate();
    queue.prepend(p);
    return {};
}

QString PaperList::remove(const QString &key) {
    int i = indexOf(key, false);
    if (i >= 0) {
        queue.removeAt(i);
        return {};
    }
    i = indexOf(key, true);
    if (i >= 0) {
        done.removeAt(i);
        return {};
    }
    return QStringLiteral("not in the list");
}

QString PaperList::move(const QString &key, const QString &where) {
    const int i = indexOf(key, false);
    if (i < 0)
        return QStringLiteral("not in the queue");
    int target;
    if (where == QStringLiteral("up"))
        target = i - 1;
    else if (where == QStringLiteral("down"))
        target = i + 1;
    else if (where == QStringLiteral("top"))
        target = 0;
    else if (where == QStringLiteral("bottom"))
        target = queue.size() - 1;
    else {
        bool ok = false;
        target = where.toInt(&ok) - 1;
        if (!ok)
            return QStringLiteral("position must be up, down, top, bottom, or a number");
    }
    target = std::clamp(target, 0, int(queue.size()) - 1);
    if (target != i)
        queue.move(i, target);
    return {};
}

namespace ListFile {

QString defaultPath() {
    if (!qEnvironmentVariableIsEmpty("READLIST_FILE"))
        return qEnvironmentVariable("READLIST_FILE");
    // ~/.config/readlist/path holds the note's location, so the list can live
    // in a synced notes folder; without it the list stays in the app's data dir.
    QFile config(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/readlist/path");
    if (config.open(QIODevice::ReadOnly)) {
        QString path = QString::fromUtf8(config.readLine()).trimmed();
        if (path.startsWith(QStringLiteral("~/")))
            path = QDir::homePath() + path.mid(1);
        if (!path.isEmpty())
            return path;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/readlist/Reading List.md";
}

QString readText(const QString &path, QString &text) {
    QFile file(path);
    if (!file.exists()) {
        text = QStringLiteral("# Reading List\n");
        return {};
    }
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
    text = QString::fromUtf8(file.readAll());
    return {};
}

QString writeText(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
    file.write(text.toUtf8());
    if (!file.commit())
        return QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
    return {};
}

QString load(const QString &path, PaperList &list) {
    QString text;
    const QString error = readText(path, text);
    if (!error.isEmpty())
        return error;
    list = PaperList::parse(text);
    list.normalize();
    return {};
}

QString save(const QString &path, const PaperList &list) { return writeText(path, list.serialize()); }

} // namespace ListFile

bool Browser::open(const QString &url) {
    // Launched from the app menu, PATH may not include Omarchy's bin.
    QStringList dirs = qEnvironmentVariable("PATH").split(':', Qt::SkipEmptyParts);
    if (!qEnvironmentVariableIsEmpty("OMARCHY_PATH"))
        dirs << qEnvironmentVariable("OMARCHY_PATH") + "/bin";
    dirs << QDir::homePath() + "/.local/share/omarchy/bin" << "/usr/share/omarchy/bin";
    QString program = QStandardPaths::findExecutable("omarchy-launch-browser", dirs);
    if (program.isEmpty())
        program = QStandardPaths::findExecutable("xdg-open");
    return !program.isEmpty() && QProcess::startDetached(program, {url});
}

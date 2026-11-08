#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "metadata.h"
#include "paperlist.h"
#include "theme.h"
#include "translator.h"

namespace {
void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}
QString read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
Paper paper(const QString &id, const QString &title) {
    Paper p;
    p.url = "https://arxiv.org/abs/" + id;
    p.title = title;
    return p;
}
// Plain literals throughout: moc misreads multi-line raw strings and then
// skips the test class.
const char *kNote =
    "---\n"
    "created: 2026-11-08\n"
    "tags:\n"
    "  - reading-list\n"
    "status: active\n"
    "---\n"
    "\n"
    "# Reading List\n"
    "\n"
    "Order is priority.\n"
    "\n"
    "---\n"
    "\n"
    "## Queue\n"
    "\n"
    "- [ ] [SHAC: Accelerated \\[Policy\\] Learning](https://arxiv.org/abs/2204.07137) · added 2026-11-01\n"
    "- [ ] https://arxiv.org/pdf/2311.07222v2\n"
    "- [x] [Ticked in Obsidian](https://arxiv.org/abs/2503.06623)\n"
    "A stray thought typed by hand.\n"
    "\n"
    "---\n"
    "\n"
    "## Read\n"
    "\n"
    "- [x] [Old](https://example.com/post) · added 2026-10-01 · read 2026-10-02 · great\n"
    "\n"
    "---\n"
    "\n"
    "## Notes\n"
    "\n"
    "Kept as is.\n";
} // namespace

class ReadlistTests : public QObject {
    Q_OBJECT

private slots:
    void canonicalisesArxivForms() {
        const QString abs = "https://arxiv.org/abs/2204.07137";
        QCOMPARE(PaperList::canonicalUrl("2204.07137"), abs);
        QCOMPARE(PaperList::canonicalUrl(" arXiv:2204.07137v3 "), abs);
        QCOMPARE(PaperList::canonicalUrl("https://arxiv.org/pdf/2204.07137v1.pdf"), abs);
        QCOMPARE(PaperList::canonicalUrl("http://www.arxiv.org/abs/2204.07137"), abs);
        QCOMPARE(PaperList::canonicalUrl("arxiv.org/abs/2204.07137"), abs);
        QCOMPARE(PaperList::canonicalUrl("hep-th/9901001v2"), QString("https://arxiv.org/abs/hep-th/9901001"));
        QCOMPARE(PaperList::canonicalUrl("https://example.com/a"), QString("https://example.com/a"));
        QVERIFY(PaperList::canonicalUrl("not a link").isEmpty());
        QVERIFY(PaperList::canonicalUrl("1234").isEmpty());
        QCOMPARE(paper("2204.07137", "x").openUrl(), QString("https://arxiv.org/pdf/2204.07137"));
    }

    void parsesAndKeepsHandEdits() {
        PaperList list = PaperList::parse(kNote);
        QCOMPARE(list.queue.size(), 3);
        QCOMPARE(list.queue[0].title, QString("SHAC: Accelerated [Policy] Learning"));
        QCOMPARE(list.queue[0].added, QDate(2026, 11, 1));
        QVERIFY(list.queue[1].needsTitle());
        QCOMPARE(list.queue[1].key(), QString("2311.07222"));
        QCOMPARE(list.queueLoose, QStringList{"A stray thought typed by hand."});
        QCOMPARE(list.done.size(), 1);
        QCOMPARE(list.done[0].extra, QStringList{"great"});

        QVERIFY(list.normalize(QDate(2026, 11, 8)));
        QCOMPARE(list.queue.size(), 2);
        QCOMPARE(list.done[0].key(), QString("2503.06623"));
        QCOMPARE(list.done[0].read, QDate(2026, 11, 8));

        const QString out = list.serialize();
        QVERIFY(out.startsWith("---\ncreated: 2026-11-08\n"));
        QVERIFY(out.contains("Order is priority.\n\n---\n\n## Queue\n\n- [ ] [SHAC: Accelerated \\[Policy\\] Learning]"));
        QVERIFY(out.contains("- [ ] https://arxiv.org/abs/2311.07222\n"));
        QVERIFY(out.contains("A stray thought typed by hand."));
        QVERIFY(out.contains("· read 2026-10-02 · great"));
        QVERIFY(out.contains("## Notes\n\nKept as is.\n"));
        // Serializing is stable: a second round trip changes nothing.
        QCOMPARE(PaperList::parse(out).serialize(), out);
    }

    void frontmatterOnlyPreambleKeepsItsClosingRule() {
        PaperList list = PaperList::parse("---\ntags:\n  - a\n---\n");
        list.add(paper("2204.07137", "A"), true);
        const QString out = list.serialize();
        QVERIFY(out.startsWith("---\ntags:\n  - a\n---\n\n## Queue\n"));
        QCOMPARE(PaperList::parse(out).serialize(), out);
    }

    void editsTheQueue() {
        PaperList list;
        for (const auto &[id, t] : {std::pair{"1111.11111", "A"}, {"2222.22222", "B"}, {"3333.33333", "C"}})
            QVERIFY(list.add(paper(id, t), false).isEmpty());
        QVERIFY(!list.add(paper("2222.22222", "dup"), true).isEmpty());
        QVERIFY(list.move("3333.33333", "top").isEmpty());
        QCOMPARE(list.queue[0].title, QString("C"));
        QVERIFY(list.move("3333.33333", "down").isEmpty());
        QCOMPARE(list.queue[1].title, QString("C"));
        QVERIFY(list.move("1111.11111", "bottom").isEmpty());
        QCOMPARE(list.queue[2].title, QString("A"));
        QVERIFY(list.move("https://arxiv.org/pdf/1111.11111v2", "1").isEmpty());
        QCOMPARE(list.queue[0].title, QString("A"));
        QVERIFY(list.move("1111.11111", "up").isEmpty()); // already first: a no-op
        QVERIFY(list.markRead("2222.22222", QDate(2026, 1, 2)).isEmpty());
        QCOMPARE(list.done[0].read, QDate(2026, 1, 2));
        QVERIFY(!list.add(paper("2222.22222", "again"), true).isEmpty());
        QVERIFY(list.markUnread("2222.22222").isEmpty());
        QCOMPARE(list.queue[0].title, QString("B"));
        QVERIFY(list.remove("2222.22222").isEmpty());
        QCOMPARE(list.queue.size(), 2);
        QVERIFY(!list.remove("2222.22222").isEmpty());
    }

    void parsesArxivFeed() {
        const QByteArray xml = "<?xml version=\"1.0\"?>\n"
            "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
            "  <entry>\n"
            "    <id>http://arxiv.org/abs/2204.07137v1</id>\n"
            "    <published>2022-04-14T17:59:00Z</published>\n"
            "    <title>Accelerated Policy Learning\n"
            "      with Parallel Differentiable Simulation</title>\n"
            "    <summary>  Deep RL can   be slow.\n"
            "    </summary>\n"
            "    <author><name>Jie Xu</name></author>\n"
            "    <author><name>Miles Macklin</name></author>\n"
            "  </entry>\n"
            "  <entry><id>http://arxiv.org/api/errors#x</id><title>Error</title></entry>\n"
            "</feed>";
        const auto infos = MetadataStore::parseArxivFeed(xml);
        QCOMPARE(infos.size(), 1);
        QCOMPARE(infos[0].key, QString("2204.07137"));
        QCOMPARE(infos[0].title, QString("Accelerated Policy Learning with Parallel Differentiable Simulation"));
        QCOMPARE(infos[0].abstract, QString("Deep RL can be slow."));
        QCOMPARE(infos[0].authors, (QStringList{"Jie Xu", "Miles Macklin"}));
        QCOMPARE(infos[0].published, QString("2022-04-14"));
    }

    void parsesPageMeta() {
        const auto info = MetadataStore::parseHtml(
            "<html><head><title>ignored</title><meta content=\"Fast &amp; Small | Blog\" property=\"og:title\">\n"
            "               <meta name=\"description\" content=\"A post.\"></head></html>", "https://example.com/p");
        QCOMPARE(info.title, QString("Fast & Small"));
        QCOMPARE(info.abstract, QString("A post."));
        QVERIFY(info.isValid());
    }

    void cachesMetadata() {
        QTemporaryDir dir;
        {
            MetadataStore store(dir.path());
            PaperInfo info;
            info.key = "2204.07137";
            info.title = "T";
            info.abstract = "A";
            info.authors = {"X Y"};
            store.store(info);
        }
        MetadataStore again(dir.path());
        QVERIFY(again.has("2204.07137"));
        QCOMPARE(again.cached("2204.07137").abstract, QString("A"));
        QVERIFY(!again.has("https://example.com/x"));
    }

    void backendEditsTheNoteAndUndoes() {
        QTemporaryDir dir;
        const QString note = dir.filePath("list.md");
        write(note, "# Reading List\n\n## Queue\n\n- [ ] [A](https://arxiv.org/abs/1111.11111)\n"
                    "- [ ] [B](https://arxiv.org/abs/2222.22222)\n\n## Read\n\n");
        // Pre-cached, so the backend never touches the network.
        MetadataStore cache(dir.filePath("cache"));
        for (const auto &[id, t] : {std::pair{"1111.11111", "A"}, {"2222.22222", "B"}, {"3333.33333", "C"}}) {
            PaperInfo info;
            info.key = id;
            info.title = t;
            info.abstract = "abstract";
            cache.store(info);
        }

        Backend backend(note, dir.filePath("cache"));
        QStringList opened;
        backend.launcher = [&](const QString &url) { opened << url; return true; };
        QCOMPARE(backend.queue().size(), 2);

        backend.move("2222.22222", "top");
        QCOMPARE(backend.queue()[0].toMap()["title"].toString(), QString("B"));
        backend.markRead("1111.11111");
        QCOMPARE(backend.done().size(), 1);
        QVERIFY(read(note).contains("- [x] [A](https://arxiv.org/abs/1111.11111)"));

        backend.add("arXiv:3333.33333v2");
        QCOMPARE(backend.queue()[0].toMap()["title"].toString(), QString("C"));
        QVERIFY(read(note).contains("- [ ] [C](https://arxiv.org/abs/3333.33333) · added "));

        backend.undo();
        QCOMPARE(backend.queue().size(), 1);
        backend.redo();
        QCOMPARE(backend.queue().size(), 2);

        QVERIFY(backend.openPaper("3333.33333"));
        QCOMPARE(opened, QStringList{"https://arxiv.org/pdf/3333.33333"});
        QCOMPARE(backend.info("3333.33333")["abstract"].toString(), QString("abstract"));

        // An outside edit (Obsidian, Syncthing, an agent) shows up live.
        QString text = read(note);
        text.replace("- [ ] [B]", "- [x] [B]");
        write(note, text.toUtf8());
        QTRY_COMPARE(backend.queue().size(), 1);
        QCOMPARE(backend.done().size(), 2);
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsColors() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\nbackground = \"#000001\"\nforeground = '#fafafa'\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.background(), QString("#000001"));
        QCOMPARE(theme.foreground(), QString("#fafafa"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }
};

QTEST_MAIN(ReadlistTests)
#include "readlist_tests.moc"

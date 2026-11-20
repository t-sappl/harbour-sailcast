// SPDX-License-Identifier: GPL-3.0-or-later
// (344) Tests of the show-notes chapter parser (core/shownoteschapters.cpp),
// cases 1-30 of DESIGN-INTERNAL "Plan: Shownotes-Kapitel-Parser".
//
//   cd tools/chapterstest && qmake-qt5 && make && ./chapterstest
//
// Each case: notes, duration (0 = unknown), the expected start times in
// seconds (empty = no chapters) and optionally expected titles.
#include <QCoreApplication>
#include <QTextStream>

#include "shownoteschapters.h"

struct Case {
    const char *name;
    QString notes;
    int duration;
    QList<int> starts;
    QStringList titles;
};

static QString L(std::initializer_list<const char *> lines)
{
    QStringList list;
    for (const char *l : lines) {
        list << QString::fromUtf8(l);
    }
    return list.join(QLatin1Char('\n'));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    out.setCodec("UTF-8");
    const QList<Case> cases = {
        {"1 plain list", L({"00:00 Intro", "05:20 News", "12:45 Interview", "58:10 Outro"}), 0, {0, 320, 765, 3490}, {"Intro", "News", "Interview", "Outro"}},
        {"2 numbered, brackets, hour", L({"1. (00:00) Begrüßung", "2. (03:15) Thema", "3. (1:02:30) Fragen"}), 0, {0, 195, 3750}, {"Begrüßung", "Thema", "Fragen"}},
        {"3 html br", QStringLiteral("<p>00:00 Start<br>04:10 Teil eins<br>15:00 Teil zwei</p>"), 0, {0, 250, 900}, {}},
        {"4 one line with |", L({"00:00 Intro | 05:20 News | 31:00 Talk"}), 0, {0, 320, 1860}, {"Intro", "News", "Talk"}},
        {"5 suffix", L({"Begrüßung (00:00)", "Rückblick [07:40]", "Ausblick – 25:10"}), 0, {0, 460, 1510}, {"Begrüßung", "Rückblick", "Ausblick"}},
        {"6 from-to", L({"00:00 - 05:30 Intro", "05:30 - 20:00 Hauptteil", "20:00 - 41:00 Fragen"}), 0, {0, 330, 1200}, {"Intro", "Hauptteil", "Fragen"}},
        {"7 blocked", L({"Live ab 20:15 Uhr", "Sendung am 12.03.", "Dauer 45:00 min", "um 8:30 AM"}), 0, {}, {}},
        {"8 list repeated", L({"00:00 A eins", "10:00 B zwei", "20:00 C drei", "00:00 A eins", "10:00 B zwei"}), 0, {0, 600, 1200}, {}},
        {"9 fewer than 3", L({"00:00 Intro", "10:00 Ende"}), 0, {}, {}},
        {"10 first after 10 min", L({"12:00 Eins", "20:00 Zwei", "30:00 Drei"}), 0, {}, {}},
        {"11 prose", L({"Wir sprechen ab 12:30 über das Wetter und treffen uns um 20:15."}), 0, {}, {}},
        {"12 bad titles", L({"00:00 Start", "00:30 –", "05:00 https://example.org", "10:00 42", "11:00 Weiter", "20:00 Ende"}), 0, {0, 660, 1200}, {}},
        {"13 bad stamps", L({"2:1 Falsch", "10:61 Falsch", "00:00 Gut", "01:00 Gut zwei"}), 0, {}, {}},
        {"14 after the end", L({"00:00 A eins", "10:00 B zwei", "20:00 C drei", "45:00 D vier"}), 1800, {0, 600, 1200}, {}},
        {"15 start group wins", L({"00:00 Eins", "05:00 Zwei", "10:00 Drei", "15:00 Vier", "Bonus (20:00)"}), 0, {0, 300, 600, 900}, {}},
        {"16 title: stamp", L({"Begrüßung: 00:00", "Hauptthema: 12:30", "Fazit: 45:10"}), 0, {0, 750, 2710}, {"Begrüßung", "Hauptthema", "Fazit"}},
        {"17 html inline tags", QStringLiteral("<p><strong>00:00</strong> - Intro<br><a href='#'>05:20</a> - News<br><b>18:40</b> - Outro</p>"), 0, {0, 320, 1120}, {"Intro", "News", "Outro"}},
        {"18 milliseconds", L({"00:00:00.000 Intro", "00:12:34.500 Hauptteil", "00:45:10,200 Ende"}), 0, {0, 754, 2710}, {}},
        {"19 from-to at the end", L({"Intro (00:00 - 04:15)", "News (04:15 - 18:30)", "Thema (18:30 - 42:00)"}), 0, {0, 255, 1110}, {"Intro", "News", "Thema"}},
        {"20 numbered 1)", L({"1) 00:00 Start", "2) 08:15 Mitte", "3) 22:40 Schluss"}), 0, {0, 495, 1360}, {"Start", "Mitte", "Schluss"}},
        {"21 stray typo", L({"00:00 Intro", "55:00 Tippfehler", "05:00 News", "15:00 Hauptteil", "30:00 Fazit"}), 0, {0, 300, 900, 1800}, {}},
        {"22 | inside a title", L({"00:00 Intro | 10:00 Talk mit Anna | Autorin | 25:00 Ende"}), 0, {0, 600, 1500}, {"Intro", "Talk mit Anna | Autorin", "Ende"}},
        {"23 short titles", L({"00:00 Intro", "10:00 F1 & MotoGP", "25:00 Q&A"}), 0, {0, 600, 1500}, {"Intro", "F1 & MotoGP", "Q&A"}},
        {"24 html list", QStringLiteral("<p><b>00:00</b> – Start</p><ul><li>[05:30] Thema 1</li><li>[15:45] Thema 2</li></ul>"), 0, {0, 330, 945}, {"Start", "Thema 1", "Thema 2"}},
        {"25 Kapitel n:", L({"Kapitel 1: (00:00) - Intro", "Kapitel 2: (08:30) - Analyse", "Kapitel 3: (22:15) - Ausblick"}), 0, {0, 510, 1335}, {"Intro", "Analyse", "Ausblick"}},
        {"26 HH:MM:SS", L({"00:00:00 Intro", "00:09:30 Teil 1", "01:00:05 Teil 2"}), 0, {0, 570, 3605}, {}},
        {"27 long minutes", L({"00:00 Start", "65:30 Mitte", "120:00 Überlänge Teil 1"}), 0, {0, 3930, 7200}, {}},
        {"28 url-only line", L({"00:00 Start", "05:00 https://kurzlink.de/abc", "10:00 Weiter", "20:00 Ende"}), 0, {0, 600, 1200}, {}},
        {"29 stamp inside a title", L({"Intro - 00:00", "Thema 20:15 - Warum wir scheitern - 08:30", "Fazit - 25:00"}), 0, {0, 510, 1500}, {"Intro", "Thema 20:15 - Warum wir scheitern", "Fazit"}},
        {"30 from-to over the end", L({"00:00 Start", "10:00 Mitte", "29:59 - 35:00 Abspann"}), 1800, {0, 600, 1799}, {}},
        // (344, review) cases found by the reviewing agent
        {"31 emoji bullets", L({"🎙️ 00:00 Intro", "▶️ 05:00 News", "✅ 10:00 Talk"}), 0, {0, 300, 600}, {"Intro", "News", "Talk"}},
        {"32 plus bullets", L({"+ 00:00 Eins", "+ 05:00 Zwei", "+ 10:00 Drei"}), 0, {0, 300, 600}, {}},
        {"33 markdown link on the stamp", L({"[00:00](https://youtu.be/x?t=0) Intro", "[05:00](https://youtu.be/x?t=300) News", "[10:00](https://youtu.be/x?t=600) Talk"}), 0, {0, 300, 600}, {"Intro", "News", "Talk"}},
        {"34 markdown link on the line", L({"* [00:00 Intro](https://y.be/a)", "* [05:00 News](https://y.be/b)", "* [10:00 Talk](https://y.be/c)"}), 0, {0, 300, 600}, {"Intro", "News", "Talk"}},
        {"35 h + umlaut title", L({"00:00 Start", "05:00 hörspiel teil", "10:00 hübsch"}), 0, {0, 300, 600}, {"Start", "hörspiel teil", "hübsch"}},
        {"36 thin space before Uhr", L({"00:00 A eins", "10:00\u2009Uhr Start", "20:00 C drei", "30:00 D vier"}), 0, {0, 1200, 1800}, {}},
        {"37 thin spaces around the dash", L({"Intro\u2009–\u200900:00", "News\u2009–\u200905:00", "Talk\u2009–\u200910:00"}), 0, {0, 300, 600}, {"Intro", "News", "Talk"}},
        {"38 named entities", QStringLiteral("<p>00:00 Begr&uuml;&szlig;ung<br>05:00 Teil&hellip;<br>10:00 &bdquo;Ende&ldquo; &#X2013; Schluss</p>"), 0, {0, 300, 600}, {QString::fromUtf8("Begrüßung"), QString::fromUtf8("Teil…"), QString::fromUtf8("„Ende“ – Schluss")}},
        {"39 entity decoded once", QStringLiteral("00:00 A &#38;lt; B\n05:00 Zwei\n10:00 Drei"), 0, {0, 300, 600}, {QStringLiteral("A &lt; B"), QStringLiteral("Zwei"), QStringLiteral("Drei")}},
        {"40 < and > in plain text", L({"00:00 Intro <3", "05:00 News > alles", "10:00 Ende"}), 0, {0, 300, 600}, {}},
        {"41 | after stamps in brackets", L({"Intro (00:00) | News (05:20) | Talk (31:00)"}), 0, {0, 320, 1860}, {"Intro", "News", "Talk"}},
        {"42 stamp at both ends", L({"00:00 Intro (00:30)", "05:00 News (05:30)", "10:00 Talk (10:30)"}), 0, {0, 300, 600}, {"Intro", "News", "Talk"}},
        {"43 long run of spaces", QStringLiteral("Intro") + QString(5000, QLatin1Char(' ')) + QStringLiteral("x - 00:00\nNews - 05:00\nTalk - 10:00"), 0, {0, 300, 600}, {}},
    };
    int failed = 0;
    for (const Case &c : cases) {
        QStringList log;
        const QList<ParsedChapter> got = ShownotesChapters::parse(c.notes, c.duration, &log);
        QList<int> starts;
        QStringList titles;
        for (const ParsedChapter &ch : got) {
            starts << int(ch.startMs / 1000);
            titles << ch.title;
        }
        const bool ok = starts == c.starts && (c.titles.isEmpty() || titles == c.titles);
        if (!ok) {
            ++failed;
            out << "FAIL " << c.name << "\n";
            QStringList want;
            for (int s : c.starts) {
                want << QString::number(s);
            }
            QStringList have;
            for (int i = 0; i < starts.size(); ++i) {
                have << QString::number(starts.at(i)) + QLatin1Char('=') + titles.at(i);
            }
            out << "  expected: " << want.join(QStringLiteral(", ")) << (c.titles.isEmpty() ? QString() : QStringLiteral("  ") + c.titles.join(QStringLiteral(" / "))) << "\n";
            out << "  got:      " << have.join(QStringLiteral(", ")) << "\n";
            out << "  log:      " << log.join(QStringLiteral("; ")) << "\n";
        } else {
            out << "ok   " << c.name << "\n";
        }
    }
    out << (failed == 0 ? QStringLiteral("all %1 cases passed").arg(cases.size())
                        : QStringLiteral("%1 of %2 cases failed").arg(failed).arg(cases.size())) << "\n";
    return failed == 0 ? 0 : 1;
}

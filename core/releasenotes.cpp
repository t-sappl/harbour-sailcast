// SPDX-License-Identifier: GPL-3.0-or-later
#include "releasenotes.h"

#include <QRegularExpression>
#include <QStringList>

namespace ReleaseNotes {

QList<int> parseVersion(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
    const QRegularExpressionMatch m = re.match(text);
    QList<int> result;
    if (!m.hasMatch()) {
        return result;
    }
    result << m.captured(1).toInt() << m.captured(2).toInt()
           << (m.captured(3).isEmpty() ? 0 : m.captured(3).toInt());
    return result;
}

int compareVersions(const QString &a, const QString &b)
{
    const QList<int> va = parseVersion(a);
    const QList<int> vb = parseVersion(b);
    if (va.isEmpty() || vb.isEmpty()) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        if (va.at(i) != vb.at(i)) {
            return va.at(i) < vb.at(i) ? -1 : 1;
        }
    }
    return 0;
}

static QString inline_(QString line)
{
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\]\\([^)]*\\)"));
    line.replace(link, QStringLiteral("\\1"));
    line.remove(QLatin1String("**"));
    line.remove(QLatin1Char('`'));
    return line;
}

QList<Block> blocks(const QString &markdown)
{
    QList<Block> result;
    bool skipping = false;      // inside the section "Install"
    bool joinable = false;      // the last block may take a wrapped continuation
    const QStringList raw = markdown.split(QLatin1Char('\n'));
    static const QRegularExpression numbered(QStringLiteral("^\\d+[.)]\\s"));
    auto blank = [&result]() {
        if (!result.isEmpty() && result.last().kind != Block::Blank) {
            result << Block { Block::Blank, QString() };
        }
    };
    for (int i = 0; i < raw.size(); ++i) {
        const QString untrimmed = raw.at(i);
        const QString line = untrimmed.trimmed();
        int indent = 0;
        while (indent < untrimmed.size() && untrimmed.at(indent) == QLatin1Char(' ')) {
            ++indent;
        }
        if (line.startsWith(QLatin1Char('#'))) {
            QString title = line;
            while (title.startsWith(QLatin1Char('#'))) {
                title.remove(0, 1);
            }
            title = inline_(title.trimmed());
            // (review 347) Only the section "Install" itself
            skipping = title.compare(QLatin1String("Install"), Qt::CaseInsensitive) == 0;
            joinable = false;
            if (skipping || title.isEmpty()) {
                continue;
            }
            blank();
            result << Block { Block::Heading, title };
            continue;
        }
        if (skipping) {
            continue;
        }
        if (line.isEmpty()) {
            joinable = false;
            blank();
            continue;
        }
        if (line == QLatin1String("---") || line.startsWith(QLatin1String("Developed by"))
                || line.startsWith(QLatin1String("**Full Changelog")) || line.startsWith(QLatin1String("Full Changelog"))) {
            joinable = false;
            continue;
        }
        if (line.startsWith(QLatin1String("- ")) || line.startsWith(QLatin1String("* "))
                || line.startsWith(QLatin1String("+ "))) {
            // (review 347) A nested item keeps its level
            result << Block { indent >= 2 ? Block::SubItem : Block::Item, inline_(line.mid(2).trimmed()) };
            joinable = true;
        } else if (numbered.match(line).hasMatch()) {
            result << Block { Block::Numbered, inline_(line) };    // a numbered item starts a line of its own
            joinable = true;
        } else if (joinable && !result.isEmpty() && result.last().kind != Block::Heading
                   && result.last().kind != Block::Blank) {
            result.last().text += QLatin1Char(' ') + inline_(line);    // wrapped line
        } else {
            const QString text = inline_(line);
            if (text.isEmpty()) {
                // (review 351) As the old plain(): counts as an empty line
                joinable = false;
                blank();
                continue;
            }
            result << Block { Block::Paragraph, text };
            joinable = true;
        }
    }
    while (!result.isEmpty() && result.first().kind == Block::Blank) {
        result.removeFirst();
    }
    while (!result.isEmpty() && result.last().kind == Block::Blank) {
        result.removeLast();
    }
    return result;
}

QString localized(const QString &markdown, bool german)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QStringList outside;
    QStringList inside;
    bool found = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines.at(i).trimmed();
        // "<details>" with "<summary>Deutsch</summary>" on the same or the next line
        if (!found && t.startsWith(QLatin1String("<details"))) {
            const bool sameLine = t.contains(QLatin1String("<summary>Deutsch</summary>"));
            const bool nextLine = i + 1 < lines.size()
                                  && lines.at(i + 1).trimmed() == QLatin1String("<summary>Deutsch</summary>");
            if (sameLine || nextLine) {
                found = true;
                int j = i + (nextLine ? 2 : 1);
                for (; j < lines.size() && lines.at(j).trimmed() != QLatin1String("</details>"); ++j) {
                    inside << lines.at(j);
                }
                i = j;    // the "</details>" line (or the end)
                continue;
            }
        }
        outside << lines.at(i);
    }
    if (german && found) {
        return inside.join(QLatin1Char('\n'));
    }
    return outside.join(QLatin1Char('\n'));
}

QString plain(const QString &markdown)
{
    QStringList lines;
    for (const Block &block : blocks(markdown)) {
        switch (block.kind) {
        case Block::Blank:
            lines << QString();
            break;
        case Block::Heading:
            // A heading right after text gets its blank line (blocks() has it)
            lines << block.text;
            break;
        case Block::Item:
            lines << QString::fromUtf8("\xE2\x80\xA2 ") + block.text;
            break;
        case Block::SubItem:
            lines << QString::fromUtf8("   \xE2\x97\xA6 ") + block.text;
            break;
        case Block::Numbered:
        case Block::Paragraph:
            lines << block.text;
            break;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QList<Section> sections(const QString &changelog)
{
    static const QRegularExpression head(QStringLiteral("^##\\s+v?(\\d+\\.\\d+(?:\\.\\d+)?)\\s*(?:\\((\\d{4}-\\d{2}-\\d{2})\\))?"));
    QList<Section> result;
    const QStringList raw = changelog.split(QLatin1Char('\n'));
    Section current;
    QStringList body;
    bool inSection = false;
    for (int i = 0; i <= raw.size(); ++i) {
        const bool end = i == raw.size();
        const QString line = end ? QString() : raw.at(i);
        const bool level2 = !end && line.startsWith(QLatin1String("## "));
        if (end || level2) {
            if (inSection) {
                const QString markdown = body.join(QLatin1Char('\n'));
                current.text = plain(markdown);
                current.blocks = blocks(markdown);
                result << current;
            }
            inSection = false;
            body.clear();
            if (level2) {
                const QRegularExpressionMatch m = head.match(line);
                if (m.hasMatch()) {
                    current = Section();
                    current.version = m.captured(1);
                    current.date = m.captured(2);
                    inSection = true;
                }
            }
            continue;
        }
        if (inSection) {
            body << line;
        }
    }
    return result;
}

} // namespace ReleaseNotes

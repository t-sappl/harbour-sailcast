// SPDX-License-Identifier: GPL-3.0-or-later
#include "opml.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QStringList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace Opml {

QList<OpmlEntry> read(const QString &path, QString *error)
{
    QList<OpmlEntry> entries;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return entries;
    }

    QXmlStreamReader xml(&file);
    // Open outlines: their folder name if they are folders (no xmlUrl)
    QStringList stack;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("outline")) {
            if (!stack.isEmpty()) {
                stack.removeLast();
            }
            continue;
        }
        if (!xml.isStartElement() || xml.name() != QLatin1String("outline")) {
            continue;
        }
        // Attribute names are case-sensitive in XML, but some exporters
        // write "xmlurl" or "XMLURL"
        OpmlEntry entry;
        const QXmlStreamAttributes attrs = xml.attributes();
        for (int i = 0; i < attrs.size(); ++i) {
            const QString name = attrs.at(i).name().toString().toLower();
            const QString value = attrs.at(i).value().toString().trimmed();
            if (name == QLatin1String("xmlurl")) {
                entry.url = value;
            } else if (name == QLatin1String("htmlurl")) {
                entry.htmlUrl = value;
            } else if (name == QLatin1String("text")) {
                entry.title = value;
            } else if (name == QLatin1String("title") && entry.title.isEmpty()) {
                entry.title = value;
            }
        }
        // The outermost enclosing folder
        for (int i = 0; i < stack.size(); ++i) {
            if (!stack.at(i).isEmpty()) {
                entry.folder = stack.at(i);
                break;
            }
        }
        stack.append(entry.url.isEmpty() ? entry.title : QString());
        if (!entry.url.isEmpty()) {
            entries.append(entry);
        }
    }
    if (xml.hasError() && error) {
        *error = QStringLiteral("XML error in line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
    }
    return entries;
}

bool write(const QString &path, const QList<OpmlEntry> &entries, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("opml"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("2.0"));
    xml.writeStartElement(QStringLiteral("head"));
    xml.writeTextElement(QStringLiteral("title"), QStringLiteral("Sailcast subscriptions"));
    xml.writeTextElement(QStringLiteral("dateCreated"),
                         QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date));
    xml.writeEndElement(); // head
    xml.writeStartElement(QStringLiteral("body"));
    // Feeds without a folder first, then one outline per folder (in the
    // order the folders first appear in the list)
    QStringList folders;
    for (int i = 0; i < entries.size(); ++i) {
        if (!entries.at(i).folder.isEmpty() && !folders.contains(entries.at(i).folder)) {
            folders.append(entries.at(i).folder);
        }
    }
    for (int f = -1; f < folders.size(); ++f) {
        const QString folder = f < 0 ? QString() : folders.at(f);
        if (f >= 0) {
            xml.writeStartElement(QStringLiteral("outline"));
            xml.writeAttribute(QStringLiteral("text"), folder);
            xml.writeAttribute(QStringLiteral("title"), folder);
        }
    for (int i = 0; i < entries.size(); ++i) {
        const OpmlEntry &entry = entries.at(i);
        if (entry.folder != folder) {
            continue;
        }
        xml.writeEmptyElement(QStringLiteral("outline"));
        xml.writeAttribute(QStringLiteral("text"), entry.title);
        xml.writeAttribute(QStringLiteral("title"), entry.title);
        xml.writeAttribute(QStringLiteral("type"), QStringLiteral("rss"));
        xml.writeAttribute(QStringLiteral("xmlUrl"), entry.url);
        if (!entry.htmlUrl.isEmpty()) {
            xml.writeAttribute(QStringLiteral("htmlUrl"), entry.htmlUrl);
        }
    }
        if (f >= 0) {
            xml.writeEndElement(); // folder outline
        }
    }
    xml.writeEndElement(); // body
    xml.writeEndElement(); // opml
    xml.writeEndDocument();

    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace Opml

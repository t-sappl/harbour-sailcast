// SPDX-License-Identifier: GPL-3.0-or-later
#include "applog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QtGlobal>

#include <cstdio>

#include "../core/urlmask.h"

namespace {

const qint64 MaxBytes = 256 * 1024;

// (356) Recursive: a warning raised while this thread writes comes back
// in here and sees s_writing. (351 had a thread_local flag for that - the
// app is loaded by the booster like a library, where thread-local storage
// may not work in other threads, e.g. the render thread of the cover:
// suspected cause of the crash when switching to the app overview)
QMutex s_mutex(QMutex::Recursive);
QFile *s_file = nullptr;
QString s_path;
QtMessageHandler s_previous = nullptr;
bool s_enabled = true;    // only under s_mutex (372)
bool s_writing = false;   // only under s_mutex

void openFile()
{
    if (s_path.isEmpty()) {
        return;
    }
    delete s_file;
    s_file = new QFile(s_path);
    if (!s_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete s_file;
        s_file = nullptr;
    }
}

void rotateIfNeeded()
{
    if (!s_file || s_file->size() < MaxBytes) {
        return;
    }
    s_file->close();
    const QString older = s_path + QStringLiteral(".1");
    QFile::remove(older);
    QFile::rename(s_path, older);
    openFile();
}

// QML messages: "file:///usr/share/harbour-sailcast/qml/PlayerController.qml" -> "PlayerController.qml"
QString shortFile(const char *file)
{
    if (!file || !*file) {
        return QString();
    }
    const QString f = QString::fromUtf8(file);
    const int slash = f.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? f.mid(slash + 1) : f;
}

void passOn(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (s_previous) {
        s_previous(type, context, message);
    } else {
        fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
        fflush(stderr);
    }
}

void writeLine(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // console.log/qDebug are left out (noise); warnings, info and errors in
    const bool keep = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg
                      || type == QtInfoMsg;
    if (!keep) {
        return;
    }
    if (AppLog::ignored(message)) {
        return;
    }
    QMutexLocker lock(&s_mutex);
    if (s_writing || !s_file) {
        return;
    }
    QString where = shortFile(context.file);
    // (368) A cover that cannot be loaded is reported by every Image showing
    // it (15 times an evening): once per address and run is enough
    // (also by text: without QT_MESSAGELOGCONTEXT a C++ warning has no file)
    if (where.startsWith(QLatin1String("CoverImage.qml")) || where.startsWith(QLatin1String("imagecache.cpp"))
        || message.startsWith(QLatin1String("[ImageCache]"))) {
        static QSet<QString> seen;
        if (seen.size() > 500) {
            seen.clear();
        }
        static const QRegularExpression address(QStringLiteral("https?://\\S+"));
        const QRegularExpressionMatch m = address.match(message);
        const QString key = m.hasMatch() ? m.captured(0) : message;
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);
    }
    s_writing = true;
    const char kind = AppLog::kindOf(type, where, message);
    if (!where.isEmpty() && context.line > 0) {
        where += QLatin1Char(':') + QString::number(context.line);
    }
    QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                   + QLatin1Char(' ') + QLatin1Char(kind) + QLatin1Char(' ')
                   + (where.isEmpty() ? QString() : where + QStringLiteral(" "))
                   + AppLog::clean(message) + QLatin1Char('\n');
    s_file->write(line.toUtf8());
    s_file->flush();
    rotateIfNeeded();
    s_writing = false;
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // Into the file first: the default handler ends the app on a fatal message
    writeLine(type, context, message);
    passOn(type, context, message);
}

} // namespace

namespace AppLog {

bool ignored(const QString &message)
{
    static const char *const harmless[] = {
        "non-existent property \"showNavigationIndicator\"",   // RemorsePopup (Silica)
        "DBusAdaptor: Failed to unregister service",             // at exit
        "libpng warning:",                                       // colour profile of a cover
        "DelegateModel::cancel: index out range",                // Qt, list changed while loading
    };
    for (const char *text : harmless) {
        if (message.contains(QLatin1String(text))) {
            return true;
        }
    }
    return false;
}

char kindOf(QtMsgType type, const QString &file, const QString &message)
{
    if (type == QtInfoMsg) {
        return 'I';
    }
    if (type != QtWarningMsg) {
        return 'E';
    }
    // The player reports with console.warn (QML has no console.info in
    // every Qt version); only lines about something going wrong stay "W"
    if (file.startsWith(QLatin1String("PlayerController.qml"))) {
        static const QRegularExpression problem(QStringLiteral(
            "hangs|giving up|cannot|failed|error|not reached|did not start|lost jump|Watchdog"),
            QRegularExpression::CaseInsensitiveOption);
        return problem.match(message).hasMatch() ? 'W' : 'I';
    }
    return 'W';
}

QString clean(const QString &line)
{
    static const QRegularExpression mail(QStringLiteral("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"));
    // (review 351) Private feeds carry the token in the path
    // ("/rss/<long token>.xml"): long letter/digit runs in a path go
    static const QRegularExpression pathToken(QStringLiteral("/[A-Za-z0-9_\\-]{24,}"));
    QString result = UrlMask::maskText(line);
    result.replace(mail, QStringLiteral("<e-mail>"));
    result.replace(pathToken, QStringLiteral("/…"));
    result.replace(QLatin1Char('\n'), QStringLiteral(" | "));
    return result;
}

void install(const QString &version)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    {
        QMutexLocker lock(&s_mutex);
        s_path = dir + QStringLiteral("/sailcast.log");
        // (372) Switched off: no file is opened, nothing is written
        s_enabled = !QFile::exists(dir + QStringLiteral("/log-off"));
        if (s_enabled) {
            openFile();
            rotateIfNeeded();
        } else {
            // (review 372) left over from an interrupted switch-off
            QFile::remove(s_path);
            QFile::remove(s_path + QStringLiteral(".1"));
        }
        if (s_file) {
            const QString start = QStringLiteral("\n") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                                  + QStringLiteral(" === Sailcast ") + version + QStringLiteral(" started (")
                                  + QSysInfo::buildCpuArchitecture() + QStringLiteral(", ")
                                  + QSysInfo::prettyProductName() + QStringLiteral(")\n");
            s_file->write(start.toUtf8());
            s_file->flush();
        }
    }
    s_previous = qInstallMessageHandler(handler);
}

QString path()
{
    QMutexLocker lock(&s_mutex);
    return s_path;
}

bool isEnabled()
{
    QMutexLocker lock(&s_mutex);
    // (review 372) without install() (SAILCAST_NO_LOG) there is no log
    return s_enabled && !s_path.isEmpty();
}

void setEnabled(bool on)
{
    QMutexLocker lock(&s_mutex);
    if (s_path.isEmpty() || on == s_enabled) {
        return;
    }
    const QString marker = QFileInfo(s_path).absolutePath() + QStringLiteral("/log-off");
    if (!on) {
        // (review 372) the marker first: if it cannot be written, the next
        // start would log again although the switch says off - stay on
        QFile out(marker);
        if (!out.open(QIODevice::WriteOnly)
            || out.write("Sailcast: the log is switched off (About > Report a problem)\n") < 0
            || !out.flush()) {
            return;
        }
    }
    s_enabled = on;
    if (on) {
        QFile::remove(marker);
        openFile();
        if (s_file) {
            const QString start = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                                  + QStringLiteral(" === Log switched on\n");
            s_file->write(start.toUtf8());
            s_file->flush();
        }
    } else {
        delete s_file;
        s_file = nullptr;
        QFile::remove(s_path);
        QFile::remove(s_path + QStringLiteral(".1"));
    }
}

QString tail(int maxLines)
{
    // (review 354) Read under the lock: no rotation between the two files
    QByteArray data;
    {
        QMutexLocker lock(&s_mutex);
        if (s_path.isEmpty()) {
            return QString();
        }
        const QStringList parts { s_path + QStringLiteral(".1"), s_path };
        for (const QString &part : parts) {
            QFile in(part);
            if (in.open(QIODevice::ReadOnly)) {
                data += in.readAll();
            }
        }
    }
    QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) {
        lines.removeFirst();
    }
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
        lines.removeLast();
    }
    if (maxLines > 0 && lines.size() > maxLines) {
        lines = lines.mid(lines.size() - maxLines);
    }
    return lines.join(QLatin1Char('\n'));
}

QString writeText(const QString &directory, const QString &text)
{
    QDir().mkpath(directory);
    const QString target = directory + QStringLiteral("/sailcast-log-")
                           + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
                           + QStringLiteral(".txt");
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    const QByteArray bytes = text.toUtf8();
    // (review 354) A full disk shows only at flush
    if (out.write(bytes) != bytes.size() || !out.flush() || out.error() != QFileDevice::NoError) {
        out.close();
        QFile::remove(target);
        return QString();
    }
    out.close();
    return target;
}

} // namespace AppLog

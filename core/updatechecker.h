// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

// What the newest GitHub release offers for this device (350)
struct UpdateInfo
{
    UpdateInfo() : valid(false), assetSize(0) {}
    bool valid;          // a usable release (not draft/pre-release) with an RPM for this architecture
    QString tag;         // "v1.5.0"
    QString version;     // "1.5.0"
    QString notes;       // release text, plain
    QString notesDe;     // (365) its German block, plain (= notes when there is none)
    QString assetName;   // "harbour-sailcast-1.5.0-1.aarch64.rpm"
    QString assetUrl;    // https://github.com/t-sappl/harbour-sailcast/releases/download/...
    qint64 assetSize;    // bytes, 0 = unknown
};

// Update check and download for the GitHub build (350): OpenRepos is not
// reliable, so the app looks at the newest GitHub release itself. Only
// QtCore/QtNetwork; the pure functions (version compare, asset choice,
// clean-up rules) are unit tested in tools/updatetest. The Jolla Store
// build is built without this code (SAILCAST_STORE).
//
// Network: one request to api.github.com (releases/latest, with ETag so an
// unchanged answer costs no quota) at most once a day; the download from
// the asset URL (github.com redirects to its CDN).
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    static QString releasesUrl();
    static QString trustedAssetPrefix();

    // "v1.5.0", "1.5", "1.4.1 (build 345)" -> {1,5,0}, {1,5,0}, {1,4,1}; empty = no version
    static QList<int> parseVersion(const QString &text);
    // <0, 0, >0
    static int compareVersions(const QString &a, const QString &b);
    // The RPM architecture name of this device: aarch64, armv7hl, i486
    static QString architecture();
    static QString architectureFor(const QString &qtCpuArchitecture);
    // Only releases that are neither draft nor pre-release, with an RPM for
    // the architecture whose address is on our own GitHub repository
    static UpdateInfo parseRelease(const QByteArray &json, const QString &arch);
    // Release text as plain lines (no markdown, without the credit line)
    static QString plainNotes(const QString &markdown);
    // harbour-sailcast-<v>-<r>.<arch>.rpm (or .rpm.part) of ours
    static bool isOwnPackage(const QString &fileName);
    // Of the file names: our packages at or below the running version and
    // every unfinished download - what may be deleted at the start
    static QStringList obsoletePackages(const QStringList &fileNames, const QString &runningVersion);

    // Request releases/latest; etag may be empty
    void check(const QString &etag);
    void download(const UpdateInfo &info, const QString &directory);
    void cancelDownload();
    bool busy() const { return m_checkReply != nullptr; }
    bool downloading() const { return m_downloadReply != nullptr; }

signals:
    // notModified: the answer for the ETag is still valid (body empty)
    void checkFinished(bool notModified, const QByteArray &body, const QString &etag);
    void checkFailed(const QString &error);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &path);
    void downloadFailed(const QString &error);

private:
    void onCheckFinished();
    void onDownloadFinished();
    void removePart();

    QNetworkAccessManager *m_network;
    QNetworkReply *m_checkReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    QFile m_file;
    QString m_path;
    qint64 m_expectedSize = 0;
    bool m_cancelled = false;
    bool m_writeFailed = false;
};

#endif // UPDATECHECKER_H

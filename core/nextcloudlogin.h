// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXTCLOUDLOGIN_H
#define NEXTCLOUDLOGIN_H

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Nextcloud Login Flow v2 (44, as in Sailfeed): the user signs in in the
// browser, the app receives an app password without typing anything.
//
// 1. POST <server>/index.php/login/v2 -> {"poll": {"token", "endpoint"},
//    "login": <URL to open in the browser>}
// 2. loginUrl() is emitted - the app opens it in the browser
// 3. POST <endpoint> with token=<token> every 2 s: 404 until the user has
//    confirmed, then {"server", "loginName", "appPassword"} -> finished()
// Gives up after 20 minutes (the token expires on the server too).
class NextcloudLogin : public QObject
{
    Q_OBJECT
public:
    // Reasons for failed(); the app turns them into translated texts
    enum Error { InvalidAddress, NoNextcloud, ServerError, NotStarted, TimedOut, Incomplete };

    explicit NextcloudLogin(QObject *parent = nullptr);

    bool isActive() const { return m_active; }
    void start(const QString &server);
    void cancel();

signals:
    void loginUrl(const QString &url);
    void finished(const QString &server, const QString &user, const QString &appPassword);
    void failed(int error, const QString &detail);
    void activeChanged();

private:
    void poll();
    void stop();

    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timer;
    QUrl m_endpoint;
    QByteArray m_token;
    qint64 m_deadline = 0;
    bool m_active = false;
};

#endif // NEXTCLOUDLOGIN_H

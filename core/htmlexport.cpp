// SPDX-License-Identifier: GPL-3.0-or-later
#include "htmlexport.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QUrl>

namespace HtmlExport {

QString escape(const QString &text)
{
    QString result = text;
    result.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    result.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    result.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    result.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return result;
}

QString plainText(const QString &html, int maxLength)
{
    QString text = html;
    text.replace(QRegularExpression(QStringLiteral("<\\s*(br|/p|/li|/div)\\s*/?>"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral(" "));
    text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text = text.simplified();
    if (maxLength > 0 && text.length() > maxLength) {
        text = text.left(maxLength);
        const int space = text.lastIndexOf(QLatin1Char(' '));
        if (space > maxLength / 2) {
            text = text.left(space);
        }
        text += QStringLiteral("…");
    }
    return text;
}

static bool safeLink(const QString &url)
{
    return url.startsWith(QLatin1String("http://")) || url.startsWith(QLatin1String("https://"));
}

static QString orDefault(const QString &value, const char *fallback)
{
    return value.isEmpty() ? QString::fromLatin1(fallback) : value;
}

static QString duration(qint64 seconds, const Labels &l)
{
    const qint64 minutes = seconds / 60;
    const QString h = orDefault(l.hour, "h");
    const QString m = orDefault(l.minute, "min");
    if (minutes < 60) {
        return QStringLiteral("%1 %2").arg(minutes).arg(m);
    }
    if (minutes % 60 == 0) {
        return QStringLiteral("%1 %2").arg(minutes / 60).arg(h);
    }
    return QStringLiteral("%1 %2 %3 %4").arg(minutes / 60).arg(h).arg(minutes % 60).arg(m);
}

QString opmlOf(const QList<Folder> &folders)
{
    QString o = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<opml version=\"2.0\"><head><title>Sailcast</title></head><body>\n");
    for (int f = 0; f < folders.size(); ++f) {
        const Folder &folder = folders.at(f);
        if (folder.podcasts.isEmpty()) {
            continue;
        }
        const bool wrap = !folder.name.isEmpty();
        if (wrap) {
            o += QStringLiteral("<outline text=\"%1\">\n").arg(escape(folder.name));
        }
        for (int i = 0; i < folder.podcasts.size(); ++i) {
            const Podcast &p = folder.podcasts.at(i);
            o += QStringLiteral("<outline type=\"rss\" text=\"%1\" xmlUrl=\"%2\"%3/>\n")
                     .arg(escape(p.title), escape(p.feedUrl),
                          safeLink(p.link) ? QStringLiteral(" htmlUrl=\"%1\"").arg(escape(p.link)) : QString());
        }
        if (wrap) {
            o += QStringLiteral("</outline>\n");
        }
    }
    return o + QStringLiteral("</body></opml>\n");
}

static QString opmlLink(const QList<Folder> &folders, const QString &fileName, const QString &text, const QString &cssClass)
{
    return QStringLiteral("<a class=\"%1\" onclick=\"event.stopPropagation()\" download=\"%2\" href=\"data:text/x-opml;base64,%3\">%4</a>")
            .arg(cssClass, escape(fileName),
                 QString::fromLatin1(opmlOf(folders).toUtf8().toBase64()), escape(text));
}

static QString listOf(const QString &title, const QStringList &items)
{
    if (items.isEmpty()) {
        return QString();
    }
    QString html = QStringLiteral("<details class=\"sub\"><summary>%1 (%2)</summary><ul>")
            .arg(escape(title), QString::number(items.size()));
    for (int i = 0; i < items.size(); ++i) {
        html += QStringLiteral("<li>%1</li>").arg(escape(items.at(i)));
    }
    return html + QStringLiteral("</ul></details>");
}

// (304, Thomas 13 Nov) Links to other sites open in a new tab
static QString episodeHtml(const Episode &e, const Labels &l)
{
    const QString head = QStringLiteral("<span>%1%2</span><span class=\"d\">%3</span>")
            .arg(e.played ? QStringLiteral("✓ ") : QString(), escape(e.title),
                 escape(e.duration.isEmpty() ? e.date : e.date + QStringLiteral(" · ") + e.duration));
    const QString notes = plainText(e.description, 600);
    // (304, Thomas 13 Nov) The description at once, without its own fold:
    // it is cut short anyway
    const QString inner = (notes.isEmpty() ? QString()
                           : QStringLiteral("<p class=\"en ds\">%1</p>").arg(escape(notes)))
            + listOf(orDefault(l.chapters, "Chapters"), e.chapters)
            + listOf(orDefault(l.bookmarks, "Bookmarks"), e.bookmarks);
    if (inner.isEmpty()) {
        return QStringLiteral("<div class=\"ep\"><div class=\"eh\">%1</div></div>").arg(head);
    }
    return QStringLiteral("<details class=\"ep\"><summary>%1</summary>%2</details>").arg(head, inner);
}

static QString podcastHtml(const Podcast &p, const Options &options, const QString &folderName)
{
    const Labels &l = options.labels;
    QString html;
    html += QStringLiteral("<details class=\"pod\"><summary>"
                           "<label class=\"hit\" onclick=\"event.stopPropagation()\"><input type=\"checkbox\" class=\"ck\" aria-label=\"%5\" data-f=\"%1\" data-t=\"%2\" data-u=\"%3\" data-h=\"%4\"></label>")
                .arg(escape(folderName), escape(p.title), escape(p.feedUrl), escape(safeLink(p.link) ? p.link : QString()), escape(p.title));
    if (!p.coverDataUri.isEmpty()) {
        html += QStringLiteral("<img class=\"cv\" alt=\"\" src=\"%1\">").arg(p.coverDataUri);
    } else {
        html += QStringLiteral("<span class=\"cv\" aria-hidden=\"true\">%1</span>").arg(escape(p.title.trimmed().left(1).toUpper()));
    }
    html += QStringLiteral("<span class=\"tx\"><span class=\"t\">%1</span>").arg(escape(p.title));
    if (!p.author.isEmpty()) {
        html += QStringLiteral("<span class=\"a\">%1</span>").arg(escape(p.author));
    }
    html += QStringLiteral("</span>");
    if (options.statistics && p.listenedSeconds > 0) {
        html += QStringLiteral("<span class=\"st\">%1</span>").arg(escape(duration(p.listenedSeconds, l)));
    }
    html += QStringLiteral("</summary><div class=\"det\">");
    QStringList facts;
    if (!p.categories.isEmpty()) {
        facts << QStringLiteral("<b>%1:</b> %2").arg(escape(orDefault(l.category, "Category")),
                    escape(QString(p.categories.first()).replace(QLatin1Char('/'), QStringLiteral(" › "))));
    }
    if (!p.language.isEmpty()) {
        facts << QStringLiteral("<b>%1:</b> %2").arg(escape(orDefault(l.language, "Language")), escape(p.language));
    }
    if (p.episodeCount > 0) {
        facts << QStringLiteral("<b>%1:</b> %2").arg(escape(orDefault(l.episodes, "Episodes")), QString::number(p.episodeCount));
    }
    if (!p.rhythm.isEmpty()) {
        facts << QStringLiteral("<b>%1:</b> %2").arg(escape(orDefault(l.rhythm, "Rhythm")), escape(p.rhythm));
    }
    if (!facts.isEmpty()) {
        html += QStringLiteral("<p>%1</p>").arg(facts.join(QStringLiteral(" · ")));
    }
    if (options.descriptions) {
        const QString text = plainText(p.description, 400);
        if (!text.isEmpty()) {
            html += QStringLiteral("<p>%1</p>").arg(escape(text));
        }
    }
    html += QStringLiteral("<p class=\"links\">");
    if (safeLink(p.feedUrl)) {
        html += QStringLiteral("<a class=\"lnk\" href=\"podcast://%1\">%2</a>")
                    .arg(escape(p.feedUrl.mid(p.feedUrl.indexOf(QStringLiteral("://")) + 3)),
                         escape(orDefault(l.subscribe, "Subscribe")));
        html += QStringLiteral("<a target=\"_blank\" rel=\"noopener\" class=\"lnk\" href=\"%1\">%2</a>").arg(escape(p.feedUrl), escape(orDefault(l.feed, "Feed")));
    }
    if (safeLink(p.link)) {
        html += QStringLiteral("<a target=\"_blank\" rel=\"noopener\" class=\"lnk\" href=\"%1\">%2</a>").arg(escape(p.link), escape(orDefault(l.website, "Website")));
    }
    // Search for the podcast by name elsewhere (no ids are stored, so a search, not a page):
    // an own line of pills below the links of the podcast
    html += QStringLiteral("</p><p class=\"links\">");
    {
        const QString q = QString::fromLatin1(QUrl::toPercentEncoding(p.title));
        html += QStringLiteral("<a target=\"_blank\" rel=\"noopener\" class=\"lnk\" href=\"https://fyyd.de/search?search=%1\">%2</a>")
                    .arg(q, escape(orDefault(l.fyyd, "fyyd")));
        html += QStringLiteral("<a target=\"_blank\" rel=\"noopener\" class=\"lnk\" href=\"https://podcastindex.org/search?q=%1\">%2</a>")
                    .arg(q, escape(orDefault(l.podcastIndex, "Podcast Index")));
        html += QStringLiteral("<a target=\"_blank\" rel=\"noopener\" class=\"lnk\" href=\"https://open.spotify.com/search/%1/podcasts\">%2</a>")
                    .arg(q, escape(orDefault(l.spotify, "Spotify")));
        html += QStringLiteral("<a target=\"_blank\" rel=\"noopener\" class=\"lnk\" href=\"https://podcasts.apple.com/search?term=%1\">%2</a>")
                    .arg(q, escape(orDefault(l.apple, "Apple Podcasts")));
    }
    html += QStringLiteral("</p>");
    if (!p.episodes.isEmpty()) {
        html += QStringLiteral("<div class=\"eps\"><b>%1</b>").arg(escape(orDefault(l.lastEpisodes, "Last episodes")));
        const int shown = qMin(p.episodes.size(), qMax(1, options.visibleEpisodes));
        for (int i = 0; i < shown; ++i) {
            html += episodeHtml(p.episodes.at(i), l);
        }
        if (p.episodes.size() > shown) {
            html += QStringLiteral("<details class=\"more\"><summary>%1</summary>")
                        .arg(escape(orDefault(l.moreEpisodes, "More episodes (%1)").arg(p.episodes.size() - shown)));
            for (int i = shown; i < p.episodes.size(); ++i) {
                html += episodeHtml(p.episodes.at(i), l);
            }
            html += QStringLiteral("</details>");
        }
        html += QStringLiteral("</div>");
    }
    html += QStringLiteral("</div></details>\n");
    return html;
}

static const char *kStyle =
    ":root{color-scheme:light dark;--bg:#f6f6f4;--card:#fff;--tx:#1d1d1b;--mu:#6b6b66;--ln:#e2e2dd;--ac:#1a5fb4}"
    "@media(prefers-color-scheme:dark){:root{--bg:#161616;--card:#202020;--tx:#ececea;--mu:#9a9a94;--ln:#333;--ac:#78aeed}}"
    "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:16px/1.5 system-ui,sans-serif}"
    "main{max-width:720px;margin:0 auto;padding:20px 14px calc(70px + env(safe-area-inset-bottom))}h1{font-size:24px;margin:0}"
    ".hd{display:flex;align-items:center;gap:12px}.ic{width:32px;height:32px;border-radius:8px;flex:none}"
    ".mu{color:var(--mu);font-size:14px}"
    ".card{background:var(--card);border:1px solid var(--ln);border-radius:12px;margin:12px 0;overflow:hidden}"
    ".card>summary{padding:13px 16px;font-weight:600;cursor:pointer;display:flex;gap:10px;align-items:center}"
    "summary{list-style:none}summary::-webkit-details-marker{display:none}"
    ".cnt{margin-left:auto;color:var(--mu);font-weight:400;font-size:13px}"
    ".fo{font-size:12px;color:var(--ac);text-decoration:none;border:1px solid var(--ln);border-radius:12px;padding:1px 10px;font-weight:400}"
    ".pod{border-top:1px solid var(--ln)}.pod>summary{padding:10px 16px;display:flex;gap:12px;align-items:center;cursor:pointer}"
    // Own rounded-square tick box in the colours of the page (the stock box is a white square in dark mode)
    ".ck{display:none;-webkit-appearance:none;-moz-appearance:none;appearance:none;margin:0;width:22px;height:22px;flex:none;"
    "border:2px solid var(--mu);border-radius:6px;background:transparent no-repeat center/14px;cursor:pointer;opacity:.7}"
    ".ck:checked{opacity:1;border-color:var(--ac);background-color:var(--ac);background-image:url(\"data:image/svg+xml,"
    "%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3E%3Cpath d='M3 8.5l3.2 3L13 4.5' fill='none' stroke='white' stroke-width='2.4' stroke-linecap='round' stroke-linejoin='round'/%3E%3C/svg%3E\")}"
    ".hit{align-items:center;display:none;margin:-10px -2px -10px -16px;padding:10px 6px 10px 16px;cursor:pointer}.js .hit{display:flex}.js .ck{display:block}"
    ".cv{display:flex;align-items:center;justify-content:center;color:var(--mu);font-size:20px;font-weight:600;width:48px;height:48px;border-radius:8px;flex:none;background:var(--ln);object-fit:cover}"
    ".tx{display:flex;flex-direction:column;min-width:0}.t{font-size:15px}.a{font-size:13px;color:var(--mu)}"
    ".st{margin-left:auto;font-size:13px;color:var(--mu);white-space:nowrap}"
    ".det{padding:0 16px 14px 16px;font-size:14px;color:var(--mu)}.det p{margin:6px 0}.det b{color:var(--tx);font-weight:600}"
    ".lnk{white-space:nowrap;display:inline-block;margin:2px 8px 0 0;padding:3px 12px;border:1px solid var(--ln);border-radius:14px;color:var(--ac);text-decoration:none;font-size:13px}"
    ".eps{margin-top:10px}.ep{border-top:1px dashed var(--ln);padding:6px 0;font-size:13px}"
    ".ep>summary,.eh{display:flex;gap:8px;color:var(--tx)}.ep>summary{cursor:pointer}.d{margin-left:auto;color:var(--mu);white-space:nowrap}"
    ".sub{padding:2px 0 2px 14px}.sub>summary{cursor:pointer;color:var(--mu);font-size:12px}.sub ul{margin:4px 0 2px 18px;padding:0;color:var(--mu)}.en{margin:4px 0 2px 4px!important;font-size:13px}.ds{padding-left:14px;color:var(--mu)}"
    ".intro{margin-top:12px;font-size:14px;color:var(--mu)}.intro p{margin:0 0 4px}.intro ul{margin:0;padding-left:18px}.intro li{margin:2px 0}.intro b{color:var(--tx);font-weight:600}"
    ".stats{background:var(--card);border:1px solid var(--ln);border-radius:12px;margin:14px 0;padding:12px 16px}"
    ".stats>summary{font-weight:600;cursor:pointer;font-size:15px;display:flex;align-items:center;justify-content:space-between}"
    ".big{text-align:center;padding:12px 0 10px;border-bottom:1px solid var(--ln);margin-top:8px}.big b{display:block;font-size:24px;color:var(--ac)}"
    ".big span,.bx span{font-size:12px;color:var(--mu)}.g{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px;text-align:center}"
    ".bx{background:var(--bg);border-radius:8px;padding:8px}.bx b{display:block;font-size:16px}"
    ".sh{font-weight:600;font-size:14px;margin-top:14px}.row{display:flex;justify-content:space-between;font-size:13px;margin-top:6px;gap:10px}"
    ".bar{height:6px;border-radius:3px;background:var(--ac);margin-top:3px}"
    ".lg{font-size:12px;color:var(--mu);margin-top:8px;line-height:1.45}.ml{display:flex;justify-content:space-between;font-size:11px;color:var(--mu)}"
    ".hm{display:grid;grid-template-rows:repeat(7,auto);grid-auto-flow:column;grid-auto-columns:1fr;gap:2px;margin-top:8px}"
    ".hm i{display:block;aspect-ratio:1;min-height:3px;border-radius:1px;background:var(--ac)}.hm .h0{background:var(--ln);opacity:1}"
    ".hm .h1{opacity:.3}.hm .h2{opacity:.5}.hm .h3{opacity:.75}.hm .hx{visibility:hidden}"
    ".hml{position:relative;height:16px;font-size:11px;color:var(--mu)}.hml span{position:absolute;top:2px}"
    ".wk{display:flex;gap:2px;align-items:flex-end;height:56px;margin-top:8px}.wk i{flex:1;background:var(--ac);border-radius:3px 3px 0 0;opacity:.85}"
    ".wl{display:flex;gap:6px;font-size:11px;line-height:1.35;color:var(--mu);text-align:center}.wl span{flex:1}"
    ".more{margin-top:4px}.more>summary{cursor:pointer;color:var(--ac);font-size:13px;padding:6px 0;display:flex;align-items:center}"
    ".promo .lnk{margin-top:6px}.promo .pri{background:var(--ac);border-color:var(--ac);color:var(--bg);font-weight:600}.promo .l2{margin-top:10px}.promo p{margin:0 0 8px}.promo .mw{margin-bottom:14px;color:var(--tx);font-weight:600;display:flex;align-items:center;gap:10px}.promo{margin-top:16px;padding:12px 14px;border:1px solid var(--ln);border-radius:12px;background:var(--card);font-size:13px;color:var(--mu)}"
    ".top{margin-top:12px}.note{margin-top:8px;font-size:13px;color:var(--mu)}"
    ".bar2{display:none;position:sticky;bottom:calc(10px + env(safe-area-inset-bottom));background:var(--card);border:1px solid var(--ln);border-radius:14px;padding:10px 14px;gap:10px;align-items:center;box-shadow:0 2px 10px #0004;margin-top:14px}.bar2.on{display:flex}"
    ".bar2 button{font:inherit;font-size:13px;color:var(--ac);background:none;border:1px solid var(--ln);border-radius:14px;padding:3px 12px}"
    ".bar2 .n{margin-right:auto;font-size:13px}"
    // Chevron: shows that a title unfolds, turns when open
    ".card>summary::after,.pod>summary::after,.ep>summary::after,.sub>summary::after,.stats>summary::after,.more>summary::after{content:\"\";flex:none;width:7px;height:7px;"
    "border-right:2px solid var(--mu);border-bottom:2px solid var(--mu);transform:rotate(45deg);margin:0 4px 3px 6px;transition:transform .15s}"
    ".pod .tx{flex:1}"
    "details[open]>summary::after{transform:rotate(-135deg);margin-bottom:-3px}"
    ".sub>summary{display:flex;align-items:center}.sub>summary::after{width:5px;height:5px}";

// Optional: tick podcasts, save them as OPML (the browser builds the file)
static const char *kScript =
    "<script>document.documentElement.className=\"js\";(function(){"
    "var c=[].slice.call(document.querySelectorAll(\".ck\")),n=document.getElementById(\"n\"),t=n.getAttribute(\"data-t\");"
    "function u(){var k=c.filter(function(x){return x.checked}).length;n.textContent=t.replace(\"%1\",k);document.getElementById(\"bar\").className=k?\"bar2 on\":\"bar2\"}u();"
    "c.forEach(function(x){x.onchange=u});"
    "document.getElementById(\"all\").onclick=function(){c.forEach(function(x){x.checked=true});u()};"
    "document.getElementById(\"none\").onclick=function(){c.forEach(function(x){x.checked=false});u()};"
    "function q(s){return s.replace(/&/g,\"&amp;\").replace(/\"/g,\"&quot;\").replace(/</g,\"&lt;\")}"
    "function o(x){return '<outline type=\"rss\" text=\"'+q(x.dataset.t)+'\" xmlUrl=\"'+q(x.dataset.u)+'\"'+(x.dataset.h?' htmlUrl=\"'+q(x.dataset.h)+'\"':'')+'/>'}"
    "document.getElementById(\"sv\").onclick=function(){var s=c.filter(function(x){return x.checked});if(!s.length){return}"
    "var g={},b='';s.forEach(function(x){if(x.dataset.f){(g[x.dataset.f]=g[x.dataset.f]||[]).push(x)}else{b+=o(x)}});"
    "var r='<?xml version=\"1.0\" encoding=\"UTF-8\"?><opml version=\"2.0\"><head><title>Sailcast</title></head><body>'+b;"
    "for(var f in g){r+='<outline text=\"'+q(f)+'\">'+g[f].map(o).join('')+'</outline>'}"
    "r+='</body></opml>';var a=document.createElement(\"a\");a.href=\"data:text/x-opml;charset=utf-8,\"+encodeURIComponent(r);"
    "a.download=\"sailcast-selection.opml\";a.click()}})();</script>\n";

static QString box(const QString &value, const QString &label)
{
    return QStringLiteral("<div class=\"bx\"><b>%1</b><span>%2</span></div>").arg(escape(value), escape(label));
}

static QString statisticsHtml(const Statistics &st)
{
    if (st.level <= 0) {
        return QString();
    }
    // (333/334, code review) Numbers in the same multi-arg() as the texts: a
    // chained .arg() replaced a "%1" inside a podcast title or category
    QString html = QStringLiteral("<details class=\"stats\"><summary>%1</summary>").arg(escape(st.title));
    html += QStringLiteral("<div class=\"big\"><b>%1</b><span>%2</span></div>").arg(escape(st.total), escape(st.totalLabel));
    html += QStringLiteral("<div class=\"g\">") + box(st.saved, st.savedLabel) + box(st.episodes, st.episodesLabel);
    if (st.level >= 2) {
        html += box(st.perDay, st.perDayLabel) + box(st.streak, st.streakLabel);
        if (!st.activeDays.isEmpty()) {
            html += box(st.activeDays, st.activeDaysLabel);
        }
    }
    html += QStringLiteral("</div>");
    if (!st.totalsNote.isEmpty()) {
        html += QStringLiteral("<div class=\"lg\">%1</div>").arg(escape(st.totalsNote));
    }
    if (st.level >= 2 && !st.streakNote.isEmpty()) {
        html += QStringLiteral("<div class=\"lg\">%1</div>").arg(escape(st.streakNote));
    }
    // (297) Course per month, year heat map, categories, records
    if (st.level >= 2 && st.months.size() > 1) {
        html += QStringLiteral("<div class=\"sh\">%1</div><div class=\"wk\">").arg(escape(st.monthsTitle));
        for (int i = 0; i < st.months.size(); ++i) {
            html += QStringLiteral("<i title=\"%1 %2\" style=\"height:%3%\"></i>")
                        .arg(escape(st.months.at(i).label), escape(st.months.at(i).value),
                             QString::number(qRound(qBound(0.03, st.months.at(i).share, 1.0) * 100)));
        }
        html += QStringLiteral("</div><div class=\"ml\"><span>%1</span><span>%2</span></div>")
                    .arg(escape(st.months.first().label), escape(st.months.last().label));
    }
    if (st.level >= 2 && st.heat.size() == 53 * 7) {
        html += QStringLiteral("<div class=\"sh\">%1</div><div class=\"hm\">").arg(escape(st.heatTitle));
        for (int i = 0; i < st.heat.size(); ++i) {
            const int v = st.heat.at(i);
            html += v < 0 ? QStringLiteral("<i class=\"hx\"></i>") : QStringLiteral("<i class=\"h%1\"></i>").arg(qBound(0, v, 4));
        }
        html += QStringLiteral("</div><div class=\"hml\">");
        for (int i = 0; i < st.heatMonths.size(); ++i) {
            const int week = st.heatMonths.at(i).first;
            const int next = i + 1 < st.heatMonths.size() ? st.heatMonths.at(i + 1).first : 53;
            if (week >= 50 || next - week < 3) {
                continue;   // would run into the next name
            }
            html += QStringLiteral("<span style=\"left:%1%\">%2</span>")
                        .arg(QString::number(100.0 * week / 53, 'f', 1), escape(st.heatMonths.at(i).second));
        }
        html += QStringLiteral("</div>");
    }
    if (st.level >= 2 && !st.top.isEmpty()) {
        html += QStringLiteral("<div class=\"sh\">%1</div>").arg(escape(st.topTitle));
        if (!st.topNote.isEmpty()) {
            html += QStringLiteral("<div class=\"lg\" style=\"margin-top:2px\">%1</div>").arg(escape(st.topNote));
        }
        for (int i = 0; i < st.top.size(); ++i) {
            const Bar &b = st.top.at(i);
            html += QStringLiteral("<div class=\"row\"><span>%1</span><span>%2</span></div><div class=\"bar\" style=\"width:%3%\"></div>")
                        .arg(escape(b.label), escape(b.value), QString::number(qRound(qBound(0.02, b.share, 1.0) * 100)));
        }
    }
    if (st.level >= 2 && st.weekdays.size() == 7) {
        html += QStringLiteral("<div class=\"sh\">%1</div><div class=\"wk\">").arg(escape(st.weekdaysTitle));
        for (int i = 0; i < 7; ++i) {
            html += QStringLiteral("<i title=\"%1\" style=\"height:%2%\"></i>")
                        .arg(escape(st.weekdays.at(i).value), QString::number(qRound(qBound(0.03, st.weekdays.at(i).share, 1.0) * 100)));
        }
        html += QStringLiteral("</div><div class=\"wl\">");
        for (int i = 0; i < 7; ++i) {
            html += QStringLiteral("<span>%1<br>%2</span>").arg(escape(st.weekdays.at(i).label), escape(st.weekdays.at(i).value));
        }
        html += QStringLiteral("</div>");
    }
    if (st.level >= 2 && !st.categories.isEmpty()) {
        html += QStringLiteral("<div class=\"sh\">%1</div>").arg(escape(st.categoriesTitle));
        if (!st.categoriesNote.isEmpty()) {
            html += QStringLiteral("<div class=\"lg\" style=\"margin-top:2px\">%1</div>").arg(escape(st.categoriesNote));
        }
        for (int i = 0; i < st.categories.size(); ++i) {
            const Bar &b = st.categories.at(i);
            html += QStringLiteral("<div class=\"row\"><span>%1</span><span>%2</span></div><div class=\"bar\" style=\"width:%3%\"></div>")
                        .arg(escape(b.label), escape(b.value), QString::number(qRound(qBound(0.02, b.share, 1.0) * 100)));
        }
    }
    if (st.level >= 2 && !st.records.isEmpty()) {
        html += QStringLiteral("<div class=\"sh\">%1</div>").arg(escape(st.recordsTitle));
        for (int i = 0; i < st.records.size(); ++i) {
            html += QStringLiteral("<div class=\"row\"><span style=\"color:var(--mu)\">%1</span><span style=\"text-align:right\">%2</span></div>")
                        .arg(escape(st.records.at(i).first), escape(st.records.at(i).second));
        }
    }
    if (!st.note.isEmpty()) {
        html += QStringLiteral("<div class=\"note\" style=\"text-align:center\">%1</div>").arg(escape(st.note));
    }
    return html + QStringLiteral("</details>\n");
}

QString build(const QList<Folder> &folders, const Options &options)
{
    const Labels &l = options.labels;
    int podcastCount = 0;
    int folderCount = 0;
    for (int f = 0; f < folders.size(); ++f) {
        podcastCount += folders.at(f).podcasts.size();
        if (!folders.at(f).name.isEmpty() && !folders.at(f).podcasts.isEmpty()) {
            ++folderCount;
        }
    }
    const QString title = orDefault(l.title, "My podcasts");
    const QString subtitle = l.subtitle.isEmpty() ? QStringLiteral("%1 podcasts").arg(podcastCount) : l.subtitle;
    QString html;
    html += QStringLiteral("<!DOCTYPE html>\n<html lang=\"%1\"><head><meta charset=\"utf-8\">"
                           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                           "<title>%2</title><style>%3</style></head><body><main>\n")
                .arg(escape(orDefault(l.lang, "en")), escape(title), QString::fromLatin1(kStyle));
    html += QStringLiteral("<div class=\"hd\">");
    if (!options.iconDataUri.isEmpty()) {
        html += QStringLiteral("<img class=\"ic\" alt=\"\" src=\"%1\">").arg(options.iconDataUri);
    }
    html += QStringLiteral("<div><h1>%1</h1><div class=\"mu\">%2</div></div></div>\n").arg(escape(title), escape(subtitle));
    if (!l.intro.isEmpty()) {
        html += QStringLiteral("<div class=\"intro\"><p>%1</p>").arg(escape(l.intro));
        if (!l.introPoints.isEmpty()) {
            html += QStringLiteral("<ul>");
            for (int i = 0; i < l.introPoints.size(); ++i) {
                // "Name: text" - the name in bold like the buttons
                const QString point = l.introPoints.at(i);
                const int colon = point.indexOf(QLatin1Char(':'));
                html += colon > 0
                        ? QStringLiteral("<li><b>%1</b>%2</li>").arg(escape(point.left(colon)), escape(point.mid(colon)))
                        : QStringLiteral("<li>%1</li>").arg(escape(point));
            }
            html += QStringLiteral("</ul>");
        }
        html += QStringLiteral("</div>\n");
    }
    html += opmlLink(folders, QStringLiteral("sailcast-subscriptions.opml"),
                     orDefault(l.opmlAll, "All podcasts (OPML)"), QStringLiteral("lnk top")) + QLatin1Char('\n');
    if (!l.privateNote.isEmpty()) {
        html += QStringLiteral("<div class=\"note\">%1</div>\n").arg(escape(l.privateNote));
    }
    html += statisticsHtml(options.stats);
    bool first = true;
    for (int f = 0; f < folders.size(); ++f) {
        const Folder &folder = folders.at(f);
        if (folder.podcasts.isEmpty()) {
            continue;
        }
        const bool card = options.folders && (folderCount > 0);
        if (card) {
            const QString name = folder.name.isEmpty() ? orDefault(l.otherPodcasts, "Other podcasts") : folder.name;
            QList<Folder> single;
            single << folder;
            html += QStringLiteral("<details class=\"card\"%1><summary>%2<span class=\"cnt\">%3</span>%4</summary>\n")
                        .arg(first ? QStringLiteral(" open") : QString(), escape(name),
                             escape(folder.countText.isEmpty() ? QString::number(folder.podcasts.size()) : folder.countText),
                             opmlLink(single, name + QStringLiteral(".opml"), orDefault(l.opml, "OPML"), QStringLiteral("fo")));
            first = false;
        } else {
            html += QStringLiteral("<div class=\"card\">\n");
        }
        for (int i = 0; i < folder.podcasts.size(); ++i) {
            html += podcastHtml(folder.podcasts.at(i), options, folder.name);
        }
        html += card ? QStringLiteral("</details>\n") : QStringLiteral("</div>\n");
    }
    html += QStringLiteral("<div class=\"bar2\" id=\"bar\"><span class=\"n\" id=\"n\" data-t=\"%1\"></span>"
                           "<button id=\"all\">%2</button><button id=\"none\">%3</button><button id=\"sv\">%4</button></div>\n")
                .arg(escape(orDefault(l.selected, "%1 selected")), escape(orDefault(l.all, "All")),
                     escape(orDefault(l.none, "Deselect")), escape(orDefault(l.saveSelection, "Save selection as OPML")));
    html += QString::fromLatin1(kScript);
    // The box: "Made with Sailcast ..." first, a blank line, the text about Sailcast and
    // Sailfish OS, then a new line of two pills: the project on GitHub, Sailfish OS
    const QString madeWith = escape(orDefault(l.madeWith, "Made with Sailcast for Sailfish OS"));
    if (!l.promo.isEmpty()) {
        const QString icon = options.iconDataUri.isEmpty() ? QString()
                : QStringLiteral("<img class=\"ic\" alt=\"\" src=\"%1\">").arg(options.iconDataUri);
        html += QStringLiteral("<div class=\"promo\"><p class=\"mw\">%5%1</p><p>%2 %6</p>"
                               "<a target=\"_blank\" rel=\"noopener\" class=\"lnk pri\" href=\"https://github.com/t-sappl/harbour-sailcast\">%3</a>"
                               "<a target=\"_blank\" rel=\"noopener\" class=\"lnk pri\" href=\"https://sailfishos.org\">%4</a>"
                               "<div class=\"l2\"><a target=\"_blank\" rel=\"noopener\" class=\"lnk\" href=\"https://fyyd.de\">fyyd.de</a>"
                               "<a target=\"_blank\" rel=\"noopener\" class=\"lnk\" href=\"https://podcastindex.org\">podcastindex.org</a></div></div>\n")
                    .arg(madeWith, escape(l.promo), escape(orDefault(l.sourceCode, "Sailcast on GitHub")),
                         escape(orDefault(l.sailfishOs, "More about Sailfish OS")), icon,
                         escape(orDefault(l.about, "The podcast search uses fyyd and Podcast Index.")));
    } else {
        html += QStringLiteral("<div class=\"note\">%1</div>\n").arg(madeWith);
    }
    html += QStringLiteral("</main></body></html>\n");
    return html;
}

} // namespace HtmlExport

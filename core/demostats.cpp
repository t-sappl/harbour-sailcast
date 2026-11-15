// SPDX-License-Identifier: GPL-3.0-or-later

#include "demostats.h"

#include <QMap>
#include <algorithm>
#include <cmath>
#include <random>

namespace DemoStats {

namespace {

typedef std::mt19937 Rng;

double uniform(Rng &rng)
{
    return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
}

double lognormal(Rng &rng, double sigma)
{
    return std::exp(std::normal_distribution<double>(0.0, sigma)(rng));
}

int pickWeighted(Rng &rng, const QList<double> &weights)
{
    double total = 0;
    for (double w : weights) {
        total += w;
    }
    if (total <= 0) {
        return -1;
    }
    double r = uniform(rng) * total;
    for (int i = 0; i < weights.size(); ++i) {
        r -= weights.at(i);
        if (r <= 0) {
            return i;
        }
    }
    return weights.size() - 1;
}

} // namespace

QList<Entry> generate(const QList<Podcast> &podcasts, const Options &options)
{
    QList<Entry> result;
    QList<double> weights;
    QList<double> speeds;
    QList<int> pointer;
    Rng rng(options.seed);
    static const double kSpeeds[] = {1.0, 1.0, 1.2, 1.25, 1.3, 1.5, 1.6};
    for (const Podcast &p : podcasts) {
        weights << (p.episodes.isEmpty() ? 0.0 : qMax(0.0, p.weight));
        speeds << kSpeeds[rng() % 7];
        pointer << int(rng() % unsigned(qMax(1, p.episodes.size())));
    }
    double totalWeight = 0;
    for (double w : weights) {
        totalWeight += w;
    }
    if (totalWeight <= 0 || options.months < 1) {
        return result;
    }
    const QDate end = options.today.addDays(-1);
    const int days = options.months * 30;
    const QDate start = end.addDays(-(days - 1));
    // Two or three breaks (holidays, ill, travel)
    QList<QPair<int, int>> breaks;
    const int breakCount = 2 + int(rng() % 2);
    for (int i = 0; i < breakCount; ++i) {
        breaks << qMakePair(int(rng() % unsigned(qMax(1, days - 12))), 5 + int(rng() % 6));
    }
    for (int d = 0; d < days; ++d) {
        const QDate date = start.addDays(d);
        bool away = false;
        for (const QPair<int, int> &b : breaks) {
            if (d >= b.first && d < b.first + b.second) {
                away = true;
            }
        }
        if (away) {
            continue;
        }
        const int weekday = date.dayOfWeek();     // 1 Monday .. 7 Sunday
        const double active = weekday <= 5 ? 0.9 : (weekday == 6 ? 0.75 : 0.8);
        if (uniform(rng) > active) {
            continue;
        }
        const double trend = 0.75 + 0.5 * double(d) / double(qMax(1, days - 1));    // grows over the year
        const double weekend = weekday >= 6 ? 1.3 : 1.0;
        double remaining = options.hoursPerDay * 3600.0 * trend * weekend * lognormal(rng, 0.35);
        QMap<qint64, int> usedToday;     // episode -> index in result
        int sessions = 0;
        while (remaining >= 120 && sessions < 6) {
            ++sessions;
            const int pi = pickWeighted(rng, weights);
            if (pi < 0) {
                break;
            }
            const Podcast &p = podcasts.at(pi);
            const double share = sessions >= 3 || uniform(rng) < 0.45 ? remaining : remaining * (0.35 + 0.4 * uniform(rng));
            double left = share;
            int guard = 0;
            while (left >= 60 && guard++ < 4) {
                const Episode &e = p.episodes.at(pointer.at(pi) % p.episodes.size());
                // Longer than the time left: stays "in progress" and the next day starts the next one
                const double wall = e.duration > 0 ? std::min(left, double(e.duration) / speeds.at(pi)) : left;
                if (wall < 30) {
                    pointer[pi] += 1;
                    continue;
                }
                if (usedToday.contains(e.id)) {
                    Entry &old = result[usedToday.value(e.id)];
                    old.seconds += int(wall);
                    old.mediaSeconds += int(wall * speeds.at(pi));
                } else {
                    Entry entry;
                    entry.day = date.toString(QStringLiteral("yyyyMMdd")).toInt();
                    entry.episodeId = e.id;
                    entry.podcastId = p.id;
                    entry.podcastTitle = p.title;
                    entry.seconds = int(wall);
                    entry.mediaSeconds = int(wall * speeds.at(pi));
                    usedToday.insert(e.id, result.size());
                    result.append(entry);
                }
                left -= wall;
                if (e.duration <= 0 || wall >= double(e.duration) / speeds.at(pi) - 1) {
                    pointer[pi] += 1;       // finished: next episode
                } else {
                    break;                  // not finished: carry on tomorrow
                }
            }
            remaining -= share;
        }
    }
    return result;
}

void applyWeights(QList<Podcast> *podcasts, const QString &text, QStringList *unmatched)
{
    for (Podcast &p : *podcasts) {
        p.weight = 0;
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int eq = line.lastIndexOf(QLatin1Char('='));
        if (eq <= 0) {
            if (unmatched) {
                *unmatched << line;
            }
            continue;
        }
        const QString title = line.left(eq).trimmed();
        bool ok = false;
        const double weight = line.mid(eq + 1).trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        int found = -1;
        for (int i = 0; i < podcasts->size() && found < 0; ++i) {
            if (podcasts->at(i).title.compare(title, Qt::CaseInsensitive) == 0) {
                found = i;
            }
        }
        for (int i = 0; i < podcasts->size() && found < 0; ++i) {
            if (podcasts->at(i).title.contains(title, Qt::CaseInsensitive)) {
                found = i;
            }
        }
        if (found < 0 || !ok) {
            if (unmatched) {
                *unmatched << line;
            }
            continue;
        }
        (*podcasts)[found].weight = qMax(0.0, weight);
    }
}

QString weightsTemplate(const QList<Podcast> &podcasts)
{
    QString text = QStringLiteral("# Podcast = weight (relative share of the listening time; 0 = never)\n"
                                  "# Change the numbers, then: seed-demo-stats --weights THIS_FILE\n");
    for (const Podcast &p : podcasts) {
        text += QStringLiteral("%1 = %2\n").arg(p.title).arg(QString::number(p.weight, 'f', 1));
    }
    return text;
}

void defaultWeights(QList<Podcast> *podcasts, unsigned seed)
{
    QList<int> order;
    for (int i = 0; i < podcasts->size(); ++i) {
        if (!podcasts->at(i).episodes.isEmpty()) {
            order << i;
        }
        (*podcasts)[i].weight = 0;
    }
    Rng rng(seed);
    std::shuffle(order.begin(), order.end(), rng);
    // Zipf-like: 100, 52, 36 ... down to a tail that is heard rarely; the last third not at all
    const int active = qMax(1, order.size() * 2 / 3);
    for (int rank = 0; rank < order.size(); ++rank) {
        if (rank < active) {
            (*podcasts)[order.at(rank)].weight = 100.0 / std::pow(double(rank + 1), 1.0);
        }
    }
}

} // namespace DemoStats

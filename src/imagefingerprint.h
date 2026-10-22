// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef IMAGEFINGERPRINT_H
#define IMAGEFINGERPRINT_H

#include <QImage>
#include <QVector>
#include <QtGlobal>

// Tells whether two images show the same picture, even if they come from
// different URLs, in different sizes or encodings: many feeds give every
// episode the podcast cover as its "own" image. Both images are reduced to
// 16 x 16 pixels (aspect ratio ignored) and compared channel by channel.
namespace ImageFingerprint {

const int Size = 16;
// Mean difference per channel (0..255) up to which two images count as equal
const int Tolerance = 12;

inline QVector<int> of(const QImage &image)
{
    QVector<int> values;
    if (image.isNull()) {
        return values;
    }
    const QImage small = image.scaled(Size, Size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB32);
    values.reserve(Size * Size * 3);
    for (int y = 0; y < Size; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < Size; ++x) {
            values.append(qRed(line[x]));
            values.append(qGreen(line[x]));
            values.append(qBlue(line[x]));
        }
    }
    return values;
}

inline bool same(const QVector<int> &a, const QVector<int> &b)
{
    if (a.isEmpty() || a.size() != b.size()) {
        return false;
    }
    qint64 sum = 0;
    for (int i = 0; i < a.size(); ++i) {
        sum += qAbs(a.at(i) - b.at(i));
    }
    return sum / a.size() <= Tolerance;
}

inline bool same(const QImage &a, const QImage &b)
{
    return same(of(a), of(b));
}

} // namespace ImageFingerprint

#endif // IMAGEFINGERPRINT_H

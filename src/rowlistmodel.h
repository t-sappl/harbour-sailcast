// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROWLISTMODEL_H
#define ROWLISTMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QVector>

// Base of the list models: keeps rows with an "id" and applies a freshly
// loaded list as the smallest set of changes - rows removed, moved or
// inserted, the rest updated in place (dataChanged).
//
// A reset (beginResetModel) destroys and recreates every delegate of a
// list: images were loaded again, scroll positions jumped, and a running
// remorse timer of a removed row stayed behind on an empty row. With these
// changes, delegates of unchanged rows stay as they are.
template <typename Row>
class RowListModel : public QAbstractListModel
{
public:
    explicit RowListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

protected:
    // Returns true if the number of rows changed
    bool applyRows(const QList<Row> &fresh)
    {
        const int oldCount = m_rows.size();
        if (m_rows.isEmpty() || fresh.isEmpty()) {
            beginResetModel();
            m_rows = fresh;
            endResetModel();
            return oldCount != m_rows.size();
        }

        QSet<qint64> freshIds;
        for (int i = 0; i < fresh.size(); ++i) {
            freshIds.insert(fresh.at(i).id);
        }

        // 1. Rows that are gone (from the end, so the indices stay valid)
        for (int i = m_rows.size() - 1; i >= 0; --i) {
            if (!freshIds.contains(m_rows.at(i).id)) {
                int first = i;
                while (first > 0 && !freshIds.contains(m_rows.at(first - 1).id)) {
                    --first;
                }
                beginRemoveRows(QModelIndex(), first, i);
                for (int k = i; k >= first; --k) {
                    m_rows.removeAt(k);
                }
                endRemoveRows();
                i = first;
            }
        }

        // 2. Walk the new list: keep, move up or insert
        for (int i = 0; i < fresh.size(); ++i) {
            const qint64 id = fresh.at(i).id;
            if (i < m_rows.size() && m_rows.at(i).id == id) {
                m_rows[i] = fresh.at(i);
                continue;
            }
            int from = -1;
            for (int j = i + 1; j < m_rows.size(); ++j) {
                if (m_rows.at(j).id == id) {
                    from = j;
                    break;
                }
            }
            if (from > i) {
                beginMoveRows(QModelIndex(), from, from, QModelIndex(), i);
                m_rows.move(from, i);
                endMoveRows();
                m_rows[i] = fresh.at(i);
            } else {
                beginInsertRows(QModelIndex(), i, i);
                m_rows.insert(i, fresh.at(i));
                endInsertRows();
            }
        }

        // Duplicates in the new list would leave extra rows - never expected
        if (m_rows.size() > fresh.size()) {
            beginRemoveRows(QModelIndex(), fresh.size(), m_rows.size() - 1);
            while (m_rows.size() > fresh.size()) {
                m_rows.removeLast();
            }
            endRemoveRows();
        }

        if (!m_rows.isEmpty()) {
            emit dataChanged(index(0), index(m_rows.size() - 1));
        }
        return oldCount != m_rows.size();
    }

    int rowOf(qint64 id) const
    {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).id == id) {
                return i;
            }
        }
        return -1;
    }

    // Moves a row (drag and drop in the playlist)
    bool moveRow(int from, int to)
    {
        if (from < 0 || from >= m_rows.size() || to < 0 || to >= m_rows.size() || from == to) {
            return false;
        }
        // beginMoveRows wants the destination index as it is before the move
        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
        m_rows.move(from, to);
        endMoveRows();
        return true;
    }

    QList<Row> m_rows;
};

#endif // ROWLISTMODEL_H

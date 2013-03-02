/*

Copyright 2013 Adam Reichold

This file is part of qpdfview.

qpdfview is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

qpdfview is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with qpdfview.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef SHORTCUTSTABLEMODEL_H
#define SHORTCUTSTABLEMODEL_H

#include <QAbstractTableModel>
#include <QKeySequence>

class QAction;

class ShortcutsTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    ShortcutsTableModel(const QSet< QAction* >& actions, const QMap< QAction*, QKeySequence >& defaultShortcuts, QObject* parent = 0);

    int columnCount(const QModelIndex& parent) const;
    int rowCount(const QModelIndex& parent) const;

    Qt::ItemFlags flags(const QModelIndex& index) const;

    QVariant headerData(int section, Qt::Orientation orientation, int role) const;

    QVariant data(const QModelIndex& index, int role) const;
    bool setData(const QModelIndex& index, const QVariant& value, int role);

    void accept();
    void reset();

private:
    QList< QAction* > m_actions;

    QMap< QAction*, QKeySequence > m_defaultShortcuts;
    QMap< QAction*, QKeySequence > m_shortcuts;

};

#endif // SHORTCUTSTABLEMODEL_H

/*
 * Copyright (C) 2024 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COLLAPSERULESDIALOG_H_
#define COLLAPSERULESDIALOG_H_

#include <QDialog>

#include "collapserules.h"

class QTableWidget;
class QToolButton;
class QDialogButtonBox;
class QAbstractButton;

class CollapseRulesDialog : public QDialog {
    Q_OBJECT

  public:
    explicit CollapseRulesDialog( QWidget* parent = nullptr );

  Q_SIGNALS:
    void optionsChanged();

  private Q_SLOTS:
    void addRule();
    void removeRule();
    void moveRuleUp();
    void moveRuleDown();
    void exportRules();
    void importRules();
    void resolveStandardButton( QAbstractButton* button );

  private:
    using Collection = CollapseRulesCollection::Collection;

    void populateTable( const Collection& rules );
    Collection readTable() const;
    void saveSettings();
    void addRuleRow( const CollapseRule& rule = {} );
    void updateButtons();

    QTableWidget* rulesTable_;
    QToolButton* addButton_;
    QToolButton* removeButton_;
    QToolButton* importButton_;
    QToolButton* exportButton_;
    QToolButton* upButton_;
    QToolButton* downButton_;
    QDialogButtonBox* buttonBox_;
};

#endif

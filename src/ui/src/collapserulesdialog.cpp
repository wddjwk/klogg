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

#include "collapserulesdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int COL_NAME = 0;
constexpr int COL_TYPE = 1;
constexpr int COL_PATTERN = 2;
constexpr int COL_ENABLED = 3;
} // namespace

class CenteredCheckBox : public QWidget {
    Q_OBJECT
  public:
    explicit CenteredCheckBox( QWidget* parent = nullptr )
        : QWidget( parent )
    {
        auto* layout = new QHBoxLayout( this );
        layout->setContentsMargins( 0, 0, 0, 0 );
        layout->addStretch();
        checkBox_ = new QCheckBox( this );
        layout->addWidget( checkBox_ );
        layout->addStretch();
        setLayout( layout );

        auto pal = palette();
        pal.setColor( QPalette::Window, pal.color( QPalette::Base ) );
        setAutoFillBackground( true );
        setPalette( pal );
    }

    QCheckBox* checkBox() const
    {
        return checkBox_;
    }

  private:
    QCheckBox* checkBox_;
};

CollapseRulesDialog::CollapseRulesDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Collapse Rules" ) );
    resize( 700, 400 );

    auto* mainLayout = new QVBoxLayout( this );

    auto* toolbarLayout = new QHBoxLayout;

    addButton_ = new QToolButton( this );
    addButton_->setText( "+" );
    addButton_->setToolTip( tr( "Add rule" ) );
    toolbarLayout->addWidget( addButton_ );

    removeButton_ = new QToolButton( this );
    removeButton_->setText( "-" );
    removeButton_->setToolTip( tr( "Remove rule" ) );
    toolbarLayout->addWidget( removeButton_ );

    toolbarLayout->addStretch();

    importButton_ = new QToolButton( this );
    importButton_->setText( tr( "Import" ) );
    toolbarLayout->addWidget( importButton_ );

    exportButton_ = new QToolButton( this );
    exportButton_->setText( tr( "Export" ) );
    toolbarLayout->addWidget( exportButton_ );

    toolbarLayout->addStretch();

    upButton_ = new QToolButton( this );
    upButton_->setText( tr( "Up" ) );
    toolbarLayout->addWidget( upButton_ );

    downButton_ = new QToolButton( this );
    downButton_->setText( tr( "Down" ) );
    toolbarLayout->addWidget( downButton_ );

    mainLayout->addLayout( toolbarLayout );

    rulesTable_ = new QTableWidget( this );
    rulesTable_->setColumnCount( 4 );
    rulesTable_->setHorizontalHeaderLabels(
        { tr( "Name" ), tr( "Type" ), tr( "Pattern / Parameter" ), tr( "Enabled" ) } );
    rulesTable_->horizontalHeader()->setStretchLastSection( false );
    rulesTable_->horizontalHeader()->setSectionResizeMode( COL_NAME,
                                                           QHeaderView::ResizeToContents );
    rulesTable_->horizontalHeader()->setSectionResizeMode( COL_TYPE,
                                                           QHeaderView::ResizeToContents );
    rulesTable_->horizontalHeader()->setSectionResizeMode( COL_PATTERN, QHeaderView::Stretch );
    rulesTable_->horizontalHeader()->setSectionResizeMode( COL_ENABLED,
                                                           QHeaderView::ResizeToContents );
    rulesTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    rulesTable_->verticalHeader()->setVisible( false );
    mainLayout->addWidget( rulesTable_ );

    buttonBox_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                           | QDialogButtonBox::Apply,
                                       this );
    mainLayout->addWidget( buttonBox_ );

    setLayout( mainLayout );

    connect( addButton_, &QToolButton::clicked, this, &CollapseRulesDialog::addRule );
    connect( removeButton_, &QToolButton::clicked, this, &CollapseRulesDialog::removeRule );
    connect( upButton_, &QToolButton::clicked, this, &CollapseRulesDialog::moveRuleUp );
    connect( downButton_, &QToolButton::clicked, this, &CollapseRulesDialog::moveRuleDown );
    connect( importButton_, &QToolButton::clicked, this, &CollapseRulesDialog::importRules );
    connect( exportButton_, &QToolButton::clicked, this, &CollapseRulesDialog::exportRules );
    connect( buttonBox_, &QDialogButtonBox::clicked, this,
             &CollapseRulesDialog::resolveStandardButton );
    connect( rulesTable_, &QTableWidget::currentCellChanged, this,
             [ this ]( int, int, int, int ) { updateButtons(); } );

    auto rules = CollapseRulesCollection::getSynced().getSyncedRules();
    populateTable( rules );
    updateButtons();
}

void CollapseRulesDialog::populateTable( const Collection& rules )
{
    rulesTable_->setRowCount( 0 );
    for ( const auto& rule : rules ) {
        addRuleRow( rule );
    }
}

void CollapseRulesDialog::addRuleRow( const CollapseRule& rule )
{
    int row = rulesTable_->rowCount();
    rulesTable_->insertRow( row );

    rulesTable_->setItem( row, COL_NAME, new QTableWidgetItem( rule.name ) );

    auto* typeCombo = new QComboBox();
    typeCombo->addItem( tr( "Prefix match (from position N)" ) );
    typeCombo->addItem( tr( "Regex match" ) );
    typeCombo->addItem( tr( "Contains string" ) );
    typeCombo->setCurrentIndex( static_cast<int>( rule.type ) );
    rulesTable_->setCellWidget( row, COL_TYPE, typeCombo );

    QString paramText;
    if ( rule.type == CollapseRule::Type::PrefixMatch ) {
        paramText = QString::number( rule.prefixLength );
    }
    else {
        paramText = rule.pattern;
    }
    rulesTable_->setItem( row, COL_PATTERN, new QTableWidgetItem( paramText ) );

    auto* cbWidget = new CenteredCheckBox( rulesTable_ );
    cbWidget->checkBox()->setChecked( rule.enabled );
    rulesTable_->setCellWidget( row, COL_ENABLED, cbWidget );
}

CollapseRulesDialog::Collection CollapseRulesDialog::readTable() const
{
    Collection rules;

    for ( int row = 0; row < rulesTable_->rowCount(); ++row ) {
        auto* nameItem = rulesTable_->item( row, COL_NAME );
        if ( !nameItem || nameItem->text().isEmpty() ) {
            continue;
        }

        CollapseRule rule;
        rule.name = nameItem->text();

        auto* typeCombo = qobject_cast<QComboBox*>( rulesTable_->cellWidget( row, COL_TYPE ) );
        rule.type = static_cast<CollapseRule::Type>( typeCombo ? typeCombo->currentIndex() : 0 );

        auto* patternItem = rulesTable_->item( row, COL_PATTERN );
        QString paramText = patternItem ? patternItem->text() : QString();

        if ( rule.type == CollapseRule::Type::PrefixMatch ) {
            rule.prefixLength = paramText.toInt();
        }
        else {
            rule.pattern = paramText;
        }

        auto* cbWidget
            = qobject_cast<CenteredCheckBox*>( rulesTable_->cellWidget( row, COL_ENABLED ) );
        rule.enabled = cbWidget ? cbWidget->checkBox()->isChecked() : true;

        rules.append( rule );
    }

    return rules;
}

void CollapseRulesDialog::saveSettings()
{
    auto rules = readTable();
    CollapseRulesCollection::getSynced().saveToStorage( rules );
    Q_EMIT optionsChanged();
}

void CollapseRulesDialog::addRule()
{
    CollapseRule defaultRule;
    defaultRule.name = tr( "New rule" );
    defaultRule.type = CollapseRule::Type::PrefixMatch;
    defaultRule.prefixLength = 10;
    defaultRule.enabled = true;

    addRuleRow( defaultRule );
    rulesTable_->scrollToBottom();
    rulesTable_->editItem( rulesTable_->item( rulesTable_->rowCount() - 1, COL_NAME ) );
    updateButtons();
}

void CollapseRulesDialog::removeRule()
{
    int row = rulesTable_->currentRow();
    if ( row >= 0 ) {
        rulesTable_->removeRow( row );
    }
    updateButtons();
}

void CollapseRulesDialog::moveRuleUp()
{
    int row = rulesTable_->currentRow();
    if ( row > 0 ) {
        // Swap name
        auto* item1 = rulesTable_->takeItem( row - 1, COL_NAME );
        auto* item2 = rulesTable_->takeItem( row, COL_NAME );
        rulesTable_->setItem( row - 1, COL_NAME, item2 );
        rulesTable_->setItem( row, COL_NAME, item1 );

        // Swap pattern
        auto* pat1 = rulesTable_->takeItem( row - 1, COL_PATTERN );
        auto* pat2 = rulesTable_->takeItem( row, COL_PATTERN );
        rulesTable_->setItem( row - 1, COL_PATTERN, pat2 );
        rulesTable_->setItem( row, COL_PATTERN, pat1 );

        // Swap type
        auto* type1 = qobject_cast<QComboBox*>( rulesTable_->cellWidget( row - 1, COL_TYPE ) );
        auto* type2 = qobject_cast<QComboBox*>( rulesTable_->cellWidget( row, COL_TYPE ) );
        if ( type1 && type2 ) {
            int idx = type1->currentIndex();
            type1->setCurrentIndex( type2->currentIndex() );
            type2->setCurrentIndex( idx );
        }

        // Swap enabled
        auto* cb1 = qobject_cast<CenteredCheckBox*>( rulesTable_->cellWidget( row - 1, COL_ENABLED ) );
        auto* cb2 = qobject_cast<CenteredCheckBox*>( rulesTable_->cellWidget( row, COL_ENABLED ) );
        if ( cb1 && cb2 ) {
            bool checked = cb1->checkBox()->isChecked();
            cb1->checkBox()->setChecked( cb2->checkBox()->isChecked() );
            cb2->checkBox()->setChecked( checked );
        }

        rulesTable_->setCurrentCell( row - 1, 0 );
    }
    updateButtons();
}

void CollapseRulesDialog::moveRuleDown()
{
    int row = rulesTable_->currentRow();
    if ( row >= 0 && row < rulesTable_->rowCount() - 1 ) {
        rulesTable_->setCurrentCell( row + 1, 0 );

        // Swap name
        auto* item1 = rulesTable_->takeItem( row, COL_NAME );
        auto* item2 = rulesTable_->takeItem( row + 1, COL_NAME );
        rulesTable_->setItem( row, COL_NAME, item2 );
        rulesTable_->setItem( row + 1, COL_NAME, item1 );

        // Swap pattern
        auto* pat1 = rulesTable_->takeItem( row, COL_PATTERN );
        auto* pat2 = rulesTable_->takeItem( row + 1, COL_PATTERN );
        rulesTable_->setItem( row, COL_PATTERN, pat2 );
        rulesTable_->setItem( row + 1, COL_PATTERN, pat1 );

        // Swap type
        auto* type1 = qobject_cast<QComboBox*>( rulesTable_->cellWidget( row, COL_TYPE ) );
        auto* type2 = qobject_cast<QComboBox*>( rulesTable_->cellWidget( row + 1, COL_TYPE ) );
        if ( type1 && type2 ) {
            int idx = type1->currentIndex();
            type1->setCurrentIndex( type2->currentIndex() );
            type2->setCurrentIndex( idx );
        }

        // Swap enabled
        auto* cb1 = qobject_cast<CenteredCheckBox*>( rulesTable_->cellWidget( row, COL_ENABLED ) );
        auto* cb2 = qobject_cast<CenteredCheckBox*>( rulesTable_->cellWidget( row + 1, COL_ENABLED ) );
        if ( cb1 && cb2 ) {
            bool checked = cb1->checkBox()->isChecked();
            cb1->checkBox()->setChecked( cb2->checkBox()->isChecked() );
            cb2->checkBox()->setChecked( checked );
        }

        rulesTable_->setCurrentCell( row + 1, 0 );
    }
    updateButtons();
}

void CollapseRulesDialog::resolveStandardButton( QAbstractButton* button )
{
    auto role = buttonBox_->buttonRole( button );
    if ( role == QDialogButtonBox::ApplyRole || role == QDialogButtonBox::AcceptRole ) {
        saveSettings();
    }
    if ( role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::RejectRole ) {
        close();
    }
}

void CollapseRulesDialog::updateButtons()
{
    int row = rulesTable_->currentRow();
    removeButton_->setEnabled( rulesTable_->rowCount() > 0 );
    upButton_->setEnabled( row > 0 );
    downButton_->setEnabled( row >= 0 && row < rulesTable_->rowCount() - 1 );
}

void CollapseRulesDialog::exportRules()
{
    auto fileName = QFileDialog::getSaveFileName( this, tr( "Export Collapse Rules" ), QString(),
                                                  tr( "Config files (*.conf)" ) );
    if ( fileName.isEmpty() ) {
        return;
    }

    auto rules = readTable();
    QSettings fileSettings( fileName, QSettings::IniFormat );
    fileSettings.beginGroup( "CollapseRulesCollection" );
    fileSettings.setValue( "version", 1 );
    fileSettings.remove( "rules" );
    fileSettings.beginWriteArray( "rules" );
    int arrayIndex = 0;
    for ( const auto& rule : rules ) {
        fileSettings.setArrayIndex( arrayIndex );
        fileSettings.setValue( "name", rule.name );
        fileSettings.setValue( "type", static_cast<int>( rule.type ) );
        fileSettings.setValue( "pattern", rule.pattern );
        fileSettings.setValue( "prefixLength", rule.prefixLength );
        fileSettings.setValue( "enabled", rule.enabled );
        arrayIndex++;
    }
    fileSettings.endArray();
    fileSettings.endGroup();
    fileSettings.sync();
}

void CollapseRulesDialog::importRules()
{
    auto fileName = QFileDialog::getOpenFileName( this, tr( "Import Collapse Rules" ), QString(),
                                                  tr( "Config files (*.conf)" ) );
    if ( fileName.isEmpty() ) {
        return;
    }

    QSettings fileSettings( fileName, QSettings::IniFormat );
    fileSettings.beginGroup( "CollapseRulesCollection" );

    Collection importedRules;
    int size = fileSettings.beginReadArray( "rules" );
    importedRules.reserve( size );
    for ( int i = 0; i < size; ++i ) {
        fileSettings.setArrayIndex( i );
        CollapseRule rule;
        rule.name = fileSettings.value( "name" ).toString();
        rule.type = static_cast<CollapseRule::Type>( fileSettings.value( "type", 0 ).toInt() );
        rule.pattern = fileSettings.value( "pattern" ).toString();
        rule.prefixLength = fileSettings.value( "prefixLength", 0 ).toInt();
        rule.enabled = fileSettings.value( "enabled", true ).toBool();
        importedRules.append( rule );
    }
    fileSettings.endArray();
    fileSettings.endGroup();

    populateTable( importedRules );
}

#include "collapserulesdialog.moc"

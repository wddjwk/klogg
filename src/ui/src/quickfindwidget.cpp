/*
 * Copyright (C) 2010, 2013 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include "log.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>
#include <qcheckbox.h>
#include <qkeysequence.h>
#include <qlineedit.h>
#include <qregularexpression.h>

#include "configuration.h"
#include "qfnotifications.h"

#include "quickfindwidget.h"

static constexpr int NotificationTimeout = 5000;

const QString QFNotification::REACHED_EOF = "Reached end of file, no occurrence found.";
const QString QFNotification::REACHED_BOF = "Reached beginning of file, no occurrence found.";
const QString QFNotification::INTERRUPTED = "Search interrupted";

QuickFindWidget::QuickFindWidget( QWidget* parent )
    : QWidget( parent )
{
    auto* layout = new QHBoxLayout( this );

    layout->setContentsMargins( 6, 0, 6, 6 );

    closeButton_
        = setupToolButton( QLatin1String( "" ), QLatin1String( ":/images/darkclosebutton.png" ) );
    closeButton_->setShortcut( QKeySequence::Cancel );
    layout->addWidget( closeButton_ );

    editQuickFind_ = new QLineEdit( this );
    editQuickFind_->setMinimumSize( QSize( 150, 0 ) );
    layout->addWidget( editQuickFind_ );

    ignoreCaseCheck_ = new QCheckBox( "Ignore &case" );
    ignoreCaseCheck_->setChecked( true );
    layout->addWidget( ignoreCaseCheck_ );

    regexCheck_ = new QCheckBox( "&regex" );
    regexCheck_->setChecked( true );
    layout->addWidget( regexCheck_ );

    previousButton_
        = setupToolButton( QLatin1String( "Previous" ), QLatin1String( ":/images/arrowup.png" ) );
    previousButton_->setShortcut( QKeySequence::FindPrevious );
    layout->addWidget( previousButton_ );

    nextButton_
        = setupToolButton( QLatin1String( "Next" ), QLatin1String( ":/images/arrowdown.png" ) );
    nextButton_->setShortcut( QKeySequence::FindNext );
    layout->addWidget( nextButton_ );

    matchCountLabel_ = new QLabel( "" );
    matchCountLabel_->setMinimumWidth( 50 );
    matchCountLabel_->setAlignment( Qt::AlignCenter );
    layout->addWidget( matchCountLabel_ );

    notificationText_ = new QLabel( "" );
    int width = QFNotification::maxWidth( notificationText_ );
    notificationText_->setMinimumSize( width, 0 );
    layout->addWidget( notificationText_ );

    setMinimumWidth( minimumSizeHint().width() );

    // Behaviour
    connect( closeButton_, &QToolButton::clicked, this, &QuickFindWidget::closeHandler );
    connect( editQuickFind_, &QLineEdit::textEdited, this, &QuickFindWidget::textChanged );

    connect( ignoreCaseCheck_, &QCheckBox::stateChanged, this, [ this ] {
        textChanged();
        Configuration::get().setQfIgnoreCase( ignoreCaseCheck_->isChecked() );
        Configuration::get().save();
    } );

    connect( regexCheck_, &QCheckBox::stateChanged, this, [ this ] { textChanged(); } );

    connect( previousButton_, &QToolButton::clicked, this, &QuickFindWidget::doSearchBackward );
    connect( nextButton_, &QToolButton::clicked, this, &QuickFindWidget::doSearchForward );

    // Install event filter on the line edit to intercept Enter/Shift+Enter
    editQuickFind_->installEventFilter( this );

    // Notification timer:
    notificationTimer_ = new QTimer( this );
    notificationTimer_->setSingleShot( true );
    connect( notificationTimer_, SIGNAL( timeout() ), this, SLOT( notificationTimeout() ) );
}

void QuickFindWidget::userActivate()
{
    userRequested_ = true;
    QWidget::show();
    editQuickFind_->setFocus( Qt::ShortcutFocusReason );
    editQuickFind_->selectAll();
}

bool QuickFindWidget::isActive() const
{
    return isVisible();
}

void QuickFindWidget::closeWidget()
{
    closeHandler();
}

//
// Q_SLOTS:
//

void QuickFindWidget::changeDisplayedPattern( const QString& newPattern, bool regex )
{
    auto pattern
        = ( !regex && isRegex() ) ? QRegularExpression::escape( newPattern ) : newPattern;
    editQuickFind_->setText( pattern );
    editQuickFind_->setCursorPosition( patternCursorPosition_ );
}

void QuickFindWidget::notify( const QFNotification& message )
{
    LOG_DEBUG << "QuickFindWidget::notify()";

    notificationText_->setText( message.message() );
    QWidget::show();
    notificationTimer_->start( NotificationTimeout );
}

void QuickFindWidget::clearNotification()
{
    LOG_DEBUG << "QuickFindWidget::clearNotification()";

    notificationText_->setText( "" );
}

void QuickFindWidget::updateMatchCount( int current, int total )
{
    if ( total == 0 ) {
        matchCountLabel_->setText( "" );
    }
    else {
        matchCountLabel_->setText( QStringLiteral( "%1 / %2" ).arg( current ).arg( total ) );
    }
}

void QuickFindWidget::doSearchForward()
{
    LOG_DEBUG << "QuickFindWidget::doSearchForward()";

    userRequested_ = true;

    Q_EMIT patternConfirmed( editQuickFind_->text(), isIgnoreCase(), isRegex() );
    Q_EMIT searchForward();

    editQuickFind_->setFocus();
}

void QuickFindWidget::doSearchBackward()
{
    LOG_DEBUG << "QuickFindWidget::doSearchBackward()";

    userRequested_ = true;

    Q_EMIT patternConfirmed( editQuickFind_->text(), isIgnoreCase(), isRegex() );
    Q_EMIT searchBackward();

    editQuickFind_->setFocus();
}

void QuickFindWidget::closeHandler()
{
    userRequested_ = false;
    this->hide();
    Q_EMIT close();
    Q_EMIT cancelSearch();
}

void QuickFindWidget::notificationTimeout()
{
    notificationText_->setText( "" );
}

void QuickFindWidget::textChanged()
{
    patternCursorPosition_ = editQuickFind_->cursorPosition();
    Q_EMIT patternUpdated( editQuickFind_->text(), isIgnoreCase(), isRegex() );
}

bool QuickFindWidget::eventFilter( QObject* obj, QEvent* event )
{
    if ( obj == editQuickFind_ && event->type() == QEvent::KeyPress ) {
        auto* keyEvent = static_cast<QKeyEvent*>( event );
        if ( keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter ) {
            if ( keyEvent->modifiers() & Qt::ShiftModifier ) {
                doSearchBackward();
            }
            else {
                doSearchForward();
            }
            return true;
        }
    }
    return QWidget::eventFilter( obj, event );
}

//
// Private functions
//
QToolButton* QuickFindWidget::setupToolButton( const QString& text, const QString& icon )
{
    auto* toolButton = new QToolButton( this );

    toolButton->setAutoRaise( true );
    toolButton->setIcon( QIcon( icon ) );

    if ( text.size() > 0 ) {
        toolButton->setText( text );
        toolButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    }
    else {
        toolButton->setToolButtonStyle( Qt::ToolButtonIconOnly );
    }

    return toolButton;
}

bool QuickFindWidget::isIgnoreCase() const
{
    return ( ignoreCaseCheck_->checkState() == Qt::Checked );
}

bool QuickFindWidget::isRegex() const
{
    return regexCheck_->isChecked();
}

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

// This file implements QuickFind.
// This class implements the Quick Find mechanism using references
// to the logData, the QFP and the selection passed.
// Search is started just after the selection and the selection is updated
// if a match is found.

#include <QtConcurrent>

#include "abstractlogdata.h"
#include "dispatch_to.h"
#include "linetypes.h"
#include "log.h"
#include "quickfindpattern.h"
#include "selection.h"

#include "quickfind.h"

void SearchingNotifier::reset()
{
    dotToDisplay_ = 0;
    startTime_ = QTime::currentTime();
}

void SearchingNotifier::sendNotification( LineNumber current_line, LinesCount nb_lines,
                                          bool backward )
{
    LOG_DEBUG << "Emitting Searching....";
    const auto progress = static_cast<int>(
        backward ? ( ( nb_lines.get() - current_line.get() ) / nb_lines.get() * 100 )
                 : ( current_line.get() / nb_lines.get() ) * 100 );

    Q_EMIT notify( QFNotificationProgress( progress ) );
    startTime_ = QTime::currentTime().addMSecs( -800 );
}

void QuickFind::LastMatchPosition::set( LineNumber line, LineColumn column )
{
    if ( ( !line_.has_value() ) || ( ( line <= *line_ ) && ( column < column_ ) ) ) {
        line_ = line;
        column_ = column;
    }
}

void QuickFind::LastMatchPosition::set( const FilePosition& position )
{
    set( position.line(), position.column() );
}

bool QuickFind::LastMatchPosition::isLater( OptionalLineNumber line, LineColumn column ) const
{
    if ( !line_.has_value() || !line.has_value() )
        return false;
    else if ( ( *line == *line_ ) && ( column >= column_ ) )
        return true;
    else if ( *line > *line_ )
        return true;
    else
        return false;
}

bool QuickFind::LastMatchPosition::isLater( const FilePosition& position ) const
{
    return isLater( position.line(), position.column() );
}

bool QuickFind::LastMatchPosition::isSooner( OptionalLineNumber line, LineColumn column ) const
{
    if ( !line_.has_value() || !line.has_value() )
        return false;
    else if ( ( *line == *line_ ) && ( column <= column_ ) )
        return true;
    else if ( *line < *line_ )
        return true;
    else
        return false;
}

bool QuickFind::LastMatchPosition::isSooner( const FilePosition& position ) const
{
    return isSooner( position.line(), position.column() );
}

QuickFind::QuickFind( const AbstractLogData& logData )
    : logData_( logData )
    , searchingNotifier_()
    , incrementalSearchStatus_()
{
    connect( &searchingNotifier_, &SearchingNotifier::notify, this, &QuickFind::sendNotification,
             Qt::DirectConnection );

    connect( &operationWatcher_, &QFutureWatcher<Portion>::finished, this,
             &QuickFind::onSearchFutureReady );

    connect( &scanWatcher_, &QFutureWatcher<std::vector<MatchInfo>>::finished, this,
             &QuickFind::onScanFutureReady );
}

Selection QuickFind::incrementalSearchStop()
{
    if ( incrementalSearchStatus_.isOngoing() ) {
        Selection s = incrementalSearchStatus_.initialSelection();
        incrementalSearchStatus_ = IncrementalSearchStatus();
        interruptRequested_.set();

        // If scan already found matches and queued a searchDone,
        // don't restore the old selection — the scan result will take over.
        if ( !allMatches_.empty() && currentMatchIndex_ >= 0 ) {
            return Selection{};
        }
        return s;
    }
    else {
        return Selection{};
    }
}

Selection QuickFind::incrementalSearchAbort()
{
    if ( incrementalSearchStatus_.isOngoing() ) {
        Selection s = incrementalSearchStatus_.initialSelection();
        incrementalSearchStatus_ = IncrementalSearchStatus();
        interruptRequested_.set();
        return s;
    }
    else {
        return Selection{};
    }
}

void QuickFind::stopSearch()
{
    LOG_INFO << "Stop search for quickfind " << this;
    interruptRequested_.set();
    operationWatcher_.waitForFinished();
    scanWatcher_.waitForFinished();
}

void QuickFind::onSearchFutureReady()
{
    auto selection = operationFuture_.result();

    if ( selection.isValid() ) {
        Q_EMIT searchDone( true, selection );
    }
    else if ( incrementalSearchStatus_.direction() != None ) {
        Q_EMIT searchDone( false, Portion{ incrementalSearchStatus_.position().line(), 0_lcol, 0_lcol } );
    }
    else {
        Q_EMIT searchDone( false, selection );
    }
}

void QuickFind::onScanFutureReady()
{
    allMatches_ = scanWatcher_.result();

    LOG_DEBUG << "QuickFind::onScanFutureReady - found " << allMatches_.size() << " matches";

    if ( allMatches_.empty() ) {
        currentMatchIndex_ = -1;
        Q_EMIT matchCountUpdated( 0, 0 );
        Q_EMIT searchDone( false, Portion{} );
        if ( incrementalSearchStatus_.direction() != None ) {
            Q_EMIT searchDone( false,
                               Portion{ incrementalSearchStatus_.position().line(), 0_lcol, 0_lcol } );
        }
        return;
    }

    // Find the first match from the initial search position
    int startIdx = 0;
    if ( incrementalSearchStatus_.isOngoing() ) {
        auto startPos = incrementalSearchStatus_.position();
        startIdx = findMatchIndexAfter( startPos.line(), startPos.column() );
        if ( startIdx < 0 )
            startIdx = 0; // wrap to beginning
    }

    currentMatchIndex_ = startIdx;
    navigateToMatch( startIdx );
}

void QuickFind::scanAllMatches( const QuickFindMatcher& matcher )
{
    LOG_DEBUG << "QuickFind::scanAllMatches";

    // Cancel any ongoing scan
    scanWatcher_.cancel();
    scanWatcher_.waitForFinished();

    // Clear previous matches
    allMatches_.clear();
    currentMatchIndex_ = -1;

    if ( !matcher.isActive() ) {
        Q_EMIT matchCountUpdated( 0, 0 );
        return;
    }

    const auto regexp = matcher.regexp();

#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
    scanFuture_ = QtConcurrent::run( [this, regexp]() {
#else
    scanFuture_ = QtConcurrent::run( [this, regexp]() -> std::vector<MatchInfo> {
#endif
        std::vector<MatchInfo> results;
        const auto nb_lines = logData_.getNbLine();
        for ( auto line = 0_lnum; line < nb_lines; ++line ) {
            const QString lineText = logData_.getExpandedLineString( line );
            auto it = regexp.globalMatch( lineText );
            while ( it.hasNext() ) {
                auto m = it.next();
                results.push_back(
                    { line, LineColumn{ m.capturedStart() }, LineColumn{ m.capturedEnd() - 1 } } );
            }
        }
        return results;
    } );

    scanWatcher_.setFuture( scanFuture_ );
}

void QuickFind::navigateToMatch( int index )
{
    if ( index < 0 || index >= static_cast<int>( allMatches_.size() ) ) {
        Q_EMIT searchDone( false, Portion{} );
        Q_EMIT matchCountUpdated( 0, static_cast<int>( allMatches_.size() ) );
        return;
    }

    const auto& m = allMatches_[index];
    Q_EMIT clearNotification();
    Q_EMIT searchDone( true, Portion{ m.line, m.startCol, m.endCol } );
    Q_EMIT matchCountUpdated( index + 1, static_cast<int>( allMatches_.size() ) );
}

int QuickFind::findMatchIndexAfter( LineNumber line, LineColumn col ) const
{
    if ( allMatches_.empty() )
        return -1;

    // Binary search for the first match at or after (line, col)
    int lo = 0;
    int hi = static_cast<int>( allMatches_.size() ) - 1;
    int result = -1;

    while ( lo <= hi ) {
        int mid = lo + ( hi - lo ) / 2;
        const auto& m = allMatches_[mid];
        if ( m.line > line || ( m.line == line && m.startCol >= col ) ) {
            result = mid;
            hi = mid - 1;
        }
        else {
            lo = mid + 1;
        }
    }

    // Wrap around to the first match if nothing found after current position
    if ( result < 0 )
        result = 0;

    return result;
}

int QuickFind::findMatchIndexBefore( LineNumber line, LineColumn col ) const
{
    if ( allMatches_.empty() )
        return -1;

    // Binary search for the last match before (line, col)
    int lo = 0;
    int hi = static_cast<int>( allMatches_.size() ) - 1;
    int result = -1;

    while ( lo <= hi ) {
        int mid = lo + ( hi - lo ) / 2;
        const auto& m = allMatches_[mid];
        if ( m.line < line || ( m.line == line && m.endCol < col ) ) {
            result = mid;
            lo = mid + 1;
        }
        else {
            hi = mid - 1;
        }
    }

    // Wrap around to the last match if nothing found before current position
    if ( result < 0 )
        result = static_cast<int>( allMatches_.size() ) - 1;

    return result;
}

int QuickFind::currentMatchIndex() const
{
    return currentMatchIndex_ >= 0 ? currentMatchIndex_ + 1 : 0;
}

int QuickFind::totalMatches() const
{
    return static_cast<int>( allMatches_.size() );
}

void QuickFind::incrementallySearchForward( Selection selection, QuickFindMatcher matcher )
{
    LOG_DEBUG << "QuickFind::incrementallySearchForward";

    interruptRequested_.set();
    operationWatcher_.waitForFinished();

    // Position where we start the search from
    FilePosition start_position = selection.getNextPosition();

    if ( incrementalSearchStatus_.direction() == Forward ) {
        // An incremental search is active, we restart the search
        // from the initial point
        LOG_DEBUG << "Restart search from initial point";
        start_position = incrementalSearchStatus_.position();
    }
    else {
        // It's a new search so we search from the selection
        incrementalSearchStatus_ = IncrementalSearchStatus( Forward, start_position, selection );
    }

    // Trigger async scan of all matches
    scanAllMatches( matcher );
}

void QuickFind::incrementallySearchBackward( Selection selection, QuickFindMatcher matcher )
{
    LOG_DEBUG << "QuickFind::incrementallySearchBackward";

    interruptRequested_.set();
    operationWatcher_.waitForFinished();

    // Position where we start the search from
    FilePosition start_position = selection.getPreviousPosition();

    if ( incrementalSearchStatus_.direction() == Backward ) {
        // An incremental search is active, we restart the search
        // from the initial point
        LOG_DEBUG << "Restart search from initial point";
        start_position = incrementalSearchStatus_.position();
    }
    else {
        // It's a new search so we search from the selection
        incrementalSearchStatus_ = IncrementalSearchStatus( Backward, start_position, selection );
    }

    // Trigger async scan of all matches
    scanAllMatches( matcher );
}

void QuickFind::searchForward( Selection selection, QuickFindMatcher matcher )
{
    LOG_DEBUG << "QuickFind::searchForward";

    incrementalSearchStatus_ = IncrementalSearchStatus();

    // Wait for any ongoing scan to complete so we use fresh results
    scanWatcher_.waitForFinished();

    if ( !allMatches_.empty() ) {
        // Navigate to the next match after the current selection, with wrap-around
        auto nextPos = selection.getNextPosition();
        int idx = findMatchIndexAfter( nextPos.line(), nextPos.column() );
        if ( idx >= 0 ) {
            currentMatchIndex_ = idx;
            navigateToMatch( idx );
        }
        else {
            Q_EMIT searchDone( false, Portion{} );
            sendNotification( QFNotificationReachedEndOfFile{} );
        }
        return;
    }

    // Fallback: no matches found, use old line-by-line search
    interruptRequested_.set();
    operationWatcher_.waitForFinished();

#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
    operationFuture_ = QtConcurrent::run( this, &QuickFind::doSearchForward, selection, matcher );
#else
    operationFuture_ = QtConcurrent::run(
        qOverload<const Selection&, const QuickFindMatcher&>( &QuickFind::doSearchForward ), this,
        selection, matcher );
#endif
    operationWatcher_.setFuture( operationFuture_ );
}

void QuickFind::searchBackward( Selection selection, QuickFindMatcher matcher )
{
    LOG_DEBUG << "QuickFind::searchBackward";

    incrementalSearchStatus_ = IncrementalSearchStatus();

    // Wait for any ongoing scan to complete so we use fresh results
    scanWatcher_.waitForFinished();

    if ( !allMatches_.empty() ) {
        // Navigate to the previous match before the current selection, with wrap-around
        auto prevPos = selection.getPreviousPosition();
        int idx = findMatchIndexBefore( prevPos.line(), prevPos.column() );
        if ( idx >= 0 ) {
            currentMatchIndex_ = idx;
            navigateToMatch( idx );
        }
        else {
            Q_EMIT searchDone( false, Portion{} );
            sendNotification( QFNotificationReachedBegininningOfFile{} );
        }
        return;
    }

    // Fallback: no matches found, use old line-by-line search
    interruptRequested_.set();
    operationWatcher_.waitForFinished();

#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
    operationFuture_ = QtConcurrent::run( this, &QuickFind::doSearchBackward, selection, matcher );
#else
    operationFuture_ = QtConcurrent::run(
        qOverload<const Selection&, const QuickFindMatcher&>( &QuickFind::doSearchBackward ), this,
        selection, matcher );
#endif
    operationWatcher_.setFuture( operationFuture_ );
}

void QuickFind::scanForPattern( QuickFindMatcher matcher )
{
    LOG_DEBUG << "QuickFind::scanForPattern";

    incrementalSearchStatus_ = IncrementalSearchStatus();
    scanAllMatches( matcher );
}

Portion QuickFind::doSearchForward( const Selection& selection, const QuickFindMatcher& matcher )
{
    return doSearchForward( selection.getNextPosition(), selection, matcher );
}

// Internal implementation of forward search,
// returns the line where the pattern is found or -1 if not found.
// Parameters are the position the search shall start
Portion QuickFind::doSearchForward( const FilePosition& start_position, const Selection& selection,
                                    const QuickFindMatcher& matcher )
{
    interruptRequested_.clear();

    bool found = false;
    LineColumn found_start_col{};
    LineColumn found_end_col{};

    if ( !matcher.isActive() )
        return {};

    // Optimisation: if we are already after the last match,
    // we don't do any search at all.
    if ( lastMatch_.isLater( start_position ) ) {
        // Send a notification
        sendNotification( QFNotificationReachedEndOfFile() );

        return {};
    }

    auto line = start_position.line();
    LOG_DEBUG << "Start searching at line " << line;
    // We look at the rest of the first line
    if ( matcher.isLineMatching( logData_.getExpandedLineString( line ),
                                 start_position.column() ) ) {
        std::tie( found_start_col, found_end_col ) = matcher.getLastMatch();
        found = true;
    }
    else {
        searchingNotifier_.reset();
        // And then the rest of the file
        const auto nb_lines = logData_.getNbLine();
        ++line;
        while ( line < nb_lines ) {
            if ( matcher.isLineMatching( logData_.getExpandedLineString( line ) ) ) {
                std::tie( found_start_col, found_end_col ) = matcher.getLastMatch();
                found = true;
                break;
            }
            ++line;

            // See if we need to notify of the ongoing search
            searchingNotifier_.ping( line, nb_lines, false );

            if ( interruptRequested_ ) {
                break;
            }
        }
    }

    if ( found ) {
        // Clear any notification
        Q_EMIT clearNotification();

        return Portion{ line, found_start_col, found_end_col };
    }
    else {
        if ( !interruptRequested_ ) {
            // Update the position of the last match
            FilePosition last_match_position = selection.getPreviousPosition();
            lastMatch_.set( last_match_position );

            // Send a notification
            sendNotification( QFNotificationReachedEndOfFile{} );
        }
        else {
            // Send a notification
            sendNotification( QFNotificationInterrupted{} );
        }

        return {};
    }
}

Portion QuickFind::doSearchBackward( const Selection& selection, const QuickFindMatcher& matcher )
{
    return doSearchBackward( selection.getPreviousPosition(), selection, matcher );
}

// Internal implementation of backward search,
// returns the line where the pattern is found or -1 if not found.
// Parameters are the position the search shall start
Portion QuickFind::doSearchBackward( const FilePosition& start_position, const Selection& selection,
                                     const QuickFindMatcher& matcher )
{
    interruptRequested_.clear();

    bool found = false;
    LineColumn start_col{};
    LineColumn end_col{};

    if ( !matcher.isActive() )
        return {};

    // Optimisation: if we are already before the first match,
    // we don't do any search at all.
    if ( firstMatch_.isSooner( start_position ) ) {
        // Send a notification
        sendNotification( QFNotificationReachedBegininningOfFile() );

        return {};
    }

    auto line = start_position.line();
    LOG_DEBUG << "Start searching at line " << line;
    // We look at the beginning of the first line
    if ( ( start_position.column() > 0_lcol )
         && ( matcher.isLineMatchingBackward( logData_.getExpandedLineString( line ),
                                              start_position.column() ) ) ) {
        std::tie( start_col, end_col ) = matcher.getLastMatch();
        found = true;
    }
    else {
        searchingNotifier_.reset();
        // And then the rest of the file
        const auto nb_lines = logData_.getNbLine();
        if ( line > 0_lnum ) {
            --line;
            while ( true ) {
                if ( matcher.isLineMatchingBackward( logData_.getExpandedLineString( line ) ) ) {
                    std::tie( start_col, end_col ) = matcher.getLastMatch();
                    found = true;
                    break;
                }
                if ( line == 0_lnum ) {
                    break;
                }

                --line;

                // See if we need to notify of the ongoing search
                searchingNotifier_.ping( line, nb_lines, true );

                if ( interruptRequested_ ) {
                    break;
                }
            }
        }
    }

    if ( found ) {
        // Clear any notification
        Q_EMIT clearNotification();

        return Portion{ line, start_col, end_col };
    }
    else {
        if ( !interruptRequested_ ) {
            // Update the position of the first match
            FilePosition first_match_position = selection.getNextPosition();
            firstMatch_.set( first_match_position );

            // Send a notification
            sendNotification( QFNotificationReachedBegininningOfFile() );
        }
        else {
            // Send a notification
            sendNotification( QFNotificationInterrupted{} );
        }

        return {};
    }
}

void QuickFind::resetLimits()
{
    lastMatch_.reset();
    firstMatch_.reset();
    allMatches_.clear();
    currentMatchIndex_ = -1;
}

void QuickFind::sendNotification( QFNotification notification )
{
    dispatchToMainThread( [ this, notification ]() { notify( notification ); } );
}

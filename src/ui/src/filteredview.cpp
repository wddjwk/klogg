/*
 * Copyright (C) 2009, 2010, 2012, 2017 Nicolas Bonnefon and other contributors
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

#include <cassert>

#include "filteredview.h"
#include "shortcuts.h"

FilteredView::FilteredView( LogFilteredData* newLogData,
                            const QuickFindPattern* const quickFindPattern, QWidget* parent )
    : AbstractLogView( newLogData, quickFindPattern, parent )
    , logFilteredData_( newLogData )
    , collapsedData_( newLogData )
{
    connect( &collapseGrouper_, &CollapseGrouper::groupsComputed, this,
             &FilteredView::applyCollapse, Qt::QueuedConnection );
}

void FilteredView::setVisibility( Visibility visi )
{
    assert( logFilteredData_ );

    logFilteredData_->setVisibility( visi );

    updateData();
}

FilteredView::Visibility FilteredView::visibility() const
{
    assert( logFilteredData_ );

    return logFilteredData_->visibility();
}

void FilteredView::setCollapseEnabled( bool enabled )
{
    collapseEnabled_ = enabled;

    if ( enabled ) {
        recomputeCollapseGroups();
    }
    else {
        setLogData( logFilteredData_ );
        collapsedData_.setGrouper( nullptr );
        collapseGrouper_.clear();
        updateData();
    }
}

bool FilteredView::isCollapseEnabled() const
{
    return collapseEnabled_;
}

void FilteredView::setCollapseRules( const QList<CollapseRule>& rules )
{
    collapseRules_ = rules;

    if ( collapseEnabled_ ) {
        recomputeCollapseGroups();
    }
}

void FilteredView::recomputeCollapseGroups()
{
    if ( !collapseEnabled_ || collapseRules_.isEmpty() ) {
        return;
    }

    collapseGrouper_.computeGroups( logFilteredData_, collapseRules_ );
}

void FilteredView::applyCollapse()
{
    if ( !collapseEnabled_ ) {
        return;
    }

    collapsedData_.setGrouper( &collapseGrouper_ );
    setLogData( &collapsedData_ );
    jumpToLine( 0_lnum );
    updateData();

    Q_EMIT collapseGroupToggled();
}

AbstractLogData::LineType FilteredView::lineType( LineNumber lineNumber ) const
{
    if ( collapseEnabled_ && !collapseGrouper_.isEmpty() ) {
        const auto filteredIdx = collapsedData_.visualToFilteredIndex( lineNumber );
        return logFilteredData_->lineTypeByIndex( filteredIdx );
    }
    return logFilteredData_->lineTypeByIndex( lineNumber );
}

LineNumber FilteredView::displayLineNumber( LineNumber lineNumber ) const
{
    if ( collapseEnabled_ && !collapseGrouper_.isEmpty() ) {
        const auto filteredIdx = collapsedData_.visualToFilteredIndex( lineNumber );
        return logFilteredData_->getMatchingLineNumber( filteredIdx ) + 1_lcount;
    }
    return logFilteredData_->getMatchingLineNumber( lineNumber ) + 1_lcount;
}

LineNumber FilteredView::lineIndex( LineNumber lineNumber ) const
{
    if ( collapseEnabled_ && !collapseGrouper_.isEmpty() ) {
        const auto filteredIdx = logFilteredData_->getLineIndexNumber( lineNumber );
        return collapsedData_.filteredIndexToVisual( filteredIdx );
    }
    return logFilteredData_->getLineIndexNumber( lineNumber );
}

LineNumber FilteredView::maxDisplayLineNumber() const
{
    return LineNumber( logFilteredData_->getNbTotalLines().get() );
}

bool FilteredView::isCollapsedPlaceholderLine( LineNumber visualLine ) const
{
    if ( !collapseEnabled_ ) {
        return false;
    }
    return collapsedData_.isCollapsedGroupLine( visualLine );
}

int64_t FilteredView::collapsedGroupSize( LineNumber visualLine ) const
{
    if ( !collapseEnabled_ ) {
        return 0;
    }
    return collapsedData_.collapsedGroupCount( visualLine );
}

void FilteredView::onPlaceholderClicked( LineNumber visualLine )
{
    if ( !collapseEnabled_ || collapseGrouper_.isEmpty() ) {
        return;
    }

    const auto filteredIdx = collapsedData_.visualToFilteredIndex( visualLine );
    collapseGrouper_.toggleGroupAtFilteredIndex( filteredIdx.get<int64_t>() );

    collapsedData_.setGrouper( &collapseGrouper_ );
    updateData();

    Q_EMIT collapseGroupToggled();
}

void FilteredView::onPlaceholderDoubleClicked( LineNumber /*visualLine*/ )
{
}

void FilteredView::doRegisterShortcuts()
{
    LOG_INFO << "Registering shortcuts for filtered view";
    AbstractLogView::doRegisterShortcuts();
    registerShortcut( ShortcutAction::LogViewNextMark, [ this ] {
        using LineTypeFlags = LogFilteredData::LineTypeFlags;
        auto i = getViewPosition() - 1_lcount;
        bool foundMark = false;
        for ( ; i != 0_lnum; --i ) {
            if ( lineType( i ).testFlag( LineTypeFlags::Mark ) ) {
                foundMark = true;
                break;
            }
        }

        if ( !foundMark ) {
            foundMark = lineType( i ).testFlag( LineTypeFlags::Mark );
        }

        if ( foundMark ) {
            selectAndDisplayLine( i );
        }
    } );
    registerShortcut( ShortcutAction::LogViewPrevMark, [ this ] {
        const auto nbLines = logFilteredData_->getNbLine();
        for ( auto i = getViewPosition() + 1_lcount; i < nbLines; ++i ) {
            if ( lineType( i ).testFlag( LogFilteredData::LineTypeFlags::Mark ) ) {
                selectAndDisplayLine( i );
                break;
            }
        }
    } );
}

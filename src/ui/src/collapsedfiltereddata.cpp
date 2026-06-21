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

#include "collapsedfiltereddata.h"
#include "logfiltereddata.h"
#include "log.h"

#include <algorithm>

CollapsedFilteredData::CollapsedFilteredData( const LogFilteredData* source )
    : source_( source )
{
}

void CollapsedFilteredData::setGrouper( const CollapseGrouper* grouper )
{
    grouper_ = grouper;
    rebuildVisualIndex();
}

void CollapsedFilteredData::rebuildVisualIndex()
{
    visualPrefixSum_.clear();
    totalVisualLines_ = 0;

    if ( !grouper_ || grouper_->isEmpty() ) {
        if ( source_ ) {
            totalVisualLines_ = source_->getNbLine().get<uint64_t>();
        }
        return;
    }

    const auto& groups = grouper_->groups();
    visualPrefixSum_.reserve( groups.size() + 1 );
    visualPrefixSum_.push_back( 0 );

    for ( const auto& group : groups ) {
        int64_t visualCount;
        if ( group.collapsed && group.count >= CollapseGrouper::MIN_COLLAPSE_SIZE ) {
            visualCount = 3;
        }
        else {
            visualCount = group.count;
        }
        totalVisualLines_ += visualCount;
        visualPrefixSum_.push_back( totalVisualLines_ );
    }
}

CollapsedFilteredData::VisualLineInfo
CollapsedFilteredData::resolveVisualLine( int64_t visualLine ) const
{
    VisualLineInfo info{};

    if ( !grouper_ || grouper_->isEmpty() || visualPrefixSum_.empty() ) {
        info.groupIndex = -1;
        info.offsetInGroup = visualLine;
        info.isPlaceholder = false;
        return info;
    }

    auto it = std::upper_bound( visualPrefixSum_.begin(), visualPrefixSum_.end(), visualLine );
    if ( it == visualPrefixSum_.begin() ) {
        info.groupIndex = -1;
        info.offsetInGroup = visualLine;
        info.isPlaceholder = false;
        return info;
    }

    --it;
    const auto groupIdx = static_cast<int64_t>( std::distance( visualPrefixSum_.begin(), it ) );
    const auto offset = visualLine - *it;

    info.groupIndex = groupIdx;

    const auto& group = grouper_->groups()[ static_cast<size_t>( groupIdx ) ];
    const bool isCollapsed = group.collapsed && group.count >= CollapseGrouper::MIN_COLLAPSE_SIZE;

    if ( isCollapsed ) {
        if ( offset == 0 ) {
            info.offsetInGroup = 0;
            info.isPlaceholder = false;
        }
        else if ( offset == 1 ) {
            info.offsetInGroup = 0;
            info.isPlaceholder = true;
        }
        else {
            info.offsetInGroup = group.count - 1;
            info.isPlaceholder = false;
        }
    }
    else {
        info.offsetInGroup = offset;
        info.isPlaceholder = false;
    }

    return info;
}

LineNumber CollapsedFilteredData::visualToFilteredIndex( LineNumber visualLine ) const
{
    if ( !grouper_ || grouper_->isEmpty() ) {
        return visualLine;
    }

    const auto info = resolveVisualLine( static_cast<int64_t>( visualLine.get() ) );

    if ( info.groupIndex < 0 ) {
        return visualLine;
    }

    const auto& group = grouper_->groups()[ static_cast<size_t>( info.groupIndex ) ];

    if ( info.isPlaceholder ) {
        return LineNumber( group.firstFilteredIndex );
    }

    return LineNumber( group.firstFilteredIndex + info.offsetInGroup );
}

LineNumber CollapsedFilteredData::filteredIndexToVisual( LineNumber filteredIndex ) const
{
    if ( !grouper_ || grouper_->isEmpty() ) {
        return filteredIndex;
    }

    const auto& groups = grouper_->groups();
    const auto idx = static_cast<int64_t>( filteredIndex.get() );

    for ( size_t i = 0; i < groups.size(); ++i ) {
        const auto& group = groups[ i ];
        if ( idx >= group.firstFilteredIndex && idx <= group.lastFilteredIndex ) {
            const bool isCollapsed = group.collapsed
                                     && group.count >= CollapseGrouper::MIN_COLLAPSE_SIZE;
            if ( isCollapsed ) {
                if ( idx == group.firstFilteredIndex ) {
                    return LineNumber( visualPrefixSum_[ i ] );
                }
                if ( idx == group.lastFilteredIndex ) {
                    return LineNumber( visualPrefixSum_[ i ] + 2 );
                }
                return LineNumber( visualPrefixSum_[ i ] + 1 );
            }
            return LineNumber( visualPrefixSum_[ i ] + ( idx - group.firstFilteredIndex ) );
        }
    }

    return filteredIndex;
}

bool CollapsedFilteredData::isCollapsedGroupLine( LineNumber visualLine ) const
{
    if ( !grouper_ || grouper_->isEmpty() ) {
        return false;
    }
    const auto info = resolveVisualLine( static_cast<int64_t>( visualLine.get() ) );
    return info.isPlaceholder;
}

int64_t CollapsedFilteredData::collapsedGroupCount( LineNumber visualLine ) const
{
    if ( !grouper_ || grouper_->isEmpty() ) {
        return 0;
    }
    const auto info = resolveVisualLine( static_cast<int64_t>( visualLine.get() ) );
    if ( !info.isPlaceholder || info.groupIndex < 0 ) {
        return 0;
    }
    return grouper_->groups()[ static_cast<size_t>( info.groupIndex ) ].count - 2;
}

QString CollapsedFilteredData::placeholderText( int64_t groupIndex ) const
{
    const auto& group = grouper_->groups()[ static_cast<size_t>( groupIndex ) ];
    return QStringLiteral( "  ... (%1 lines collapsed)" ).arg( group.count - 2 );
}

QString CollapsedFilteredData::doGetLineString( LineNumber line ) const
{
    const auto info = resolveVisualLine( static_cast<int64_t>( line.get() ) );

    if ( info.isPlaceholder ) {
        return placeholderText( info.groupIndex );
    }

    if ( info.groupIndex >= 0 ) {
        const auto& group = grouper_->groups()[ static_cast<size_t>( info.groupIndex ) ];
        const auto filteredIdx = group.firstFilteredIndex + info.offsetInGroup;
        return source_->getLineString( LineNumber( filteredIdx ) );
    }

    return source_->getLineString( line );
}

QString CollapsedFilteredData::doGetExpandedLineString( LineNumber line ) const
{
    const auto info = resolveVisualLine( static_cast<int64_t>( line.get() ) );

    if ( info.isPlaceholder ) {
        return placeholderText( info.groupIndex );
    }

    if ( info.groupIndex >= 0 ) {
        const auto& group = grouper_->groups()[ static_cast<size_t>( info.groupIndex ) ];
        const auto filteredIdx = group.firstFilteredIndex + info.offsetInGroup;
        return source_->getExpandedLineString( LineNumber( filteredIdx ) );
    }

    return source_->getExpandedLineString( line );
}

klogg::vector<QString> CollapsedFilteredData::doGetLines( LineNumber first_line,
                                                          LinesCount number ) const
{
    klogg::vector<QString> lines;
    const auto count = static_cast<int64_t>( number.get() );
    lines.reserve( static_cast<size_t>( count ) );

    for ( int64_t i = 0; i < count; ++i ) {
        lines.push_back( doGetLineString( LineNumber( first_line.get() + i ) ) );
    }

    return lines;
}

klogg::vector<QString> CollapsedFilteredData::doGetExpandedLines( LineNumber first_line,
                                                                  LinesCount number ) const
{
    klogg::vector<QString> lines;
    const auto count = static_cast<int64_t>( number.get() );
    lines.reserve( static_cast<size_t>( count ) );

    for ( int64_t i = 0; i < count; ++i ) {
        lines.push_back( doGetExpandedLineString( LineNumber( first_line.get() + i ) ) );
    }

    return lines;
}

LineNumber CollapsedFilteredData::doGetLineNumber( LineNumber index ) const
{
    return visualToFilteredIndex( index );
}

LinesCount CollapsedFilteredData::doGetNbLine() const
{
    return LinesCount( totalVisualLines_ );
}

LineLength CollapsedFilteredData::doGetMaxLength() const
{
    return source_->getMaxLength();
}

LineLength CollapsedFilteredData::doGetLineLength( LineNumber line ) const
{
    if ( isCollapsedGroupLine( line ) ) {
        const auto info = resolveVisualLine( static_cast<int64_t>( line.get() ) );
        return LineLength( placeholderText( info.groupIndex ).size() );
    }
    const auto filteredIdx = visualToFilteredIndex( line );
    return source_->getLineLength( filteredIdx );
}

void CollapsedFilteredData::doSetDisplayEncoding( const char* encoding )
{
    const_cast<LogFilteredData*>( source_ )->setDisplayEncoding( encoding );
}

QTextCodec* CollapsedFilteredData::doGetDisplayEncoding() const
{
    return source_->getDisplayEncoding();
}

void CollapsedFilteredData::doAttachReader() const
{
    source_->attachReader();
}

void CollapsedFilteredData::doDetachReader() const
{
    source_->detachReader();
}

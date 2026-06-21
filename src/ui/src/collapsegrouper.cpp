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

#include "collapsegrouper.h"
#include "logfiltereddata.h"
#include "log.h"

#include <QStringList>

namespace {
constexpr int BATCH_SIZE = 5000;
}

void CollapseGrouper::computeGroups( const LogFilteredData* data,
                                     const QList<CollapseRule>& rules )
{
    clear();

    if ( !data || rules.isEmpty() ) {
        return;
    }

    QList<CollapseRule> enabledRules;
    QList<QRegularExpression> compiledRegexes;

    for ( const auto& rule : rules ) {
        if ( !rule.enabled ) {
            compiledRegexes.append( QRegularExpression() );
            continue;
        }
        enabledRules.append( rule );
        if ( rule.type == CollapseRule::Type::RegexMatch ) {
            QRegularExpression re( rule.pattern );
            re.optimize();
            compiledRegexes.append( re );
        }
        else {
            compiledRegexes.append( QRegularExpression() );
        }
    }

    if ( enabledRules.isEmpty() ) {
        return;
    }

    const auto totalLines = static_cast<int64_t>( data->getNbLine().get() );

    if ( totalLines == 0 ) {
        return;
    }

    LOG_DEBUG << "CollapseGrouper: computing groups for " << totalLines << " lines with "
              << enabledRules.size() << " rules (OR semantics)";

    // Load all lines for pairwise comparison
    QStringList allLines;
    allLines.reserve( static_cast<int>( totalLines ) );
    for ( int64_t batchStart = 0; batchStart < totalLines; batchStart += BATCH_SIZE ) {
        const int64_t batchSize = std::min( static_cast<int64_t>( BATCH_SIZE ),
                                            totalLines - batchStart );
        const auto batch = data->getLines( LineNumber( batchStart ),
                                           LinesCount( batchSize ) );
        for ( const auto& line : batch ) {
            allLines.append( line );
        }
    }

    // OR semantics: two consecutive lines are "similar" if ANY enabled rule says so
    auto areSimilar = [ & ]( const QString& lineA, const QString& lineB ) -> bool {
        for ( int r = 0; r < rules.size(); ++r ) {
            const auto& rule = rules[ r ];
            if ( !rule.enabled ) continue;

            switch ( rule.type ) {
            case CollapseRule::Type::PrefixMatch: {
                const QString suffixA
                    = ( rule.prefixLength >= 0 && rule.prefixLength < lineA.size() )
                          ? lineA.mid( rule.prefixLength )
                          : lineA;
                const QString suffixB
                    = ( rule.prefixLength >= 0 && rule.prefixLength < lineB.size() )
                          ? lineB.mid( rule.prefixLength )
                          : lineB;
                if ( suffixA == suffixB ) return true;
                break;
            }
            case CollapseRule::Type::ContainsString: {
                const bool matchA = lineA.contains( rule.pattern );
                const bool matchB = lineB.contains( rule.pattern );
                if ( matchA && matchB ) return true;
                break;
            }
            case CollapseRule::Type::RegexMatch: {
                if ( r < compiledRegexes.size() && compiledRegexes[ r ].isValid() ) {
                    const bool matchA = compiledRegexes[ r ].match( lineA ).hasMatch();
                    const bool matchB = compiledRegexes[ r ].match( lineB ).hasMatch();
                    if ( matchA && matchB ) return true;
                }
                break;
            }
            }
        }
        return false;
    };

    int64_t groupStart = 0;
    int64_t groupCount = 1;

    for ( int64_t i = 1; i < totalLines; ++i ) {
        if ( areSimilar( allLines[ static_cast<int>( i - 1 ) ],
                         allLines[ static_cast<int>( i ) ] ) ) {
            ++groupCount;
        }
        else {
            CollapseGroup group;
            group.firstFilteredIndex = groupStart;
            group.lastFilteredIndex = i - 1;
            group.count = groupCount;
            group.collapsed = ( groupCount >= MIN_COLLAPSE_SIZE );
            groups_.push_back( group );

            groupStart = i;
            groupCount = 1;
        }
    }

    if ( groupCount > 0 ) {
        CollapseGroup group;
        group.firstFilteredIndex = groupStart;
        group.lastFilteredIndex = totalLines - 1;
        group.count = groupCount;
        group.collapsed = ( groupCount >= MIN_COLLAPSE_SIZE );
        groups_.push_back( group );
    }

    totalFilteredLines_ = totalLines;
    lastComputedIndex_ = totalLines;

    int collapsedCount = 0;
    for ( const auto& g : groups_ ) {
        if ( g.collapsed ) collapsedCount++;
    }
    LOG_DEBUG << "CollapseGrouper: computed " << groups_.size() << " groups, "
              << collapsedCount << " collapsed";

    Q_EMIT groupsComputed();
}

void CollapseGrouper::clear()
{
    groups_.clear();
    totalFilteredLines_ = 0;
    lastComputedIndex_ = 0;
}

const std::vector<CollapseGroup>& CollapseGrouper::groups() const
{
    return groups_;
}

void CollapseGrouper::toggleGroupAtFilteredIndex( int64_t filteredIndex )
{
    for ( auto& group : groups_ ) {
        if ( filteredIndex >= group.firstFilteredIndex
             && filteredIndex <= group.lastFilteredIndex ) {
            group.collapsed = !group.collapsed;
            return;
        }
    }
}

bool CollapseGrouper::isEmpty() const
{
    return groups_.empty();
}

int64_t CollapseGrouper::totalFilteredLines() const
{
    return totalFilteredLines_;
}

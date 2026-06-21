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

#ifndef COLLAPSEDFILTEREDDATA_H_
#define COLLAPSEDFILTEREDDATA_H_

#include "abstractlogdata.h"
#include "collapsegrouper.h"

#include <vector>

class LogFilteredData;

class CollapsedFilteredData : public AbstractLogData {
    Q_OBJECT

  public:
    explicit CollapsedFilteredData( const LogFilteredData* source );

    void setGrouper( const CollapseGrouper* grouper );

    bool isCollapsedGroupLine( LineNumber visualLine ) const;
    int64_t collapsedGroupCount( LineNumber visualLine ) const;

    LineNumber visualToFilteredIndex( LineNumber visualLine ) const;
    LineNumber filteredIndexToVisual( LineNumber filteredIndex ) const;

  protected:
    QString doGetLineString( LineNumber line ) const override;
    QString doGetExpandedLineString( LineNumber line ) const override;
    klogg::vector<QString> doGetLines( LineNumber first_line, LinesCount number ) const override;
    klogg::vector<QString> doGetExpandedLines( LineNumber first_line,
                                               LinesCount number ) const override;
    LineNumber doGetLineNumber( LineNumber index ) const override;
    LinesCount doGetNbLine() const override;
    LineLength doGetMaxLength() const override;
    LineLength doGetLineLength( LineNumber line ) const override;
    void doSetDisplayEncoding( const char* encoding ) override;
    QTextCodec* doGetDisplayEncoding() const override;
    void doAttachReader() const override;
    void doDetachReader() const override;

  private:
    struct VisualLineInfo {
        int64_t groupIndex;
        int64_t offsetInGroup;
        bool isPlaceholder;
    };

    VisualLineInfo resolveVisualLine( int64_t visualLine ) const;
    void rebuildVisualIndex();
    QString placeholderText( int64_t groupIndex ) const;

    const LogFilteredData* source_;
    const CollapseGrouper* grouper_ = nullptr;

    std::vector<int64_t> visualPrefixSum_;
    int64_t totalVisualLines_ = 0;
};

#endif

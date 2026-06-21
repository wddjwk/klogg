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

#ifndef COLLAPSEGROUPE_H_
#define COLLAPSEGROUPE_H_

#include <cstdint>
#include <vector>

#include <QObject>
#include <QRegularExpression>

#include "collapserules.h"

class LogFilteredData;

struct CollapseGroup {
    int64_t firstFilteredIndex = 0;
    int64_t lastFilteredIndex = 0;
    int64_t count = 0;
    bool collapsed = true;
};

class CollapseGrouper : public QObject {
    Q_OBJECT

  public:
    static constexpr int64_t MIN_COLLAPSE_SIZE = 3;

    CollapseGrouper() = default;

    void computeGroups( const LogFilteredData* data,
                        const QList<CollapseRule>& rules );

    void clear();

    const std::vector<CollapseGroup>& groups() const;

    void toggleGroupAtFilteredIndex( int64_t filteredIndex );

    bool isEmpty() const;

    int64_t totalFilteredLines() const;

  Q_SIGNALS:
    void groupsComputed();

  private:
    std::vector<CollapseGroup> groups_;
    int64_t totalFilteredLines_ = 0;
    int64_t lastComputedIndex_ = 0;
};

#endif

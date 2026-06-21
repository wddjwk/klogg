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

#ifndef COLLAPSERULES_H_
#define COLLAPSERULES_H_

#include <QList>
#include <QString>

#include "persistable.h"

struct CollapseRule {
    enum class Type {
        PrefixMatch = 0,
        RegexMatch = 1,
        ContainsString = 2,
    };

    QString name;
    Type type = Type::PrefixMatch;
    QString pattern;
    int prefixLength = 0;
    bool enabled = true;
};

class CollapseRulesCollection final : public Persistable<CollapseRulesCollection> {
  public:
    using Collection = QList<CollapseRule>;

    static const char* persistableName()
    {
        return "CollapseRulesCollection";
    }

    Collection getSyncedRules();
    Collection getRules() const;
    void setRules( const Collection& rules );

    void retrieveFromStorage( QSettings& settings );
    void saveToStorage( QSettings& settings ) const;
    void saveToStorage( const Collection& rules );

  private:
    static constexpr int CollapseRulesCollection_VERSION = 1;

    Collection rules_;
};

Q_DECLARE_METATYPE( CollapseRule )

#endif

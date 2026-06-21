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

#include "collapserules.h"

#include "log.h"

void CollapseRulesCollection::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "CollapseRulesCollection::retrieveFromStorage";

    if ( settings.contains( "CollapseRulesCollection/version" ) ) {
        settings.beginGroup( "CollapseRulesCollection" );
        if ( settings.value( "version" ).toInt() <= CollapseRulesCollection_VERSION ) {
            rules_.clear();

            int size = settings.beginReadArray( "rules" );

            rules_.reserve( size );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );

                CollapseRule rule;
                rule.name = settings.value( "name" ).toString();
                rule.type = static_cast<CollapseRule::Type>(
                    settings.value( "type", 0 ).toInt() );
                rule.pattern = settings.value( "pattern" ).toString();
                rule.prefixLength = settings.value( "prefixLength", 0 ).toInt();
                rule.enabled = settings.value( "enabled", true ).toBool();

                rules_.push_back( rule );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of CollapseRulesCollection, ignoring it...";
        }
        settings.endGroup();
    }
}

void CollapseRulesCollection::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "CollapseRulesCollection::saveToStorage";

    settings.beginGroup( "CollapseRulesCollection" );
    settings.setValue( "version", CollapseRulesCollection_VERSION );

    settings.remove( "rules" );

    settings.beginWriteArray( "rules" );
    int arrayIndex = 0;
    for ( const auto& rule : rules_ ) {
        settings.setArrayIndex( arrayIndex );
        settings.setValue( "name", rule.name );
        settings.setValue( "type", static_cast<int>( rule.type ) );
        settings.setValue( "pattern", rule.pattern );
        settings.setValue( "prefixLength", rule.prefixLength );
        settings.setValue( "enabled", rule.enabled );

        arrayIndex++;
    }
    settings.endArray();
    settings.endGroup();
}

void CollapseRulesCollection::saveToStorage(
    const CollapseRulesCollection::Collection& rules )
{
    rules_ = rules;
    this->save();
}

CollapseRulesCollection::Collection CollapseRulesCollection::getRules() const
{
    return rules_;
}

CollapseRulesCollection::Collection CollapseRulesCollection::getSyncedRules()
{
    rules_ = this->getSynced().getRules();
    return rules_;
}

void CollapseRulesCollection::setRules( const Collection& rules )
{
    rules_ = rules;
}

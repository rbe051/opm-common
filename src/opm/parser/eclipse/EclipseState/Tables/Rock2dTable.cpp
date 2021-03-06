/*
  Copyright (C) 2019 by Norce

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <vector>
#include <opm/parser/eclipse/Deck/DeckItem.hpp>
#include <opm/parser/eclipse/Deck/DeckKeyword.hpp>
#include <opm/parser/eclipse/Deck/DeckRecord.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/Rock2dTable.hpp>

namespace Opm {

        Rock2dTable::Rock2dTable()
        {
        }

        Rock2dTable::Rock2dTable(const std::vector<std::vector<double>>& pvmultValues,
                                 const std::vector<double>& pressureValues)
            : m_pvmultValues(pvmultValues)
            , m_pressureValues(pressureValues)
        {
        }

        void Rock2dTable::init(const DeckRecord& record, size_t /* tableIdx */)
        {
            m_pressureValues.push_back(record.getItem("PRESSURE").getSIDoubleData()[0]);
            m_pvmultValues.push_back(record.getItem("PVMULT").getSIDoubleData());
        }

        size_t Rock2dTable::size() const
        {
            return m_pressureValues.size();
        }

        size_t Rock2dTable::sizeMultValues() const
        {
            return m_pvmultValues[0].size();
        }

        double Rock2dTable::getPressureValue(size_t index) const
        {
            return m_pressureValues[index];
        }

        double Rock2dTable::getPvmultValue(size_t pressureIndex, size_t saturationIndex) const
        {
            return m_pvmultValues[pressureIndex][saturationIndex];
        }

        bool Rock2dTable::operator==(const Rock2dTable& data) const
        {
            return this->pvmultValues() == data.pvmultValues() &&
                   this->pressureValues() == data.pressureValues();
        }

}


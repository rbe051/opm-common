/*
  Copyright 2014 Statoil ASA.

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

#include <stdexcept>
#include <iostream>
#include <boost/filesystem.hpp>

#define BOOST_TEST_MODULE EclipseGridTests
#include <boost/test/unit_test.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/parser/eclipse/Parser/Parser.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/TableManager.hpp>
#include <opm/parser/eclipse/EclipseState/Eclipse3DProperties.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/FieldPropsManager.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/TransMult.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/TransMult.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/GridProperty.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/GridDims.hpp>

BOOST_AUTO_TEST_CASE(Empty) {
    Opm::Eclipse3DProperties props;
    Opm::EclipseGrid grid(10,10,10);
    Opm::FieldPropsManager fp(Opm::Deck(), grid, Opm::TableManager());
    Opm::TransMult transMult(grid ,{} , fp, props);

    BOOST_CHECK_THROW( transMult.getMultiplier(12,10,10 , Opm::FaceDir::XPlus) , std::invalid_argument );
    BOOST_CHECK_THROW( transMult.getMultiplier(1000 , Opm::FaceDir::XPlus) , std::invalid_argument );

    BOOST_CHECK_EQUAL( transMult.getMultiplier(9,9,9, Opm::FaceDir::YPlus) , 1.0 );
    BOOST_CHECK_EQUAL( transMult.getMultiplier(100 , Opm::FaceDir::ZPlus) , 1.0 );

    BOOST_CHECK_EQUAL( transMult.getMultiplier(9,9,9, Opm::FaceDir::YMinus) , 1.0 );
    BOOST_CHECK_EQUAL( transMult.getMultiplier(100 , Opm::FaceDir::ZMinus) , 1.0 );
}


BOOST_AUTO_TEST_CASE(GridAndEdit) {
    const std::string deck_string = R"(
RUNSPEC
GRIDOPTS
  'YES'  2 /

DIMENS
 5 5 5 /
GRID
MULTZ
  125*2 /
EDIT
MULTZ
  125*2 /
)";

    Opm::Parser parser;
    Opm::Deck deck = parser.parseString(deck_string);
    Opm::TableManager tables(deck);
    Opm::EclipseGrid grid(5,5,5);
    Opm::Eclipse3DProperties props(deck, tables, grid);
    Opm::FieldPropsManager fp(deck, grid, tables);
    Opm::TransMult transMult(grid, deck, fp, props);

    transMult.applyMULT(props.getDoubleGridProperty("MULTZ").getData(), Opm::FaceDir::ZPlus);
    BOOST_CHECK_EQUAL( transMult.getMultiplier(0,0,0 , Opm::FaceDir::ZPlus) , 4.0 );
}

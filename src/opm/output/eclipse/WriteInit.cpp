/*
  Copyright (c) 2019 Equinor ASA
  Copyright (c) 2016 Statoil ASA
  Copyright (c) 2013-2015 Andreas Lauser
  Copyright (c) 2013 SINTEF ICT, Applied Mathematics.
  Copyright (c) 2013 Uni Research AS
  Copyright (c) 2015 IRIS AS

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

#include <opm/output/eclipse/WriteInit.hpp>

#include <opm/common/OpmLog/OpmLog.hpp>

#include <opm/io/eclipse/OutputStream.hpp>

#include <opm/output/data/Solution.hpp>
#include <opm/output/eclipse/LogiHEAD.hpp>
#include <opm/output/eclipse/Tables.hpp>
#include <opm/output/eclipse/WriteRestartHelpers.hpp>

#include <opm/parser/eclipse/EclipseState/Eclipse3DProperties.hpp>
#include <opm/parser/eclipse/EclipseState/EclipseState.hpp>
#include <opm/parser/eclipse/EclipseState/EndpointScaling.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/GridProperties.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/GridProperty.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/FieldPropsManager.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/NNC.hpp>
#include <opm/parser/eclipse/EclipseState/Runspec.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Schedule.hpp>

#include <opm/parser/eclipse/Units/UnitSystem.hpp>

#include <cstddef>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace {

    struct CellProperty
    {
        std::string                name;
        ::Opm::UnitSystem::measure unit;
    };

    using Properties = std::vector<CellProperty>;

    class ScalingVectors
    {
    public:
        ScalingVectors& withHysteresis(const bool active);
        ScalingVectors& collect(const ::Opm::Phases& ph);

        const Properties& getVectors() const
        {
            return this->vectors_;
        }

    private:
        Properties vectors_{};
        bool       useHysteresis_{false};

        void insertScaledWaterEndPoints();
        void insertScaledGasEndPoints();
        void insertScaledOilEndPoints(const ::Opm::Phases& ph);
        void insertScaledRelpermValues(const ::Opm::Phases& ph);
        void insertScaledCapillaryPressure(const ::Opm::Phases& ph);
        void insertImbibitionPoints();
    };

    ScalingVectors& ScalingVectors::withHysteresis(const bool active)
    {
        this->useHysteresis_ = active;

        return *this;
    }

    ScalingVectors& ScalingVectors::collect(const ::Opm::Phases& ph)
    {
        if (ph.active(::Opm::Phase::WATER)) {
            this->insertScaledWaterEndPoints();
        }

        if (ph.active(::Opm::Phase::GAS)) {
            this->insertScaledGasEndPoints();
        }

        if (ph.active(::Opm::Phase::OIL)) {
            this->insertScaledOilEndPoints(ph);
        }

        this->insertScaledRelpermValues(ph);

        if (ph.active(::Opm::Phase::OIL)) {
            this->insertScaledCapillaryPressure(ph);
        }

        if (this->useHysteresis_) {
            // Run uses hysteresis.  Output scaled imbibition curve too.
            this->insertImbibitionPoints();
        }

        return *this;
    }

    void ScalingVectors::insertScaledWaterEndPoints()
    {
        this->vectors_.insert(this->vectors_.end(),
        {
            {"SWL" , ::Opm::UnitSystem::measure::identity },
            {"SWCR", ::Opm::UnitSystem::measure::identity },
            {"SWU" , ::Opm::UnitSystem::measure::identity },
        });
    }

    void ScalingVectors::insertScaledGasEndPoints()
    {
        this->vectors_.insert(this->vectors_.end(),
        {
            {"SGL" , ::Opm::UnitSystem::measure::identity },
            {"SGCR", ::Opm::UnitSystem::measure::identity },
            {"SGU" , ::Opm::UnitSystem::measure::identity },
        });
    }

    void ScalingVectors::insertScaledOilEndPoints(const ::Opm::Phases& ph)
    {
        if (ph.active(::Opm::Phase::WATER)) {
            this->vectors_.push_back(CellProperty {
                "SOWCR", ::Opm::UnitSystem::measure::identity
            });
        }

        if (ph.active(::Opm::Phase::GAS)) {
            this->vectors_.push_back(CellProperty {
                "SOGCR", ::Opm::UnitSystem::measure::identity
            });
        }
    }

    void ScalingVectors::insertScaledRelpermValues(const ::Opm::Phases& ph)
    {
        if (ph.active(::Opm::Phase::WATER)) {
            this->vectors_.insert(this->vectors_.end(),
            {
                {"KRW" , ::Opm::UnitSystem::measure::identity },
                {"KRWR", ::Opm::UnitSystem::measure::identity },
            });
        }

        if (ph.active(::Opm::Phase::GAS)) {
            this->vectors_.insert(this->vectors_.end(),
            {
                {"KRG" , ::Opm::UnitSystem::measure::identity },
                {"KRGR", ::Opm::UnitSystem::measure::identity },
            });
        }

        if (ph.active(::Opm::Phase::OIL)) {
            this->vectors_.push_back(CellProperty {
                "KRO", ::Opm::UnitSystem::measure::identity
            });

            if (ph.active(::Opm::Phase::WATER)) {
                this->vectors_.push_back(CellProperty {
                    "KRORW", ::Opm::UnitSystem::measure::identity
                });
            }

            if (ph.active(::Opm::Phase::GAS)) {
                this->vectors_.push_back(CellProperty {
                    "KRORG", ::Opm::UnitSystem::measure::identity
                });
            }
        }
    }

    void ScalingVectors::insertScaledCapillaryPressure(const ::Opm::Phases& ph)
    {
        if (ph.active(::Opm::Phase::WATER)) {
            this->vectors_.insert(this->vectors_.end(),
            {
                {"SWLPC", ::Opm::UnitSystem::measure::identity },
                {"PCW"  , ::Opm::UnitSystem::measure::pressure },
            });
        }

        if (ph.active(::Opm::Phase::GAS)) {
            this->vectors_.insert(this->vectors_.end(),
            {
                {"SGLPC", ::Opm::UnitSystem::measure::identity },
                {"PCG"  , ::Opm::UnitSystem::measure::pressure },
            });
        }
    }

    void ScalingVectors::insertImbibitionPoints()
    {
        const auto start = static_cast<std::string::size_type>(0);
        const auto count = static_cast<std::string::size_type>(1);
        const auto npt   = this->vectors_.size();

        this->vectors_.reserve(2 * npt);

        for (auto i = 0*npt; i < npt; ++i) {
            auto pt = this->vectors_[i]; // Copy, preserve unit of measure.

            // Imbibition vector.  E.g., SOWCR -> ISOWCR
            pt.name.insert(start, count, 'I');

            this->vectors_.push_back(std::move(pt));
        }
    }

    // =================================================================

    std::vector<float> singlePrecision(const std::vector<double>& x)
    {
        return { x.begin(), x.end() };
    }

    ::Opm::RestartIO::LogiHEAD::PVTModel
    pvtFlags(const ::Opm::Runspec& rspec, const ::Opm::TableManager& tabMgr)
    {
        auto pvt = ::Opm::RestartIO::LogiHEAD::PVTModel{};

        const auto& phases = rspec.phases();

        pvt.isLiveOil = phases.active(::Opm::Phase::OIL) &&
            !tabMgr.getPvtoTables().empty();

        pvt.isWetGas = phases.active(::Opm::Phase::GAS) &&
            !tabMgr.getPvtgTables().empty();

        pvt.constComprOil = phases.active(::Opm::Phase::OIL) &&
            !(pvt.isLiveOil ||
              tabMgr.hasTables("PVDO") ||
              tabMgr.getPvcdoTable().empty());

        return pvt;
    }

    ::Opm::RestartIO::LogiHEAD::SatfuncFlags
    satfuncFlags(const ::Opm::Runspec& rspec)
    {
        auto flags = ::Opm::RestartIO::LogiHEAD::SatfuncFlags{};

        const auto& eps = rspec.endpointScaling();
        if (eps) {
            flags.useEndScale       = true;
            flags.useDirectionalEPS = eps.directional();
            flags.useReversibleEPS  = eps.reversible();
            flags.useAlternateEPS   = eps.threepoint();
        }

        return flags;
    }

    std::vector<bool> logihead(const ::Opm::EclipseState& es)
    {
        const auto& rspec   = es.runspec();
        const auto& wsd     = rspec.wellSegmentDimensions();
        const auto& hystPar = rspec.hysterPar();

        const auto lh = ::Opm::RestartIO::LogiHEAD{}
            .variousParam(false, false, wsd.maxSegmentedWells(), hystPar.active())
            .pvtModel(pvtFlags(rspec, es.getTableManager()))
            .saturationFunction(satfuncFlags(rspec))
            ;

        return lh.data();
    }

    void writeInitFileHeader(const ::Opm::EclipseState&      es,
                             const ::Opm::EclipseGrid&       grid,
                             const ::Opm::Schedule&          sched,
                             Opm::EclIO::OutputStream::Init& initFile)
    {
        {
            const auto ih = ::Opm::RestartIO::Helpers::
                createInteHead(es, grid, sched, 0.0, 0, 0);

            initFile.write("INTEHEAD", ih);
        }

        {
            initFile.write("LOGIHEAD", logihead(es));
        }

        {
            const auto dh = ::Opm::RestartIO::Helpers::
                createDoubHead(es, sched, 0, 0.0, 0.0);

            initFile.write("DOUBHEAD", dh);
        }
    }


#ifdef ENABLE_3DPROPS_TESTING

    void writePoreVolume(const ::Opm::EclipseState&        es,
                         const ::Opm::UnitSystem&          units,
                         ::Opm::EclIO::OutputStream::Init& initFile)
    {
        auto porv = es.fieldProps().porv(true);
        units.from_si(::Opm::UnitSystem::measure::volume, porv);
        initFile.write("PORV", singlePrecision(porv));
    }

    void writeIntegerCellProperties(const ::Opm::EclipseState&        es,
                                    ::Opm::EclIO::OutputStream::Init& initFile)
    {

        // The INIT file should always contain PVT, saturation function,
        // equilibration, and fluid-in-place region vectors.  Call
        // assertKeyword() here--on a 'const' GridProperties object--to
        // invoke the autocreation property, and ensure that the keywords
        // exist in the properties container.
        const auto& fp = es.fieldProps();
        fp.get<int>("PVTNUM");
        fp.get<int>("SATNUM");
        fp.get<int>("EQLNUM");
        fp.get<int>("FIPNUM");

        for (const auto& keyword : fp.keys<int>())
            initFile.write(keyword, fp.get<int>(keyword));

    }

#else

     void writePoreVolume(const ::Opm::EclipseState&        es,
                          const ::Opm::EclipseGrid&         grid,
                          const ::Opm::UnitSystem&          units,
                          ::Opm::EclIO::OutputStream::Init& initFile)
     {
        auto porv = es.get3DProperties()
           .getDoubleGridProperty("PORV").getData();
        for (auto nGlob    = porv.size(),
                  globCell = 0*nGlob; globCell < nGlob; ++globCell)
        {
            if (! grid.cellActive(globCell)) {
                porv[globCell] = 0.0;
            }
        }
        units.from_si(::Opm::UnitSystem::measure::volume, porv);
        initFile.write("PORV", singlePrecision(porv));
     }


    void writeIntegerCellProperties(const ::Opm::EclipseState&        es,
                                    const ::Opm::EclipseGrid&         grid,
                                    ::Opm::EclIO::OutputStream::Init& initFile)
    {

        // The INIT file should always contain PVT, saturation function,
        // equilibration, and fluid-in-place region vectors.  Call
        // assertKeyword() here--on a 'const' GridProperties object--to
        // invoke the autocreation property, and ensure that the keywords
        // exist in the properties container.
        const auto& properties = es.get3DProperties().getIntProperties();
        properties.assertKeyword("PVTNUM");
        properties.assertKeyword("SATNUM");
        properties.assertKeyword("EQLNUM");
        properties.assertKeyword("FIPNUM");

        for (const auto& property : properties) {
            if (property.getKeywordName() == "ACTNUM")
                continue;

            auto ecl_data = property.compressedCopy(grid);
            initFile.write(property.getKeywordName(), ecl_data);
        }
    }

#endif


    void writeGridGeometry(const ::Opm::EclipseGrid&         grid,
                           const ::Opm::UnitSystem&          units,
                           ::Opm::EclIO::OutputStream::Init& initFile)
    {
        const auto length = ::Opm::UnitSystem::measure::length;
        const auto nAct   = grid.getNumActive();

        auto dx    = std::vector<float>{};  dx   .reserve(nAct);
        auto dy    = std::vector<float>{};  dy   .reserve(nAct);
        auto dz    = std::vector<float>{};  dz   .reserve(nAct);
        auto depth = std::vector<float>{};  depth.reserve(nAct);

        for (auto cell = 0*nAct; cell < nAct; ++cell) {
            const auto  globCell = grid.getGlobalIndex(cell);
            const auto& dims     = grid.getCellDims(globCell);

            dx   .push_back(units.from_si(length, dims[0]));
            dy   .push_back(units.from_si(length, dims[1]));
            dz   .push_back(units.from_si(length, dims[2]));
            depth.push_back(units.from_si(length, grid.getCellDepth(globCell)));
        }

        initFile.write("DEPTH", depth);
        initFile.write("DX"   , dx);
        initFile.write("DY"   , dy);
        initFile.write("DZ"   , dz);
    }

    template <typename T, class WriteVector>
    void writeCellPropertiesWithDefaultFlag(const Properties& propList,
                                            const ::Opm::GridProperties<T>&
#ifndef ENABLE_3DPROPS_TESTING
                                            propValues
#endif
                                            , const ::Opm::FieldPropsManager& fp,
                                            const ::Opm::EclipseGrid&
#ifndef ENABLE_3DPROPS_TESTING
                                            grid
#endif
                                            , WriteVector&&                   write)
    {
        for (const auto& prop : propList) {
#ifdef ENABLE_3DPROPS_TESTING
            if (! fp.has<T>(prop.name))
                continue;

            auto data = fp.get<T>(prop.name);
            auto defaulted = fp.defaulted<T>(prop.name);
            write(prop, std::move(defaulted), std::move(data));
#else
            if (! propValues.hasKeyword(prop.name)) {
                continue;
            }

            const auto& opm_property = propValues.getKeyword(prop.name);
            const auto& dflt         = opm_property.wasDefaulted();

            write(prop, grid.compressedVector(dflt),
                  opm_property.compressedCopy(grid));
#endif
        }
    }

    template <typename T, class WriteVector>
    void writeCellPropertiesValuesOnly(const Properties& propList,
                                       const ::Opm::GridProperties<T>&
#ifndef ENABLE_3DPROPS_TESTING
                                       propValues
#endif
                                       , const ::Opm::FieldPropsManager& fp,
                                       const ::Opm::EclipseGrid&
#ifndef ENABLE_3DPROPS_TESTING
                                       grid
#endif
                                       , WriteVector&&                   write)
    {
        for (const auto& prop : propList) {

#ifdef ENABLE_3DPROPS_TESTING
            if (!fp.has<T>(prop.name))
                continue;
            auto data = fp.get<T>(prop.name);
            write(prop, std::move(data));
#else
            if (! propValues.hasKeyword(prop.name)) {
                continue;
            }
            const auto& opm_property = propValues.getKeyword(prop.name);
            write(prop, opm_property.compressedCopy(grid));
#endif
        }
    }

    void writeDoubleCellProperties(const Properties&                    propList,
                                   const ::Opm::GridProperties<double>& propValues,
                                   const ::Opm::FieldPropsManager&      fp,
                                   const ::Opm::EclipseGrid&            grid,
                                   const ::Opm::UnitSystem&             units,
                                   const bool                           needDflt,
                                   ::Opm::EclIO::OutputStream::Init&    initFile)
    {
        if (needDflt) {
            writeCellPropertiesWithDefaultFlag(propList, propValues, fp, grid,
                [&units, &initFile](const CellProperty&   prop,
                                    std::vector<bool>&&   dflt,
                                    std::vector<double>&& value)
            {
                units.from_si(prop.unit, value);

                for (auto n = dflt.size(), i = decltype(n){}; i < n; ++i) {
                    if (dflt[i]) {
                        // Element defaulted.  Output sentinel value
                        // (-1.0e+20) to signify defaulted element.
                        //
                        // Note: Start as float for roundtripping through
                        // function singlePrecision().
                        value[i] = static_cast<double>(-1.0e+20f);
                    }
                }

                initFile.write(prop.name, singlePrecision(value));
            });
        }
        else {
            writeCellPropertiesValuesOnly(propList, propValues, fp, grid,
                [&units, &initFile](const CellProperty&   prop,
                                    std::vector<double>&& value)
            {
                units.from_si(prop.unit, value);
                initFile.write(prop.name, singlePrecision(value));
            });
        }
    }

    void writeDoubleCellProperties(const ::Opm::EclipseState&        es,
                                   const ::Opm::EclipseGrid&         grid,
                                   const ::Opm::UnitSystem&          units,
                                   ::Opm::EclIO::OutputStream::Init& initFile)
    {
        const auto doubleKeywords = Properties {
            {"PORO"  , ::Opm::UnitSystem::measure::identity },
            {"PERMX" , ::Opm::UnitSystem::measure::permeability },
            {"PERMY" , ::Opm::UnitSystem::measure::permeability },
            {"PERMZ" , ::Opm::UnitSystem::measure::permeability },
            {"NTG"   , ::Opm::UnitSystem::measure::identity },
        };

        // The INIT file should always contain the NTG property, we
        // therefore invoke the auto create functionality to ensure
        // that "NTG" is included in the properties container.
        const auto& properties = es.get3DProperties().getDoubleProperties();
        const auto& fp = es.fieldProps();
#ifdef ENABLE_3DPROPS_TESTING
        es.fieldProps().get<double>("NTG");
#else
        properties.assertKeyword("NTG");
#endif
        writeDoubleCellProperties(doubleKeywords, properties, fp,
                                  grid, units, false, initFile);
    }

    void writeSimulatorProperties(const ::Opm::EclipseGrid&         grid,
                                  const ::Opm::data::Solution&      simProps,
                                  ::Opm::EclIO::OutputStream::Init& initFile)
    {
        for (const auto& prop : simProps) {
            const auto& value = grid.compressedVector(prop.second.data);

            initFile.write(prop.first, singlePrecision(value));
        }
    }

    void writeTableData(const ::Opm::EclipseState&        es,
                        const ::Opm::UnitSystem&          units,
                        ::Opm::EclIO::OutputStream::Init& initFile)
    {
        ::Opm::Tables tables(units);

        tables.addPVTTables(es);
        tables.addDensity(es.getTableManager().getDensityTable());
        tables.addSatFunc(es);

        initFile.write("TABDIMS", tables.tabdims());
        initFile.write("TAB"    , tables.tab());
    }

    void writeIntegerMaps(std::map<std::string, std::vector<int>> mapData,
                          ::Opm::EclIO::OutputStream::Init&       initFile)
    {
        for (const auto& pair : mapData) {
            const auto& key = pair.first;

            if (key.size() > std::size_t{8}) {
                throw std::invalid_argument {
                    "Keyword '" + key + "'is too long."
                };
            }

            initFile.write(key, pair.second);
        }
    }

    void writeFilledSatFuncScaling(const Properties&                 propList,
                                   ::Opm::GridProperties<double>&&   propValues,
                                   ::Opm::FieldPropsManager&&        fp,
                                   const ::Opm::EclipseGrid&         grid,
                                   const ::Opm::UnitSystem&          units,
                                   ::Opm::EclIO::OutputStream::Init& initFile)
    {
        for (const auto& prop : propList) {
#ifdef ENABLE_3DPROPS_TESTING
            fp.get<double>(prop.name);
#else
            propValues.assertKeyword(prop.name);
#endif
        }

        // Don't write sentinel value if input defaulted.
        writeDoubleCellProperties(propList, propValues, fp, grid,
                                  units, false, initFile);
    }

    void writeSatFuncScaling(const ::Opm::EclipseState&        es,
                             const ::Opm::EclipseGrid&         grid,
                             const ::Opm::UnitSystem&          units,
                             ::Opm::EclIO::OutputStream::Init& initFile)
    {
        const auto& ph = es.runspec().phases();

        const auto epsVectors = ScalingVectors{}
            .withHysteresis(es.runspec().hysterPar().active())
            .collect       (ph);

        const auto nactph = ph.active(::Opm::Phase::WATER)
            + ph.active(Opm::Phase::OIL)
            + ph.active(Opm::Phase::GAS);

        const auto& props = es.get3DProperties().getDoubleProperties();
        const auto& fp = es.fieldProps();
        if (! es.cfg().init().filleps() || (nactph < 3)) {
            if (nactph < 3) {
                const auto msg = "OPM-Flow does currently not support "
                    "FILLEPS for fewer than 3 active phases. "
                    "Ignoring FILLEPS for "
                    + std::to_string(nactph)
                    + " active phases.";

                Opm::OpmLog::warning(msg);
            }

            // No FILLEPS in input deck or number of active phases
            // unsupported by Flow's saturation function finalizers.
            //
            // Output only those endpoint arrays that exist in the input
            // deck.  Write sentinel value if input defaulted.
            writeDoubleCellProperties(epsVectors.getVectors(), props, fp,
                                      grid, units, true, initFile);
        }
        else {
            // Input deck specified FILLEPS so we should output all endpoint
            // arrays, whether explicitly defined in the input deck or not.
            // However, downstream clients of GridProperties<double> should
            // not see scaling arrays created for output purposes only, so
            // make a copy of the properties object and modify that copy in
            // order to leave the original intact.  Don't write sentinel
            // value if input defaulted.
            auto propsCopy = props;
            auto fp_copy = fp;
            writeFilledSatFuncScaling(epsVectors.getVectors(),
                                      std::move(propsCopy),
                                      std::move(fp_copy),
                                      grid, units, initFile);
        }
    }

    void writeNonNeighbourConnections(const ::Opm::NNC&                 nnc,
                                      const ::Opm::UnitSystem&          units,
                                      ::Opm::EclIO::OutputStream::Init& initFile)
    {
        auto tran = std::vector<double>{};
        tran.reserve(nnc.numNNC());

        for (const auto& nd : nnc.data()) {
            tran.push_back(nd.trans);
        }

        units.from_si(::Opm::UnitSystem::measure::transmissibility, tran);

        initFile.write("TRANNNC", singlePrecision(tran));
    }
} // Anonymous namespace

void Opm::InitIO::write(const ::Opm::EclipseState&              es,
                        const ::Opm::EclipseGrid&               grid,
                        const ::Opm::Schedule&                  schedule,
                        const ::Opm::data::Solution&            simProps,
                        std::map<std::string, std::vector<int>> int_data,
                        const ::Opm::NNC&                       nnc,
                        ::Opm::EclIO::OutputStream::Init&       initFile)
{
    const auto& units = es.getUnits();

    writeInitFileHeader(es, grid, schedule, initFile);

    // The PORV vector is a special case.  This particular vector always
    // holds a total of nx*ny*nz elements, and the elements are explicitly
    // set to zero for inactive cells.  This treatment implies that the
    // active/inactive cell mapping can be inferred by reading the PORV
    // vector from the result set.
#ifdef ENABLE_3DPROPS_TESTING
    writePoreVolume(es, units, initFile);
#else
    writePoreVolume(es, grid, units, initFile);
#endif

    writeGridGeometry(grid, units, initFile);

    writeDoubleCellProperties(es, grid, units, initFile);

    writeSimulatorProperties(grid, simProps, initFile);

    writeTableData(es, units, initFile);

#ifdef ENABLE_3DPROPS_TESTING
    writeIntegerCellProperties(es, initFile);
#else
    writeIntegerCellProperties(es, grid, initFile);
#endif

    writeIntegerMaps(std::move(int_data), initFile);

    writeSatFuncScaling(es, grid, units, initFile);

    if (nnc.numNNC() > std::size_t{0}) {
        writeNonNeighbourConnections(nnc, units, initFile);
    }
}

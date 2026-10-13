// csv.hpp - Tabular transient-result export.

#ifndef MICROSPICE_CSV_HPP
#define MICROSPICE_CSV_HPP

#include <cstddef>
#include <iomanip>
#include <ostream>
#include <string>

#include "microspice/dc_sweep.hpp"
#include "microspice/transient_solver.hpp"

namespace microspice {

inline void writeTransientCsv(std::ostream& output,
                              const TransientResult& result) {
    output << "time_s";
    for (const std::string& node : result.nodeNames) {
        output << ",V(" << node << ')';
    }
    output << '\n' << std::setprecision(12);

    for (std::size_t sampleIndex = 0; sampleIndex < result.samples.size();
         ++sampleIndex) {
        output << result.samples[sampleIndex].timeSeconds;
        for (const std::string& node : result.nodeNames) {
            output << ',' << result.voltage(sampleIndex, node);
        }
        output << '\n';
    }
}

inline void writeDcSweepCsv(std::ostream& output, const DcSweepResult& sweep) {
    output << sweep.sourceName << "_V";
    if (!sweep.points.empty()) {
        const DcResult& first = sweep.points.front().result.operatingPoint;
        for (const std::string& node : first.nodeNames) {
            output << ",V(" << node << ')';
        }
        for (const std::string& source : first.voltageSourceNames) {
            output << ",I(" << source << ')';
        }
    }
    output << ",converged,newton_iterations\n" << std::setprecision(12);

    for (const DcSweepPoint& point : sweep.points) {
        output << point.sourceVoltage;
        const DcResult& operatingPoint = point.result.operatingPoint;
        for (const std::string& node : operatingPoint.nodeNames) {
            output << ',' << operatingPoint.voltage(node);
        }
        for (const std::string& source : operatingPoint.voltageSourceNames) {
            output << ',' << operatingPoint.currentThrough(source);
        }
        output << ',' << (point.result.converged ? 1 : 0) << ','
               << point.result.iterations.size() << '\n';
    }
}

}  // namespace microspice

#endif  // MICROSPICE_CSV_HPP
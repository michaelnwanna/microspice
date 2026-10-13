// csv.hpp - Tabular transient-result export.

#ifndef MICROSPICE_CSV_HPP
#define MICROSPICE_CSV_HPP

#include <cstddef>
#include <iomanip>
#include <ostream>
#include <string>

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

}  // namespace microspice

#endif  // MICROSPICE_CSV_HPP
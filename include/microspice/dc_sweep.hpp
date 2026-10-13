// dc_sweep.hpp - Repeated operating points across a source range.

#ifndef MICROSPICE_DC_SWEEP_HPP
#define MICROSPICE_DC_SWEEP_HPP

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "microspice/circuit.hpp"
#include "microspice/nonlinear_solver.hpp"

namespace microspice {

struct DcSweepOptions {
    std::string sourceName;
    double startVoltage;
    double stopVoltage;
    double stepVoltage;
    NonlinearOptions nonlinear;
};

struct DcSweepPoint {
    double sourceVoltage;
    NonlinearDcResult result;
};

struct DcSweepResult {
    std::string sourceName;
    std::vector<DcSweepPoint> points;
};

inline DcSweepResult solveDcSweep(const Circuit& circuit,
                                  const DcSweepOptions& options) {
    if (!std::isfinite(options.startVoltage) ||
        !std::isfinite(options.stopVoltage) ||
        !std::isfinite(options.stepVoltage) || options.stepVoltage == 0.0) {
        throw std::invalid_argument(
            "DC sweep values must be finite and step nonzero");
    }
    const double span = options.stopVoltage - options.startVoltage;
    if ((span > 0.0 && options.stepVoltage < 0.0) ||
        (span < 0.0 && options.stepVoltage > 0.0)) {
        throw std::invalid_argument(
            "DC sweep step points away from stop value");
    }

    const double intervalCount =
        span == 0.0 ? 0.0 : std::floor(span / options.stepVoltage + 1e-12);
    constexpr double kMaximumPoints = 1'000'000.0;
    if (intervalCount + 1.0 > kMaximumPoints) {
        throw std::invalid_argument("DC sweep requests too many points");
    }

    Circuit sweptCircuit = circuit;
    DcSweepResult sweep{options.sourceName, {}};
    sweep.points.reserve(static_cast<std::size_t>(intervalCount) + 1);
    for (std::size_t index = 0;
         index <= static_cast<std::size_t>(intervalCount); ++index) {
        const double voltage = options.startVoltage +
                               static_cast<double>(index) * options.stepVoltage;
        sweptCircuit.setVoltageSourceVoltage(options.sourceName, voltage);
        sweep.points.push_back(
            {voltage, solveNonlinearDc(sweptCircuit, options.nonlinear)});
    }
    return sweep;
}

}  // namespace microspice

#endif  // MICROSPICE_DC_SWEEP_HPP
// transient_solver.hpp - Backward-Euler capacitor transient analysis.

#ifndef MICROSPICE_TRANSIENT_SOLVER_HPP
#define MICROSPICE_TRANSIENT_SOLVER_HPP

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "microspice/circuit.hpp"
#include "microspice/dc_solver.hpp"
#include "microspice/linear_solver.hpp"

namespace microspice {

struct TransientOptions {
    double timeStepSeconds;
    double stopTimeSeconds;

    /// Optional V(positive)-V(negative) value for each capacitor, in circuit
    /// insertion order. An empty vector means every capacitor starts at 0 V.
    std::vector<double> initialCapacitorVoltages;
};

struct CapacitorStepInfo {
    std::string name;
    double previousVoltage;
    double equivalentConductance;
    double historyCurrent;
    double solvedVoltage;
};

struct TransientSample {
    double timeSeconds;
    DenseMatrix systemMatrix;
    std::vector<double> rightHandSide;
    std::vector<double> solution;
    std::vector<CapacitorStepInfo> capacitors;
};

struct TransientResult {
    std::vector<std::string> nodeNames;
    std::vector<std::string> voltageSourceNames;
    std::vector<TransientSample> samples;

    double voltage(std::size_t sampleIndex, const std::string& node) const {
        if (sampleIndex >= samples.size()) {
            throw std::out_of_range("transient sample index out of range");
        }
        if (node == "0" || node == "gnd" || node == "GND") {
            return 0.0;
        }
        for (std::size_t index = 0; index < nodeNames.size(); ++index) {
            if (nodeNames[index] == node) {
                return samples[sampleIndex].solution[index];
            }
        }
        throw std::out_of_range("unknown node: " + node);
    }
};

namespace detail {

inline double solutionVoltage(const std::vector<std::string>& nodes,
                              const std::vector<double>& solution,
                              const std::string& node) {
    const std::optional<std::size_t> index = nodeIndex(nodes, node);
    return index ? solution[*index] : 0.0;
}

inline void stampConductance(DenseMatrix& matrix,
                             const std::optional<std::size_t>& positive,
                             const std::optional<std::size_t>& negative,
                             double conductance) {
    if (positive) {
        matrix(*positive, *positive) += conductance;
    }
    if (negative) {
        matrix(*negative, *negative) += conductance;
    }
    if (positive && negative) {
        matrix(*positive, *negative) -= conductance;
        matrix(*negative, *positive) -= conductance;
    }
}

}  // namespace detail

inline TransientResult solveTransient(const Circuit& circuit,
                                      const TransientOptions& options) {
    if (!std::isfinite(options.timeStepSeconds) ||
        options.timeStepSeconds <= 0.0) {
        throw std::invalid_argument("transient timestep must be positive");
    }
    if (!std::isfinite(options.stopTimeSeconds) ||
        options.stopTimeSeconds < options.timeStepSeconds) {
        throw std::invalid_argument(
            "transient stop time must be at least one timestep");
    }
    if (!options.initialCapacitorVoltages.empty() &&
        options.initialCapacitorVoltages.size() !=
            circuit.capacitors().size()) {
        throw std::invalid_argument(
            "initial capacitor voltage count does not match circuit");
    }

    const double rawStepCount =
        std::floor(options.stopTimeSeconds / options.timeStepSeconds + 1e-12);
    constexpr double kMaximumSteps = 1'000'000.0;
    if (rawStepCount > kMaximumSteps ||
        rawStepCount >
            static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(
            "transient analysis requests too many steps");
    }
    const std::size_t stepCount = static_cast<std::size_t>(rawStepCount);

    std::vector<double> previousVoltages(circuit.capacitors().size(), 0.0);
    if (!options.initialCapacitorVoltages.empty()) {
        previousVoltages = options.initialCapacitorVoltages;
    }

    TransientResult transient{circuit.nodes(), {}, {}};
    for (const VoltageSource& source : circuit.voltageSources()) {
        transient.voltageSourceNames.push_back(source.name);
    }
    transient.samples.reserve(stepCount);

    for (std::size_t step = 1; step <= stepCount; ++step) {
        DcResult system = assembleDcSystem(circuit);
        std::vector<CapacitorStepInfo> capacitorInfo;
        capacitorInfo.reserve(circuit.capacitors().size());

        for (std::size_t index = 0; index < circuit.capacitors().size();
             ++index) {
            const Capacitor& capacitor = circuit.capacitors()[index];
            const auto positive =
                detail::nodeIndex(circuit.nodes(), capacitor.positiveNode);
            const auto negative =
                detail::nodeIndex(circuit.nodes(), capacitor.negativeNode);
            const double conductance =
                capacitor.capacitanceFarads / options.timeStepSeconds;
            const double historyCurrent = conductance * previousVoltages[index];

            detail::stampConductance(system.systemMatrix, positive, negative,
                                     conductance);
            if (positive) {
                system.rightHandSide[*positive] += historyCurrent;
            }
            if (negative) {
                system.rightHandSide[*negative] -= historyCurrent;
            }
            capacitorInfo.push_back({capacitor.name, previousVoltages[index],
                                     conductance, historyCurrent, 0.0});
        }

        system.solution =
            solveLinearSystem(system.systemMatrix, system.rightHandSide);
        for (std::size_t index = 0; index < circuit.capacitors().size();
             ++index) {
            const Capacitor& capacitor = circuit.capacitors()[index];
            const double solvedVoltage =
                detail::solutionVoltage(circuit.nodes(), system.solution,
                                        capacitor.positiveNode) -
                detail::solutionVoltage(circuit.nodes(), system.solution,
                                        capacitor.negativeNode);
            previousVoltages[index] = solvedVoltage;
            capacitorInfo[index].solvedVoltage = solvedVoltage;
        }

        transient.samples.push_back(
            {static_cast<double>(step) * options.timeStepSeconds,
             std::move(system.systemMatrix), std::move(system.rightHandSide),
             std::move(system.solution), std::move(capacitorInfo)});
    }
    return transient;
}

}  // namespace microspice

#endif  // MICROSPICE_TRANSIENT_SOLVER_HPP
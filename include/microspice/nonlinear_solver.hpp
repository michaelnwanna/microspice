// nonlinear_solver.hpp - Newton-Raphson DC analysis for nonlinear circuits.

#ifndef MICROSPICE_NONLINEAR_SOLVER_HPP
#define MICROSPICE_NONLINEAR_SOLVER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "microspice/dc_solver.hpp"
#include "microspice/diode_model.hpp"

namespace microspice {

struct NonlinearOptions {
    std::size_t maximumIterations = 100;
    double voltageToleranceVolts = 1e-9;
    double currentToleranceAmps = 1e-12;
    double thermalVoltageVolts = kDefaultThermalVoltageVolts;
};

struct DiodeIterationInfo {
    std::string name;
    double linearizationVoltage;
    double solvedVoltage;
    double currentAmps;
    double conductanceSiemens;
    double equivalentCurrentAmps;
};

struct NewtonIteration {
    std::size_t number;
    double maximumVoltageChange;
    double maximumCurrentChange;
    double maximumJunctionMismatch;
    std::vector<DiodeIterationInfo> diodes;
};

struct NonlinearDcResult {
    DcResult operatingPoint;
    bool converged;
    std::vector<NewtonIteration> iterations;
};

namespace detail {

inline double nodeVoltage(const std::vector<std::string>& nodes,
                          const std::vector<double>& solution,
                          const std::string& node) {
    const std::optional<std::size_t> index = nodeIndex(nodes, node);
    return index ? solution[*index] : 0.0;
}

inline double limitJunctionVoltage(double proposed, double previous,
                                   double thermalVoltage,
                                   double saturationCurrent) {
    const double criticalVoltage =
        thermalVoltage *
        std::log(thermalVoltage / (std::sqrt(2.0) * saturationCurrent));
    if (proposed <= criticalVoltage ||
        std::abs(proposed - previous) <= 2.0 * thermalVoltage) {
        return proposed;
    }

    if (previous > 0.0) {
        const double argument = 1.0 + (proposed - previous) / thermalVoltage;
        return argument > 0.0 ? previous + thermalVoltage * std::log(argument)
                              : criticalVoltage;
    }
    return thermalVoltage * std::log(proposed / thermalVoltage);
}

inline void stampDiode(DenseMatrix& matrix, std::vector<double>& rhs,
                       const std::optional<std::size_t>& positive,
                       const std::optional<std::size_t>& negative,
                       const DiodeLinearization& model) {
    if (positive) {
        matrix(*positive, *positive) += model.conductanceSiemens;
        rhs[*positive] -= model.equivalentCurrentAmps;
    }
    if (negative) {
        matrix(*negative, *negative) += model.conductanceSiemens;
        rhs[*negative] += model.equivalentCurrentAmps;
    }
    if (positive && negative) {
        matrix(*positive, *negative) -= model.conductanceSiemens;
        matrix(*negative, *positive) -= model.conductanceSiemens;
    }
}

}  // namespace detail

inline NonlinearDcResult solveNonlinearDc(
    const Circuit& circuit, const NonlinearOptions& options = {}) {
    if (options.maximumIterations == 0) {
        throw std::invalid_argument(
            "maximum Newton iterations must be positive");
    }
    if (!std::isfinite(options.voltageToleranceVolts) ||
        options.voltageToleranceVolts <= 0.0) {
        throw std::invalid_argument(
            "Newton voltage tolerance must be positive");
    }
    if (!std::isfinite(options.currentToleranceAmps) ||
        options.currentToleranceAmps <= 0.0) {
        throw std::invalid_argument(
            "Newton current tolerance must be positive");
    }
    if (!std::isfinite(options.thermalVoltageVolts) ||
        options.thermalVoltageVolts <= 0.0) {
        throw std::invalid_argument("thermal voltage must be positive");
    }
    if (circuit.diodes().empty()) {
        return {solveDc(circuit), true, {}};
    }

    std::vector<double> linearizationVoltages(circuit.diodes().size(), 0.0);
    std::vector<double> previousSolution;
    NonlinearDcResult result{{{}, {}, DenseMatrix(0), {}, {}, {}}, false, {}};
    result.iterations.reserve(options.maximumIterations);

    for (std::size_t iteration = 1; iteration <= options.maximumIterations;
         ++iteration) {
        DcResult system = assembleDcSystem(circuit);
        std::vector<DiodeIterationInfo> diodeInfo;
        diodeInfo.reserve(circuit.diodes().size());

        for (std::size_t index = 0; index < circuit.diodes().size(); ++index) {
            const Diode& diode = circuit.diodes()[index];
            const auto positive =
                detail::nodeIndex(circuit.nodes(), diode.positiveNode);
            const auto negative =
                detail::nodeIndex(circuit.nodes(), diode.negativeNode);
            const DiodeLinearization model =
                linearizeDiode(diode, linearizationVoltages[index],
                               options.thermalVoltageVolts);
            detail::stampDiode(system.systemMatrix, system.rightHandSide,
                               positive, negative, model);
            diodeInfo.push_back({diode.name, linearizationVoltages[index], 0.0,
                                 model.currentAmps, model.conductanceSiemens,
                                 model.equivalentCurrentAmps});
        }

        system.solution =
            solveLinearSystem(system.systemMatrix, system.rightHandSide);
        double maximumJunctionMismatch = 0.0;
        for (std::size_t index = 0; index < circuit.diodes().size(); ++index) {
            const Diode& diode = circuit.diodes()[index];
            const double solvedVoltage =
                detail::nodeVoltage(circuit.nodes(), system.solution,
                                    diode.positiveNode) -
                detail::nodeVoltage(circuit.nodes(), system.solution,
                                    diode.negativeNode);
            diodeInfo[index].solvedVoltage = solvedVoltage;
            maximumJunctionMismatch = std::max(
                maximumJunctionMismatch,
                std::abs(solvedVoltage - linearizationVoltages[index]));
        }

        double maximumVoltageChange = std::numeric_limits<double>::infinity();
        double maximumCurrentChange = std::numeric_limits<double>::infinity();
        if (!previousSolution.empty()) {
            maximumVoltageChange = 0.0;
            for (std::size_t index = 0; index < circuit.nodes().size();
                 ++index) {
                maximumVoltageChange = std::max(
                    maximumVoltageChange,
                    std::abs(system.solution[index] - previousSolution[index]));
            }
            maximumCurrentChange = 0.0;
            for (std::size_t index = circuit.nodes().size();
                 index < system.solution.size(); ++index) {
                maximumCurrentChange = std::max(
                    maximumCurrentChange,
                    std::abs(system.solution[index] - previousSolution[index]));
            }
        }
        result.iterations.push_back(
            {iteration, maximumVoltageChange, maximumCurrentChange,
             maximumJunctionMismatch, std::move(diodeInfo)});
        result.operatingPoint = system;

        if (maximumVoltageChange <= options.voltageToleranceVolts &&
            maximumCurrentChange <= options.currentToleranceAmps &&
            maximumJunctionMismatch <= options.voltageToleranceVolts) {
            result.converged = true;
            return result;
        }

        previousSolution = system.solution;
        for (std::size_t index = 0; index < circuit.diodes().size(); ++index) {
            const Diode& diode = circuit.diodes()[index];
            const double proposed =
                detail::nodeVoltage(circuit.nodes(), system.solution,
                                    diode.positiveNode) -
                detail::nodeVoltage(circuit.nodes(), system.solution,
                                    diode.negativeNode);
            linearizationVoltages[index] = detail::limitJunctionVoltage(
                proposed, linearizationVoltages[index],
                diode.idealityFactor * options.thermalVoltageVolts,
                diode.saturationCurrentAmps);
        }
    }
    return result;
}

}  // namespace microspice

#endif  // MICROSPICE_NONLINEAR_SOLVER_HPP
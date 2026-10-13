// dc_solver.hpp - Linear DC nodal analysis with inspectable matrix stamps.

#ifndef MICROSPICE_DC_SOLVER_HPP
#define MICROSPICE_DC_SOLVER_HPP

#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "microspice/circuit.hpp"
#include "microspice/linear_solver.hpp"

namespace microspice {

struct StampTrace {
    std::string component;
    std::vector<std::string> operations;
};

struct DcResult {
    std::vector<std::string> nodeNames;
    std::vector<std::string> voltageSourceNames;
    DenseMatrix systemMatrix;
    std::vector<double> rightHandSide;
    std::vector<double> solution;
    std::vector<StampTrace> stamps;

    double voltage(const std::string& node) const {
        if (node == "0" || node == "gnd" || node == "GND") {
            return 0.0;
        }
        for (std::size_t index = 0; index < nodeNames.size(); ++index) {
            if (nodeNames[index] == node) {
                return solution[index];
            }
        }
        throw std::out_of_range("unknown node: " + node);
    }

    /// Current is positive from the source's positive node to negative node.
    double currentThrough(const std::string& source) const {
        for (std::size_t index = 0; index < voltageSourceNames.size();
             ++index) {
            if (voltageSourceNames[index] == source) {
                return solution[nodeNames.size() + index];
            }
        }
        throw std::out_of_range("unknown voltage source: " + source);
    }
};

namespace detail {

inline std::optional<std::size_t> nodeIndex(
    const std::vector<std::string>& nodes, const std::string& node) {
    if (node == "0" || node == "gnd" || node == "GND") {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index] == node) {
            return index;
        }
    }
    throw std::logic_error("component references an unregistered node");
}

inline std::string matrixOperation(const char* target, std::size_t row,
                                   std::size_t column, const char* operation,
                                   double value) {
    std::ostringstream stream;
    stream << target << '[' << row << "][" << column << "] " << operation << ' '
           << value;
    return stream.str();
}

inline std::string vectorOperation(const char* target, std::size_t row,
                                   const char* operation, double value) {
    std::ostringstream stream;
    stream << target << '[' << row << "] " << operation << ' ' << value;
    return stream.str();
}

}  // namespace detail

/// Assemble the static-device portion of A*x=z without solving it.
inline DcResult assembleDcSystem(const Circuit& circuit) {
    const std::vector<std::string>& nodes = circuit.nodes();
    if (nodes.empty()) {
        throw std::invalid_argument("circuit has no non-ground nodes");
    }

    std::vector<std::string> voltageSourceNames;
    for (const VoltageSource& source : circuit.voltageSources()) {
        voltageSourceNames.push_back(source.name);
    }
    const std::size_t systemSize =
        nodes.size() + circuit.voltageSources().size();
    DcResult result{nodes,
                    voltageSourceNames,
                    DenseMatrix(systemSize),
                    std::vector<double>(systemSize, 0.0),
                    {},
                    {}};

    for (const Resistor& resistor : circuit.resistors()) {
        const double conductance = 1.0 / resistor.resistanceOhms;
        const auto positive = detail::nodeIndex(nodes, resistor.positiveNode);
        const auto negative = detail::nodeIndex(nodes, resistor.negativeNode);
        StampTrace trace{resistor.name + " (resistor)", {}};

        if (positive) {
            result.systemMatrix(*positive, *positive) += conductance;
            trace.operations.push_back(detail::matrixOperation(
                "G", *positive, *positive, "+=", conductance));
        }
        if (negative) {
            result.systemMatrix(*negative, *negative) += conductance;
            trace.operations.push_back(detail::matrixOperation(
                "G", *negative, *negative, "+=", conductance));
        }
        if (positive && negative) {
            result.systemMatrix(*positive, *negative) -= conductance;
            result.systemMatrix(*negative, *positive) -= conductance;
            trace.operations.push_back(detail::matrixOperation(
                "G", *positive, *negative, "-=", conductance));
            trace.operations.push_back(detail::matrixOperation(
                "G", *negative, *positive, "-=", conductance));
        }
        result.stamps.push_back(std::move(trace));
    }

    for (const CurrentSource& source : circuit.currentSources()) {
        const auto positive = detail::nodeIndex(nodes, source.positiveNode);
        const auto negative = detail::nodeIndex(nodes, source.negativeNode);
        StampTrace trace{source.name + " (current source)", {}};

        // Current leaving the positive node is a negative node injection;
        // current entering the negative node is a positive node injection.
        if (positive) {
            result.rightHandSide[*positive] -= source.currentAmps;
            trace.operations.push_back(detail::vectorOperation(
                "z", *positive, "-=", source.currentAmps));
        }
        if (negative) {
            result.rightHandSide[*negative] += source.currentAmps;
            trace.operations.push_back(detail::vectorOperation(
                "z", *negative, "+=", source.currentAmps));
        }
        result.stamps.push_back(std::move(trace));
    }

    for (std::size_t sourceIndex = 0;
         sourceIndex < circuit.voltageSources().size(); ++sourceIndex) {
        const VoltageSource& source = circuit.voltageSources()[sourceIndex];
        const auto positive = detail::nodeIndex(nodes, source.positiveNode);
        const auto negative = detail::nodeIndex(nodes, source.negativeNode);
        const std::size_t branch = nodes.size() + sourceIndex;
        StampTrace trace{source.name + " (voltage source)", {}};

        if (positive) {
            result.systemMatrix(*positive, branch) += 1.0;
            result.systemMatrix(branch, *positive) += 1.0;
            trace.operations.push_back(
                detail::matrixOperation("A", *positive, branch, "+=", 1.0));
            trace.operations.push_back(
                detail::matrixOperation("A", branch, *positive, "+=", 1.0));
        }
        if (negative) {
            result.systemMatrix(*negative, branch) -= 1.0;
            result.systemMatrix(branch, *negative) -= 1.0;
            trace.operations.push_back(
                detail::matrixOperation("A", *negative, branch, "-=", 1.0));
            trace.operations.push_back(
                detail::matrixOperation("A", branch, *negative, "-=", 1.0));
        }
        result.rightHandSide[branch] += source.voltageVolts;
        trace.operations.push_back(
            detail::vectorOperation("z", branch, "+=", source.voltageVolts));
        result.stamps.push_back(std::move(trace));
    }

    return result;
}

/// Assemble and solve the Modified Nodal Analysis system A*x=z.
inline DcResult solveDc(const Circuit& circuit) {
    if (!circuit.diodes().empty()) {
        throw std::invalid_argument(
            "linear DC analysis cannot solve nonlinear diodes");
    }
    DcResult result = assembleDcSystem(circuit);
    result.solution =
        solveLinearSystem(result.systemMatrix, result.rightHandSide);
    return result;
}

}  // namespace microspice

#endif  // MICROSPICE_DC_SOLVER_HPP
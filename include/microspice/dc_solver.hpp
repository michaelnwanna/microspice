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
    DenseMatrix conductanceMatrix;
    std::vector<double> currentVector;
    std::vector<double> nodeVoltages;
    std::vector<StampTrace> stamps;

    double voltage(const std::string& node) const {
        if (node == "0" || node == "gnd" || node == "GND") {
            return 0.0;
        }
        for (std::size_t index = 0; index < nodeNames.size(); ++index) {
            if (nodeNames[index] == node) {
                return nodeVoltages[index];
            }
        }
        throw std::out_of_range("unknown node: " + node);
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

inline std::string vectorOperation(std::size_t row, const char* operation,
                                   double value) {
    std::ostringstream stream;
    stream << "i[" << row << "] " << operation << ' ' << value;
    return stream.str();
}

}  // namespace detail

/// Assemble and solve G*v=i using resistor and current-source stamps.
inline DcResult solveDc(const Circuit& circuit) {
    const std::vector<std::string>& nodes = circuit.nodes();
    if (nodes.empty()) {
        throw std::invalid_argument("circuit has no non-ground nodes");
    }

    DcResult result{nodes,
                    DenseMatrix(nodes.size()),
                    std::vector<double>(nodes.size(), 0.0),
                    {},
                    {}};

    for (const Resistor& resistor : circuit.resistors()) {
        const double conductance = 1.0 / resistor.resistanceOhms;
        const auto positive = detail::nodeIndex(nodes, resistor.positiveNode);
        const auto negative = detail::nodeIndex(nodes, resistor.negativeNode);
        StampTrace trace{resistor.name + " (resistor)", {}};

        if (positive) {
            result.conductanceMatrix(*positive, *positive) += conductance;
            trace.operations.push_back(detail::matrixOperation(
                "G", *positive, *positive, "+=", conductance));
        }
        if (negative) {
            result.conductanceMatrix(*negative, *negative) += conductance;
            trace.operations.push_back(detail::matrixOperation(
                "G", *negative, *negative, "+=", conductance));
        }
        if (positive && negative) {
            result.conductanceMatrix(*positive, *negative) -= conductance;
            result.conductanceMatrix(*negative, *positive) -= conductance;
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
            result.currentVector[*positive] -= source.currentAmps;
            trace.operations.push_back(
                detail::vectorOperation(*positive, "-=", source.currentAmps));
        }
        if (negative) {
            result.currentVector[*negative] += source.currentAmps;
            trace.operations.push_back(
                detail::vectorOperation(*negative, "+=", source.currentAmps));
        }
        result.stamps.push_back(std::move(trace));
    }

    result.nodeVoltages =
        solveLinearSystem(result.conductanceMatrix, result.currentVector);
    return result;
}

}  // namespace microspice

#endif  // MICROSPICE_DC_SOLVER_HPP
#pragma once

#include "microspice/nonlinear_solver.hpp"
#include "schematic_model.hpp"

namespace microspice::gui {

struct DcAnalysis {
    ExtractedCircuit extracted;
    NonlinearDcResult solved;
};

inline DcAnalysis analyzeDc(ExtractedCircuit extracted,
                            const ExecutionControl& execution = {}) {
    const auto& circuit = extracted.circuit;
    if (circuit.resistors().empty() && circuit.voltageSources().empty() &&
        circuit.currentSources().empty() && circuit.diodes().empty()) {
        throw std::invalid_argument(
            "Add DC components before running analysis");
    }
    auto solved = solveNonlinearDc(circuit, {}, execution);
    return {std::move(extracted), std::move(solved)};
}

}  // namespace microspice::gui
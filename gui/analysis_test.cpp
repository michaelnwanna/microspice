#include "analysis.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main() {
    using namespace microspice;
    using namespace microspice::gui;
    try {
        Circuit circuit;
        circuit.addVoltageSource("V1", "in", "0", 10);
        circuit.addResistor("R1", "in", "out", 1000);
        circuit.addCapacitor("C1", "out", "0", 1e-6);
        bool cancelled = false;
        std::size_t completedSteps = 0;
        const ExecutionControl execution{
            [&] { return completedSteps >= 3; },
            [&](std::size_t completed, std::size_t) {
                completedSteps = completed;
            }};
        try {
            transientWaveforms(circuit, {1e-4, 5e-3, {}}, execution);
        } catch (const AnalysisCancelled&) {
            cancelled = true;
        }
        require(cancelled && completedSteps == 3,
                "transient cancellation must interrupt a running solve");
        completedSteps = 0;
        cancelled = false;
        try {
            sweepWaveforms(circuit, {"V1", 0, 10, 1, {}}, execution);
        } catch (const AnalysisCancelled&) {
            cancelled = true;
        }
        require(cancelled && completedSteps == 3,
                "sweep cancellation must interrupt a running solve");
        cancelled = false;
        try {
            solveDc(circuit, {[] { return true; }, {}});
        } catch (const AnalysisCancelled&) {
            cancelled = true;
        }
        require(cancelled, "DC cancellation must reach the core solver");
        const auto charging = transientWaveforms(circuit, {1e-4, 5e-3, {}});
        require(charging.traces.size() == 3 &&
                    charging.traces[1].points.size() == 50,
                "transient must expose voltage and source current traces");
        require(std::abs(charging.traces[1].points.back().y() -
                         10 * (1 - std::pow(1 / 1.1, 50))) < 1e-9,
                "RC plot must match backward Euler charging");
        require(charging.csv.startsWith("time_s,V(in),V(out)\n"),
                "transient CSV must use the core exporter");
        require(charging.diagnostics.size() == 50 &&
                    charging.diagnostics.back().capacitors.size() == 1,
                "every transient sample must retain its matrix and capacitor "
                "history");
        const auto& frame = charging.diagnostics.back();
        for (std::size_t row = 0; row < frame.solution.size(); ++row) {
            double assembled = 0;
            for (std::size_t column = 0; column < frame.solution.size();
                 ++column) {
                assembled += frame.matrix(row, column) * frame.solution[column];
            }
            require(std::abs(assembled - frame.rhs[row]) < 1e-9,
                    "inspector matrix must satisfy A*x=z");
        }
        const auto initial = transientWaveforms(circuit, {1e-4, 1e-4, {2}});
        require(
            std::abs(initial.traces[1].points[0].y() - (2 + 1) / 1.1) < 1e-9,
            "initial capacitor voltage must reach the core solver");
        circuit.addResistor("R2", "out", "0", 1000);
        const auto sweep = sweepWaveforms(circuit, {"V1", 0, 10, 1, {}});
        require(sweep.traces[1].points.size() == 11 &&
                    std::abs(sweep.traces[1].points.back().y() - 5) < 1e-9,
                "sweep plot must show the divider midpoint");
        require(sweep.csv.startsWith("V1_V,") && sweep.csv.contains("I(V1)"),
                "sweep CSV must contain currents");
        const auto descending = sweepWaveforms(circuit, {"V1", 10, 0, -2, {}});
        require(descending.traces[0].points.size() == 6 &&
                    descending.traces[0].points.back().x() == 0,
                "descending sweeps must preserve source order");
        bool rejected = false;
        try {
            sweepWaveforms(circuit, {"V1", 0, 10, -1, {}});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "wrong sweep direction must be rejected");
        rejected = false;
        try {
            transientWaveforms(circuit, {1e-9, 1, {}});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "oversized interactive analyses must be rejected");
        circuit.addDiode("D1", "out", "0");
        std::size_t cancellationChecks = 0;
        cancelled = false;
        try {
            solveNonlinearDc(circuit, {},
                             {[&] { return ++cancellationChecks >= 8; }, {}});
        } catch (const AnalysisCancelled&) {
            cancelled = true;
        }
        require(cancelled && cancellationChecks == 8,
                "Newton and matrix loops must cooperate with cancellation");
        const auto nonlinear = sweepWaveforms(circuit, {"V1", 0, 2, 0.1, {}});
        require(nonlinear.traces[1].points.back().y() < 1,
                "diode sweep must use the nonlinear solver");
        require(!nonlinear.diagnostics.back().iterations.empty() &&
                    nonlinear.diagnostics.back().converged,
                "sweep diagnostics must retain convergence and diode Newton "
                "iterations");
        rejected = false;
        try {
            transientWaveforms(circuit, {1e-4, 1e-3, {}});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected,
                "unsupported nonlinear transient must report an error");
        std::cout << "Analysis checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
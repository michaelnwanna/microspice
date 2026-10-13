// nonlinear_walkthrough.cpp - Inspect Newton-Raphson diode iterations.

#include <cstdio>

#include "microspice/circuit.hpp"
#include "microspice/nonlinear_solver.hpp"

int main() {
    microspice::Circuit circuit;
    circuit.addVoltageSource("V1", "in", "0", 5.0);
    circuit.addResistor("R1", "in", "out", 1e3);
    circuit.addDiode("D1", "out", "0");

    const microspice::NonlinearDcResult result =
        microspice::solveNonlinearDc(circuit);

    std::printf("microspice Phase 4: Newton-Raphson DC analysis\n\n");
    std::printf("At each guess Vd, replace the diode with its tangent:\n");
    std::printf("  Gd  = dI/dV\n");
    std::printf("  Ieq = I(Vd) - Gd * Vd\n\n");
    std::printf(
        " iter  guess_V  solved_V  diode_mA  Gd_mS  Ieq_mA  "
        "mismatch_V\n");

    for (const microspice::NewtonIteration& iteration : result.iterations) {
        const microspice::DiodeIterationInfo& diode = iteration.diodes[0];
        std::printf(" %4zu  %7.4f  %8.4f  %8.4f  %6.3f  %7.3f  %.3e\n",
                    iteration.number, diode.linearizationVoltage,
                    diode.solvedVoltage, diode.currentAmps * 1e3,
                    diode.conductanceSiemens * 1e3,
                    diode.equivalentCurrentAmps * 1e3,
                    iteration.maximumJunctionMismatch);
    }

    std::printf("\n%s after %zu iterations: V(out) = %.9g V\n",
                result.converged ? "Converged" : "Not converged",
                result.iterations.size(), result.operatingPoint.voltage("out"));
    return result.converged ? 0 : 1;
}
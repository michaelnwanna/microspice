// transient_walkthrough.cpp - Compare backward Euler with an exact RC curve.

#include <cmath>
#include <cstddef>
#include <cstdio>

#include "microspice/circuit.hpp"
#include "microspice/transient_solver.hpp"

int main() {
    constexpr double kSupplyVolts = 10.0;
    constexpr double kResistanceOhms = 1e3;
    constexpr double kCapacitanceFarads = 1e-6;
    constexpr double kTimeStepSeconds = 1e-4;
    constexpr double kStopTimeSeconds = 5e-3;
    constexpr double kTimeConstantSeconds =
        kResistanceOhms * kCapacitanceFarads;

    microspice::Circuit circuit;
    circuit.addVoltageSource("V1", "in", "0", kSupplyVolts);
    circuit.addResistor("R1", "in", "out", kResistanceOhms);
    circuit.addCapacitor("C1", "out", "0", kCapacitanceFarads);

    const auto result = microspice::solveTransient(
        circuit, {kTimeStepSeconds, kStopTimeSeconds, {}});

    std::printf("microspice Phase 3: RC transient analysis\n\n");
    std::printf("Backward Euler replaces C1 at each step with:\n");
    std::printf("  G_eq = C / dt = %.6g S\n",
                kCapacitanceFarads / kTimeStepSeconds);
    std::printf("  I_history = G_eq * V_previous\n\n");
    std::printf(
        " time_ms  previous_V  history_mA  simulated_V  exact_V  "
        "error_mV\n");

    for (std::size_t index = 0; index < result.samples.size(); index += 5) {
        const auto& sample = result.samples[index];
        const auto& capacitor = sample.capacitors[0];
        const double simulated = result.voltage(index, "out");
        const double exact =
            kSupplyVolts *
            (1.0 - std::exp(-sample.timeSeconds / kTimeConstantSeconds));
        std::printf(" %7.3f  %10.6f  %10.6f  %11.6f  %7.4f  %8.3f\n",
                    sample.timeSeconds * 1e3, capacitor.previousVoltage,
                    capacitor.historyCurrent * 1e3, simulated, exact,
                    (simulated - exact) * 1e3);
    }
    return 0;
}
// dc_walkthrough.cpp - Build and inspect a two-node resistor network.

#include <cstddef>
#include <cstdio>

#include "microspice/circuit.hpp"
#include "microspice/dc_solver.hpp"

int main() {
    microspice::Circuit circuit;

    // A 1 mA source injects current into "in". Two 1 kohm resistors form the
    // path in -> out -> ground. Ohm's law predicts V(out)=1 V and V(in)=2 V.
    circuit.addCurrentSource("I1", "0", "in", 1e-3);
    circuit.addResistor("R1", "in", "out", 1e3);
    circuit.addResistor("R2", "out", "0", 1e3);

    const microspice::DcResult result = microspice::solveDc(circuit);

    std::printf("microspice Phase 1: DC nodal analysis\n\n");
    std::printf("Unknown vector:\n");
    for (std::size_t index = 0; index < result.nodeNames.size(); ++index) {
        std::printf("  v[%zu] = V(%s)\n", index,
                    result.nodeNames[index].c_str());
    }

    std::printf("\nComponent stamps:\n");
    for (const auto& stamp : result.stamps) {
        std::printf("  %s\n", stamp.component.c_str());
        for (const auto& operation : stamp.operations) {
            std::printf("    %s\n", operation.c_str());
        }
    }

    std::printf("\nAssembled equation A * x = z:\n");
    for (std::size_t row = 0; row < result.systemMatrix.size(); ++row) {
        std::printf("  [");
        for (std::size_t column = 0; column < result.systemMatrix.size();
             ++column) {
            std::printf("%s% .6f", column == 0 ? "" : "  ",
                        result.systemMatrix(row, column));
        }
        std::printf("] = [% .6f]\n", result.rightHandSide[row]);
    }

    std::printf("\nSolution:\n");
    for (std::size_t index = 0; index < result.nodeNames.size(); ++index) {
        std::printf("  V(%s) = %.6f V\n", result.nodeNames[index].c_str(),
                    result.solution[index]);
    }
    return 0;
}
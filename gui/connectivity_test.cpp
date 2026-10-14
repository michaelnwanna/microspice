#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "microspice/dc_solver.hpp"
#include "schematic_model.hpp"

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main() {
    using namespace microspice::gui;
    try {
        const std::vector<SchematicComponent> divider = {
            {ComponentKind::Voltage, "V1", {-60, 0}, {60, 0}, 10},
            {ComponentKind::Resistor, "R1", {-60, -120}, {60, -120}, 1000},
            {ComponentKind::Resistor, "R2", {180, -120}, {300, -120}, 1000},
            {ComponentKind::Ground, "G1", {60, 120}, {}, 0}};
        const std::vector<QLineF> wires = {{{-60, 0}, {-60, -120}},
                                           {{60, -120}, {180, -120}},
                                           {{300, -120}, {300, 120}},
                                           {{300, 120}, {60, 120}},
                                           {{60, 0}, {60, 120}}};
        const auto extracted = extractCircuit(divider, wires);
        const auto result = microspice::solveDc(extracted.circuit);
        require(std::abs(result.voltage(extracted.terminals.at("R1")[1]) - 5) <
                    1e-9,
                "wired divider must produce 5 V");
        require(extracted.terminals.at("V1")[1] == "0",
                "ground must propagate");
        const std::vector<SchematicComponent> crossing = {
            {ComponentKind::Resistor, "R1", {-100, 0}, {100, 0}, 1000},
            {ComponentKind::Ground, "G1", {0, -100}, {}, 0}};
        const auto separate = extractCircuit(
            crossing, {{{-100, 0}, {100, 0}}, {{0, -100}, {0, 100}}});
        require(separate.terminals.at("R1")[0] != "0",
                "interior crossings without endpoints must not connect");
        const auto junction = extractCircuit(
            crossing, {{{-100, 0}, {100, 0}}, {{0, -100}, {0, 0}}});
        require(junction.terminals.at("R1")[0] == "0",
                "T junction must connect");
        bool rejected = false;
        try {
            extractCircuit({divider[0]}, {});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "missing ground must be rejected");
        std::cout << "Connectivity checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
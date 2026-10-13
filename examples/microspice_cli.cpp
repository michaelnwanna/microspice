// microspice_cli.cpp - Parse and solve a SPICE-like netlist file.

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "microspice/csv.hpp"
#include "microspice/dc_solver.hpp"
#include "microspice/netlist.hpp"
#include "microspice/transient_solver.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: microspice_cli <circuit.cir>\n");
        return 2;
    }

    std::ifstream file(argv[1]);
    if (!file) {
        std::fprintf(stderr, "cannot open netlist: %s\n", argv[1]);
        return 2;
    }
    std::ostringstream contents;
    contents << file.rdbuf();

    try {
        const microspice::ParsedNetlist parsed =
            microspice::parseNetlistDocument(contents.str());
        if (parsed.transient) {
            const microspice::TransientResult result =
                microspice::solveTransient(parsed.circuit,
                                           {parsed.transient->timeStepSeconds,
                                            parsed.transient->stopTimeSeconds,
                                            {}});
            microspice::writeTransientCsv(std::cout, result);
            return 0;
        }

        const microspice::DcResult result = microspice::solveDc(parsed.circuit);

        std::printf("DC operating point\n");
        for (const std::string& node : result.nodeNames) {
            std::printf("  V(%s) = %.9g V\n", node.c_str(),
                        result.voltage(node));
        }
        for (const std::string& source : result.voltageSourceNames) {
            std::printf("  I(%s) = %.9g A  (+ node to - node)\n",
                        source.c_str(), result.currentThrough(source));
        }

        std::printf("\nMNA system size: %zu unknowns\n",
                    result.systemMatrix.size());
        std::printf("Component stamps: %zu\n", result.stamps.size());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "microspice/circuit.hpp"
#include "microspice/dc_solver.hpp"
#include "microspice/linear_solver.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (false)

bool near(double actual, double expected, double tolerance = 1e-9) {
    return std::abs(actual - expected) <= tolerance;
}

void test_solver_pivots_and_solves() {
    microspice::DenseMatrix matrix(3);
    matrix(0, 0) = 0.0;
    matrix(0, 1) = 2.0;
    matrix(0, 2) = 1.0;
    matrix(1, 0) = 1.0;
    matrix(1, 1) = -2.0;
    matrix(1, 2) = -3.0;
    matrix(2, 0) = 2.0;
    matrix(2, 1) = 3.0;
    matrix(2, 2) = 1.0;

    const auto solution =
        microspice::solveLinearSystem(matrix, {7.0, -12.0, 11.0});
    CHECK(near(solution[0], 1.0));
    CHECK(near(solution[1], 2.0));
    CHECK(near(solution[2], 3.0));
}

void test_solver_rejects_singular_matrix() {
    microspice::DenseMatrix matrix(2);
    matrix(0, 0) = 1.0;
    matrix(0, 1) = 2.0;
    matrix(1, 0) = 2.0;
    matrix(1, 1) = 4.0;

    bool threw = false;
    try {
        static_cast<void>(microspice::solveLinearSystem(matrix, {1.0, 2.0}));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

void test_one_node_ohms_law() {
    microspice::Circuit circuit;
    circuit.addCurrentSource("I1", "0", "out", 5e-3);
    circuit.addResistor("R1", "out", "0", 1e3);

    const auto result = microspice::solveDc(circuit);
    CHECK(near(result.voltage("out"), 5.0));
    CHECK(near(result.conductanceMatrix(0, 0), 1e-3));
    CHECK(near(result.currentVector[0], 5e-3));
}

void test_two_node_resistor_network() {
    microspice::Circuit circuit;
    circuit.addCurrentSource("I1", "0", "in", 1e-3);
    circuit.addResistor("R1", "in", "out", 1e3);
    circuit.addResistor("R2", "out", "0", 1e3);

    const auto result = microspice::solveDc(circuit);
    CHECK(near(result.voltage("in"), 2.0));
    CHECK(near(result.voltage("out"), 1.0));
    CHECK(result.stamps.size() == 3);
}

void test_floating_circuit_is_singular() {
    microspice::Circuit circuit;
    circuit.addResistor("R1", "a", "b", 1e3);

    bool threw = false;
    try {
        static_cast<void>(microspice::solveDc(circuit));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    test_solver_pivots_and_solves();
    test_solver_rejects_singular_matrix();
    test_one_node_ohms_law();
    test_two_node_resistor_network();
    test_floating_circuit_is_singular();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
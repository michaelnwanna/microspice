#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "microspice/circuit.hpp"
#include "microspice/dc_solver.hpp"
#include "microspice/linear_solver.hpp"
#include "microspice/netlist.hpp"

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
    CHECK(near(result.systemMatrix(0, 0), 1e-3));
    CHECK(near(result.rightHandSide[0], 5e-3));
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

void test_voltage_source_uses_mna_branch_unknown() {
    microspice::Circuit circuit;
    circuit.addVoltageSource("V1", "in", "0", 10.0);
    circuit.addResistor("R1", "in", "out", 1e3);
    circuit.addResistor("R2", "out", "0", 1e3);

    const auto result = microspice::solveDc(circuit);
    CHECK(result.systemMatrix.size() == 3);
    CHECK(near(result.voltage("in"), 10.0));
    CHECK(near(result.voltage("out"), 5.0));
    CHECK(near(result.currentThrough("V1"), -5e-3));
}

void test_engineering_values() {
    CHECK(near(microspice::parseEngineeringValue("1k"), 1e3));
    CHECK(near(microspice::parseEngineeringValue("4.7u"), 4.7e-6));
    CHECK(near(microspice::parseEngineeringValue("2meg"), 2e6));
    CHECK(near(microspice::parseEngineeringValue("3M"), 3e-3));
}

void test_parse_and_solve_voltage_divider() {
    const auto circuit = microspice::parseNetlist(R"(
* equal-resistor divider
V1 in 0 DC 10
R1 in out 1k
R2 out 0 1k ; inline comment
.end
R3 out 0 1
)");

    const auto result = microspice::solveDc(circuit);
    CHECK(circuit.resistors().size() == 2);
    CHECK(circuit.voltageSources().size() == 1);
    CHECK(near(result.voltage("out"), 5.0));
    CHECK(near(result.currentThrough("V1"), -5e-3));
}

void test_netlist_errors_report_line() {
    bool reportedLine = false;
    try {
        static_cast<void>(microspice::parseNetlist("R1 in 0 bananas\n"));
    } catch (const microspice::NetlistError& error) {
        reportedLine = error.line() == 1;
    }
    CHECK(reportedLine);

    reportedLine = false;
    try {
        static_cast<void>(microspice::parseNetlist("\nR1 in 0 0\n"));
    } catch (const microspice::NetlistError& error) {
        reportedLine = error.line() == 2;
    }
    CHECK(reportedLine);
}

}  // namespace

int main() {
    test_solver_pivots_and_solves();
    test_solver_rejects_singular_matrix();
    test_one_node_ohms_law();
    test_two_node_resistor_network();
    test_floating_circuit_is_singular();
    test_voltage_source_uses_mna_branch_unknown();
    test_engineering_values();
    test_parse_and_solve_voltage_divider();
    test_netlist_errors_report_line();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
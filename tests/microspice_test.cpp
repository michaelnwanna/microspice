#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include "microspice/circuit.hpp"
#include "microspice/csv.hpp"
#include "microspice/dc_solver.hpp"
#include "microspice/dc_sweep.hpp"
#include "microspice/diode_model.hpp"
#include "microspice/linear_solver.hpp"
#include "microspice/netlist.hpp"
#include "microspice/nonlinear_solver.hpp"
#include "microspice/transient_solver.hpp"

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

void test_capacitor_validation() {
    microspice::Circuit circuit;
    circuit.addCapacitor("C1", "out", "0", 1e-6);
    CHECK(circuit.capacitors().size() == 1);
    CHECK(near(circuit.capacitors()[0].capacitanceFarads, 1e-6));

    bool rejectedZero = false;
    try {
        circuit.addCapacitor("Cbad", "out", "0", 0.0);
    } catch (const std::invalid_argument&) {
        rejectedZero = true;
    }
    CHECK(rejectedZero);
}

void test_shockley_diode_model_and_tangent() {
    microspice::Circuit circuit;
    circuit.addDiode("D1", "anode", "0", 1e-12, 1.0);
    const microspice::Diode& diode = circuit.diodes()[0];

    const auto atZero = microspice::linearizeDiode(diode, 0.0);
    CHECK(near(atZero.currentAmps, 0.0));
    CHECK(near(atZero.conductanceSiemens,
               1e-12 / microspice::kDefaultThermalVoltageVolts, 1e-18));
    CHECK(near(atZero.equivalentCurrentAmps, 0.0));

    const auto forward = microspice::linearizeDiode(diode, 0.6);
    const double expectedCurrent =
        1e-12 * (std::exp(0.6 / microspice::kDefaultThermalVoltageVolts) - 1.0);
    CHECK(near(forward.currentAmps, expectedCurrent, 1e-12));
    CHECK(forward.conductanceSiemens > 0.0);
    CHECK(near(forward.equivalentCurrentAmps,
               forward.currentAmps - forward.conductanceSiemens * 0.6, 1e-12));
}

void test_newton_solves_resistor_diode_operating_point() {
    microspice::Circuit circuit;
    circuit.addVoltageSource("V1", "in", "0", 5.0);
    circuit.addResistor("R1", "in", "out", 1e3);
    circuit.addDiode("D1", "out", "0");

    const auto result = microspice::solveNonlinearDc(circuit);
    const double diodeVoltage = result.operatingPoint.voltage("out");
    const double resistorCurrent = (5.0 - diodeVoltage) / 1e3;
    const double diodeCurrent =
        microspice::linearizeDiode(circuit.diodes()[0], diodeVoltage)
            .currentAmps;

    CHECK(result.converged);
    CHECK(result.iterations.size() < 30);
    CHECK(diodeVoltage > 0.5 && diodeVoltage < 0.8);
    CHECK(near(resistorCurrent, diodeCurrent, 1e-9));
    CHECK(result.iterations.back().maximumJunctionMismatch <= 1e-9);
}

void test_newton_reports_iteration_limit() {
    microspice::Circuit circuit;
    circuit.addVoltageSource("V1", "in", "0", 5.0);
    circuit.addResistor("R1", "in", "out", 1e3);
    circuit.addDiode("D1", "out", "0");

    const auto result = microspice::solveNonlinearDc(circuit, {1, 1e-9, 1e-12});
    CHECK(!result.converged);
    CHECK(result.iterations.size() == 1);
}

void test_dc_sweep_traces_diode_curve() {
    microspice::Circuit circuit;
    circuit.addVoltageSource("V1", "out", "0", 0.0);
    circuit.addDiode("D1", "out", "0");

    const auto sweep =
        microspice::solveDcSweep(circuit, {"V1", 0.0, 0.8, 0.2, {}});

    CHECK(sweep.points.size() == 5);
    double previousCurrent = -1.0;
    for (const auto& point : sweep.points) {
        CHECK(point.result.converged);
        const double diodeCurrent =
            -point.result.operatingPoint.currentThrough("V1");
        CHECK(diodeCurrent > previousCurrent);
        previousCurrent = diodeCurrent;
    }
    CHECK(near(sweep.points.back().sourceVoltage, 0.8));
}

void test_clipper_and_rectifier_behaviour() {
    microspice::Circuit clipper;
    clipper.addVoltageSource("V1", "in", "0", 5.0);
    clipper.addResistor("R1", "in", "out", 1e3);
    clipper.addDiode("Dpositive", "out", "0");
    clipper.addDiode("Dnegative", "0", "out");
    const auto positiveClip = microspice::solveNonlinearDc(clipper);
    CHECK(positiveClip.converged);
    CHECK(positiveClip.operatingPoint.voltage("out") > 0.5);
    CHECK(positiveClip.operatingPoint.voltage("out") < 0.8);

    clipper.setVoltageSourceVoltage("V1", -5.0);
    const auto negativeClip = microspice::solveNonlinearDc(clipper);
    CHECK(negativeClip.converged);
    CHECK(negativeClip.operatingPoint.voltage("out") < -0.5);
    CHECK(negativeClip.operatingPoint.voltage("out") > -0.8);

    microspice::Circuit rectifier;
    rectifier.addVoltageSource("V1", "in", "0", -5.0);
    rectifier.addDiode("D1", "in", "out");
    rectifier.addResistor("Rload", "out", "0", 1e3);
    const auto reverse = microspice::solveNonlinearDc(rectifier);
    CHECK(reverse.converged);
    CHECK(std::abs(reverse.operatingPoint.voltage("out")) < 1e-6);

    rectifier.setVoltageSourceVoltage("V1", 5.0);
    const auto forward = microspice::solveNonlinearDc(rectifier);
    CHECK(forward.converged);
    CHECK(forward.operatingPoint.voltage("out") > 4.0);
}

microspice::Circuit makeRcChargingCircuit() {
    microspice::Circuit circuit;
    circuit.addVoltageSource("V1", "in", "0", 10.0);
    circuit.addResistor("R1", "in", "out", 1e3);
    circuit.addCapacitor("C1", "out", "0", 1e-6);
    return circuit;
}

void test_backward_euler_first_rc_step() {
    const auto transient =
        microspice::solveTransient(makeRcChargingCircuit(), {1e-3, 1e-3, {}});

    CHECK(transient.samples.size() == 1);
    CHECK(near(transient.voltage(0, "out"), 5.0));
    CHECK(near(transient.samples[0].capacitors[0].equivalentConductance, 1e-3));
    CHECK(near(transient.samples[0].capacitors[0].historyCurrent, 0.0));
}

void test_initial_capacitor_voltage_drives_discharge() {
    microspice::Circuit circuit;
    circuit.addResistor("R1", "out", "0", 1e3);
    circuit.addCapacitor("C1", "out", "0", 1e-6);

    const auto transient =
        microspice::solveTransient(circuit, {1e-3, 1e-3, {10.0}});

    CHECK(near(transient.voltage(0, "out"), 5.0));
    CHECK(near(transient.samples[0].capacitors[0].previousVoltage, 10.0));
    CHECK(near(transient.samples[0].capacitors[0].historyCurrent, 0.01));
}

void test_rc_charging_is_monotonic_and_approaches_supply() {
    const auto transient =
        microspice::solveTransient(makeRcChargingCircuit(), {1e-4, 5e-3, {}});

    double previous = 0.0;
    for (std::size_t index = 0; index < transient.samples.size(); ++index) {
        const double voltage = transient.voltage(index, "out");
        CHECK(voltage > previous);
        CHECK(voltage < 10.0);
        previous = voltage;
    }
    const double analytical = 10.0 * (1.0 - std::exp(-5.0));
    CHECK(std::abs(previous - analytical) < 0.03);
}

void test_transient_options_are_validated() {
    bool rejected = false;
    try {
        static_cast<void>(microspice::solveTransient(makeRcChargingCircuit(),
                                                     {0.0, 1e-3, {}}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

void test_transient_csv_header_and_rows() {
    const auto transient =
        microspice::solveTransient(makeRcChargingCircuit(), {1e-3, 2e-3, {}});
    std::ostringstream csv;
    microspice::writeTransientCsv(csv, transient);
    const std::string text = csv.str();

    CHECK(text.find("time_s,V(in),V(out)\n") == 0);
    CHECK(static_cast<std::size_t>(
              std::count(text.begin(), text.end(), '\n')) == 3);
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

void test_parse_capacitor_and_transient_directive() {
    const auto parsed = microspice::parseNetlistDocument(R"(
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.tran 100u 5m
.end
)");

    CHECK(parsed.circuit.capacitors().size() == 1);
    CHECK(near(parsed.circuit.capacitors()[0].capacitanceFarads, 1e-6));
    CHECK(parsed.transient.has_value());
    CHECK(near(parsed.transient->timeStepSeconds, 1e-4));
    CHECK(near(parsed.transient->stopTimeSeconds, 5e-3));
}

void test_parse_diode_model_parameters() {
    const auto circuit =
        microspice::parseNetlist("Ddefault a 0\nDcustom b 0 2p 1.5\n");

    CHECK(circuit.diodes().size() == 2);
    CHECK(near(circuit.diodes()[0].saturationCurrentAmps, 1e-12));
    CHECK(near(circuit.diodes()[0].idealityFactor, 1.0));
    CHECK(near(circuit.diodes()[1].saturationCurrentAmps, 2e-12));
    CHECK(near(circuit.diodes()[1].idealityFactor, 1.5));
}

void test_parse_dc_sweep_and_export_csv() {
    const auto parsed = microspice::parseNetlistDocument(
        "V1 out 0 0\nD1 out 0\n.dc V1 0 0.4 0.2\n");
    CHECK(parsed.dcSweep.has_value());
    CHECK(parsed.dcSweep->sourceName == "V1");

    const auto sweep =
        microspice::solveDcSweep(parsed.circuit, {parsed.dcSweep->sourceName,
                                                  parsed.dcSweep->startVoltage,
                                                  parsed.dcSweep->stopVoltage,
                                                  parsed.dcSweep->stepVoltage,
                                                  {}});
    std::ostringstream csv;
    microspice::writeDcSweepCsv(csv, sweep);
    const std::string text = csv.str();
    CHECK(text.find("V1_V,V(out),I(V1),converged,newton_iterations\n") == 0);
    CHECK(static_cast<std::size_t>(
              std::count(text.begin(), text.end(), '\n')) == 4);
}

void test_invalid_dc_sweep_reports_line() {
    bool rejected = false;
    try {
        static_cast<void>(
            microspice::parseNetlistDocument("V1 out 0 0\n.dc V1 0 1 0\n"));
    } catch (const microspice::NetlistError& error) {
        rejected = error.line() == 2;
    }
    CHECK(rejected);

    rejected = false;
    try {
        static_cast<void>(
            microspice::parseNetlistDocument(".dc missing 0 1 0.1\n"));
    } catch (const microspice::NetlistError& error) {
        rejected = error.line() == 1;
    }
    CHECK(rejected);
}

void test_linear_dc_rejects_diode_circuit() {
    microspice::Circuit circuit;
    circuit.addDiode("D1", "out", "0");
    bool rejected = false;
    try {
        static_cast<void>(microspice::solveDc(circuit));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);

    rejected = false;
    try {
        static_cast<void>(
            microspice::solveTransient(circuit, {1e-3, 1e-3, {}}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

void test_invalid_transient_directive_reports_line() {
    bool rejected = false;
    try {
        static_cast<void>(
            microspice::parseNetlistDocument("R1 out 0 1k\n.tran 0 1m\n"));
    } catch (const microspice::NetlistError& error) {
        rejected = error.line() == 2;
    }
    CHECK(rejected);
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
    test_capacitor_validation();
    test_shockley_diode_model_and_tangent();
    test_newton_solves_resistor_diode_operating_point();
    test_newton_reports_iteration_limit();
    test_dc_sweep_traces_diode_curve();
    test_clipper_and_rectifier_behaviour();
    test_backward_euler_first_rc_step();
    test_initial_capacitor_voltage_drives_discharge();
    test_rc_charging_is_monotonic_and_approaches_supply();
    test_transient_options_are_validated();
    test_transient_csv_header_and_rows();
    test_engineering_values();
    test_parse_and_solve_voltage_divider();
    test_parse_capacitor_and_transient_directive();
    test_parse_diode_model_parameters();
    test_parse_dc_sweep_and_export_csv();
    test_invalid_dc_sweep_reports_line();
    test_linear_dc_rejects_diode_circuit();
    test_invalid_transient_directive_reports_line();
    test_netlist_errors_report_line();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
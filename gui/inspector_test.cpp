#include "inspector.hpp"

#include <QApplication>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "analysis.hpp"

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        using namespace microspice;
        using namespace microspice::gui;
        Circuit circuit;
        circuit.addVoltageSource("V1", "in", "0", 10);
        circuit.addResistor("R1", "in", "out", 1000);
        circuit.addResistor("R2", "out", "0", 1000);
        SolverInspector inspector;
        inspector.resize(850, 360);
        inspector.show();
        const auto dc = solveDc(circuit);
        inspector.setFrames({dcDiagnostics(dc)});
        require(inspector.systemTable().rowCount() == 3 &&
                    inspector.systemTable().columnCount() == 5,
                "matrix inspector must include RHS and solution columns");
        require(inspector.systemTable().horizontalHeaderItem(0)->text() ==
                        "V(in)" &&
                    inspector.systemTable().item(1, 4)->text().toDouble() == 5,
                "matrix labels and solution must match the core result");
        require(inspector.stampsText().contains("R1"),
                "component stamps must be displayed");
        circuit.addCapacitor("C1", "out", "0", 1e-6);
        const auto waveform = transientWaveforms(circuit, {1e-4, 5e-4, {}});
        inspector.setFrames(waveform.diagnostics);
        inspector.selectFrame(4);
        require(inspector.frameCount() == 5 &&
                    inspector.capacitorTable().rowCount() == 1 &&
                    inspector.status().contains("0.0005"),
                "sample selector must show the correct capacitor history");
        circuit.addDiode("D1", "out", "0");
        const auto nonlinear = solveNonlinearDc(circuit);
        inspector.setFrames({nonlinearDiagnostics(nonlinear, "Diode DC")});
        require(
            inspector.iterationTable().rowCount() ==
                    static_cast<int>(nonlinear.iterations.size()) &&
                inspector.diodeTable().rowCount() == 1,
            "Newton inspector must expose iterations and diode tangent data");
        inspector.iterationTable().setCurrentCell(
            inspector.iterationTable().rowCount() - 1, 0);
        require(
            std::abs(inspector.diodeTable().item(0, 2)->text().toDouble() -
                     nonlinear.iterations.back().diodes.front().solvedVoltage) <
                1e-9,
            "selecting a Newton row must update its diode details");
        auto* tabs = inspector.findChild<QTabWidget*>();
        tabs->setCurrentIndex(3);
        application.processEvents();
        require(inspector.grab().save("inspector-newton.png"),
                "Newton screenshot must save");
        inspector.resize(420, 300);
        application.processEvents();
        require(inspector.grab().save("inspector-newton-compact.png"),
                "compact Newton screenshot must save");
        inspector.resize(850, 360);
        tabs->setCurrentIndex(0);
        const auto failed = solveNonlinearDc(
            circuit, {1, 1e-9, 1e-12, kDefaultThermalVoltageVolts});
        inspector.setFrames({nonlinearDiagnostics(failed, "Failed DC")});
        require(inspector.status().contains("Not converged"),
                "failed Newton solve must retain diagnostic status");
        const auto sweep = sweepWaveforms(circuit, {"V1", 0, 1, 0.1, {}});
        inspector.setFrames(sweep.diagnostics);
        inspector.selectFrame(10);
        require(inspector.status().contains("V1 = 1 V"),
                "sweep selector must display selected source voltage");
        application.processEvents();
        require(inspector.grab().save("inspector-desktop.png"),
                "inspector desktop screenshot must save");
        inspector.resize(420, 300);
        application.processEvents();
        require(inspector.grab().save("inspector-compact.png"),
                "inspector compact screenshot must save");
        inspector.clear();
        require(inspector.frameCount() == 0 &&
                    inspector.systemTable().rowCount() == 0,
                "clear must remove stale solver diagnostics");
        std::cout << "Inspector checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
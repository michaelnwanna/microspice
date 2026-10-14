#include "analysis_panel.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    using namespace microspice::gui;
    try {
        EditorScene scene;
        scene.loadDivider();
        auto* capacitor = scene.place(ComponentKind::Capacitor, {240, -120});
        capacitor->setValue(1e-6);
        AnalysisPanel panel(scene);
        scene.edited = [&panel] { panel.invalidate(); };
        panel.resize(900, 320);
        panel.show();
        application.processEvents();
        panel.setTransient("100u", "5m");
        const auto waitForAnalysis = [&] {
            QEventLoop loop;
            QTimer deadline;
            deadline.setSingleShot(true);
            QObject::connect(&deadline, &QTimer::timeout, &loop,
                             &QEventLoop::quit);
            QTimer poll;
            QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                if (!panel.busy()) {
                    loop.quit();
                }
            });
            deadline.start(5000);
            poll.start(5);
            loop.exec();
            require(!panel.busy(),
                    "background analysis must finish within test deadline");
        };
        require(panel.startAnalysis() && panel.busy() && !panel.startAnalysis(),
                "background run must start without blocking and reject "
                "overlapping runs");
        panel.resize(600, 280);
        require(panel.grab().save("analysis-running-compact.png"),
                "running controls must capture at compact size");
        panel.resize(900, 320);
        bool uiTick = false;
        QTimer::singleShot(0, &panel, [&] { uiTick = true; });
        waitForAnalysis();
        require(uiTick && panel.data().diagnostics.size() == 50,
                "UI event loop must remain responsive during analysis");
        require(panel.startAnalysis(), "background run must restart");
        panel.cancelAnalysis();
        waitForAnalysis();
        require(
            panel.data().csv.isEmpty() && panel.status().contains("cancelled"),
            "cancelled run must not publish data");
        require(panel.startAnalysis(), "run before invalidation must start");
        panel.setTransient("200u", "5m");
        waitForAnalysis();
        require(panel.data().csv.isEmpty(),
                "settings edits must discard pending results");
        panel.setTransient("100u", "5m");
        panel.setExternalBusy(true);
        require(!panel.startAnalysis(),
                "waveform runs must not overlap a DC run");
        panel.setExternalBusy(false);
        require(panel.startAnalysis(), "run before a circuit edit must start");
        scene.findComponent("R1")->setValue(1100);
        waitForAnalysis();
        require(panel.data().csv.isEmpty(),
                "circuit edits must discard pending results");
        scene.findComponent("R1")->setValue(1000);
        panel.setTransient("1n", "1");
        require(panel.startAnalysis(),
                "validly parsed settings must reach the worker");
        waitForAnalysis();
        require(panel.status().contains("Interactive analysis") &&
                    panel.data().csv.isEmpty(),
                "worker validation failures must reach the panel without "
                "publishing results");
        panel.setTransient("100u", "5m");
        {
            AnalysisPanel closing(scene);
            require(closing.startAnalysis(),
                    "close-time test must have a pending analysis");
        }
        require(panel.runAnalysis() &&
                    panel.data().traces.front().points.size() == 50,
                "transient controls must produce expected samples");
        require(panel.plot().pointCount() == 50,
                "plot must receive trace samples");
        const auto extractedForProbe = scene.extract();
        const auto dcForProbe = microspice::solveDc(extractedForProbe.circuit);
        scene.setProbeReadings(dcForProbe);
        const auto beforeProbe = scene.project();
        const int historyBeforeProbe = scene.history().index();
        QString mouseProbe;
        scene.probed = [&mouseProbe](const QString& key) { mouseProbe = key; };
        scene.setProbeTool();
        QGraphicsSceneMouseEvent probePress(QEvent::GraphicsSceneMousePress);
        probePress.setScenePos({120, -120});
        probePress.setButton(Qt::LeftButton);
        probePress.setButtons(Qt::LeftButton);
        QApplication::sendEvent(&scene, &probePress);
        require(
            panel.selectProbe(mouseProbe) &&
                scene.history().index() == historyBeforeProbe,
            "mouse probe must select a trace without creating an undo command");
        const auto voltageKey = scene.probeAt({120, -120});
        require(
            panel.selectProbe(voltageKey) &&
                scene.probeReading().contains("5 V"),
            "wire probe must select the output trace and show the DC voltage");
        require(scene.probeAt({0, 0}) == "I(V1)" && panel.selectProbe("I(V1)"),
                "source-body probe must select source current");
        require(scene.project() == beforeProbe,
                "probes must not edit schematic data");
        const std::vector<SchematicComponent> crossingComponents = {
            {ComponentKind::Resistor, "R1", {-100, 0}, {-100, 100}, 1000},
            {ComponentKind::Resistor, "R2", {0, -100}, {100, -100}, 1000},
            {ComponentKind::Ground, "G1", {300, 300}, {300, 300}, 0}};
        std::vector<QLineF> crossingWires = {{{-100, 0}, {100, 0}},
                                             {{0, -100}, {0, 100}}};
        const auto crossing = extractCircuit(crossingComponents, crossingWires);
        const auto firstNet =
            "V(" + QString::fromStdString(crossing.terminals.at("R1")[0]) + ")";
        const auto secondNet =
            "V(" + QString::fromStdString(crossing.terminals.at("R2")[0]) + ")";
        require(firstNet != secondNet &&
                    voltageProbe({0, 0}, crossingComponents, crossingWires,
                                 crossing) == firstNet &&
                    voltageProbe({0, 40}, crossingComponents, crossingWires,
                                 crossing) == secondNet,
                "crossing probes must follow only one net, without joining "
                "interior-only crossings");
        require(voltageProbe({300, 300}, crossingComponents, crossingWires,
                             crossing) == "V(0)",
                "ground pin probe must resolve to ground");
        crossingWires.push_back({{0, 0}, {40, 0}});
        const auto junction = extractCircuit(crossingComponents, crossingWires);
        require(
            junction.terminals.at("R1")[0] == junction.terminals.at("R2")[0] &&
                voltageProbe({0, 40}, crossingComponents, crossingWires,
                             junction) ==
                    "V(" +
                        QString::fromStdString(junction.terminals.at("R1")[0]) +
                        ")",
            "endpoint junction probes must agree with circuit extraction");
        require(panel.selectProbe("V(0)"),
                "ground must have a selectable zero-volt trace");
        require(panel.selectProbe("I(V1)"),
                "source current must remain selectable with a ground trace");
        require(panel.plot().pointCount() == 50,
                "current trace selection must update the plot");
        panel.selectTrace(0);
        QTemporaryDir directory;
        const auto filename = directory.filePath("transient.csv");
        panel.exportCsv(filename);
        QFile file(filename);
        require(file.open(QIODevice::ReadOnly) &&
                    file.readAll() == panel.data().csv,
                "CSV action must write the core output exactly");
        QImage image = panel.grab().toImage();
        int tracePixels = 0;
        for (int row = 0; row < image.height(); ++row) {
            for (int column = 0; column < image.width(); ++column) {
                const auto color = image.pixelColor(column, row);
                if (color.green() > 70 && color.blue() > 70 &&
                    color.red() < 40) {
                    ++tracePixels;
                }
            }
        }
        require(tracePixels > 100, "waveform must render a visible trace");
        require(image.save("analysis-desktop.png"),
                "desktop screenshot must save");
        panel.resize(600, 280);
        application.processEvents();
        require(panel.grab().save("analysis-compact.png"),
                "compact screenshot must save");
        const auto range = panel.plot().dataRange();
        const QPointF position(160, 100);
        QWheelEvent wheel(position,
                          panel.plot().mapToGlobal(position.toPoint()),
                          QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&panel.plot(), &wheel);
        require(panel.plot().dataRange().width() < range.width(),
                "plot wheel must zoom");
        panel.plot().resetView();
        require(panel.plot().dataRange() == range,
                "plot reset must restore the data range");
        QMouseEvent press(QEvent::MouseButtonPress, position,
                          panel.plot().mapToGlobal(position.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&panel.plot(), &press);
        const QPointF moved = position + QPointF(30, 0);
        QMouseEvent move(QEvent::MouseMove, moved,
                         panel.plot().mapToGlobal(moved.toPoint()),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&panel.plot(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, moved,
                            panel.plot().mapToGlobal(moved.toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&panel.plot(), &release);
        require(panel.plot().dataRange().left() < range.left(),
                "plot drag must pan horizontally");
        panel.plot().resetView();
        auto* table = panel.findChild<QTableWidget*>();
        require(table && table->rowCount() == 1,
                "capacitor initial condition must be editable");
        table->item(0, 1)->setText("2");
        require(panel.data().csv.isEmpty(),
                "initial-condition edits must invalidate the result");
        require(panel.runAnalysis(),
                "initial-condition values must be accepted");
        panel.resetInitialConditions();
        require(
            table->item(0, 1)->text() == "0",
            "opening a different project must reset initial capacitor values");
        panel.setMode(true);
        panel.setSweep("V1", "10", "0", "-1");
        require(panel.runAnalysis() &&
                    panel.data().traces.front().points.size() == 11,
                "sweep controls must run descending sweeps");
        scene.findComponent("R1")->setValue(2200);
        require(scene.probeReading().isEmpty(),
                "schematic edits must clear probe readings");
        require(panel.data().csv.isEmpty() && panel.plot().pointCount() == 0,
                "schematic edits must invalidate plots and export");
        panel.setSweep("V1", "0", "10", "-1");
        require(!panel.runAnalysis() && !panel.status().isEmpty(),
                "bad sweep inputs must display an error");
        panel.setSweep("V1", "2", "2", "1");
        require(panel.runAnalysis() && panel.plot().pointCount() == 1 &&
                    panel.plot().dataRange().width() > 0,
                "single-point sweep must plot with a stable range");
        panel.setMode(false);
        panel.setTransient("1n", "1");
        require(!panel.runAnalysis() &&
                    panel.status().contains("Interactive analysis"),
                "oversized runs must report a size error");
        panel.setTransient("100u", "5m");
        scene.place(ComponentKind::Diode, {240, -120});
        require(
            !panel.runAnalysis() && panel.status().contains("nonlinear diodes"),
            "unsupported diode transient must be reported");
        if (application.arguments().size() > 1) {
            scene.setProject(readProject(application.arguments()[1]));
            panel.resetInitialConditions();
            require(panel.runAnalysis(),
                    "saved RC project must run through the waveform panel");
            const auto extracted = scene.extract();
            const QString traceName =
                "V(" + QString::fromStdString(extracted.terminals.at("C1")[0]) +
                ")";
            bool foundCharging = false;
            for (const auto& trace : panel.data().traces) {
                if (trace.name.startsWith(traceName)) {
                    foundCharging =
                        std::abs(trace.points.back().y() -
                                 10 * (1 - std::pow(1 / 1.1, 50))) < 1e-9;
                }
            }
            require(
                foundCharging,
                "saved RC output must match the backward-Euler charging curve");
        }
        std::cout << "Analysis panel checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
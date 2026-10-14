#include <QFile>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "../gui/wire_routing.hpp"
#include "microspice/script/executor.hpp"
#include "project_backend.hpp"

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void compare(const QByteArray& contents) {
    const auto desktop = microspice::gui::parseProject(contents);
    const auto browser = microspice::web::parseProject(
        nlohmann::json::parse(contents.toStdString()));
    std::vector<microspice::gui::SchematicComponent> components;
    for (std::size_t index = 0; index < desktop.components.size(); ++index) {
        const auto& placed = desktop.components[index];
        const auto pins = microspice::gui::placedTerminals(placed);
        const auto& converted = browser.components[index];
        require(converted.positive.x == pins[0].x() &&
                    converted.positive.y == pins[0].y(),
                "positive terminal geometry must match desktop QTransform");
        if (pins.size() == 2)
            require(converted.negative.x == pins[1].x() &&
                        converted.negative.y == pins[1].y(),
                    "negative terminal geometry must match desktop QTransform");
        components.push_back({placed.kind, placed.reference, pins[0],
                              pins.size() == 2 ? pins[1] : QPointF{},
                              placed.value, placed.ideality});
    }
    const auto extracted =
        microspice::gui::extractCircuit(components, desktop.wires);
    const auto simulation =
        microspice::web::compileProject(browser, nlohmann::json::object());
    require(extracted.circuit.nodes() == simulation.compiled.circuit.nodes(),
            "node insertion order must match desktop");
    for (const auto& entry : extracted.terminals)
        require(
            simulation.terminals.at(entry.first.toStdString()) == entry.second,
            "ordered component terminals must match desktop");
    const auto expected = microspice::solveNonlinearDc(extracted.circuit);
    require(expected.converged, "desktop example must converge");
    const auto result = microspice::script::run(simulation.compiled);
    const auto& actual =
        std::get<microspice::NonlinearDcResult>(result.analyses[0].solver);
    require(
        expected.operatingPoint.nodeNames == actual.operatingPoint.nodeNames,
        "solver node order must match desktop");
    require(expected.operatingPoint.voltageSourceNames ==
                actual.operatingPoint.voltageSourceNames,
            "branch current order must match desktop");
    require(expected.operatingPoint.solution == actual.operatingPoint.solution,
            "DC voltages and branch currents must exactly match desktop");
    const auto normalized = browser.normalized.dump();
    require(microspice::gui::parseProject(
                QByteArray::fromStdString(normalized)) == desktop,
            "normalization must round-trip through desktop without loss");
}

int main(int argc, char* argv[]) {
    try {
        require(argc == 2, "provide examples directory");
        for (const auto* name :
             {"voltage_divider", "resistor_load", "current_driven_resistor",
              "rc_charging", "diode_bias"}) {
            QFile file(QString::fromLocal8Bit(argv[1]) + "/" + name +
                       ".microspice");
            require(file.open(QIODevice::ReadOnly), "open desktop example");
            compare(file.readAll());
        }
        for (const auto kind : {microspice::gui::ComponentKind::Voltage,
                                microspice::gui::ComponentKind::Current}) {
            for (const double rotation : {0., 90., 180., 270.}) {
                const auto name =
                    kind == microspice::gui::ComponentKind::Voltage ? "V1"
                                                                    : "I1";
                microspice::gui::ProjectState state;
                state.components = {
                    {kind,
                     name,
                     {0, 0},
                     rotation,
                     kind == microspice::gui::ComponentKind::Voltage
                         ? -2.1234567890123457
                         : -0.0021234567890123457,
                     1},
                    {microspice::gui::ComponentKind::Resistor,
                     "R1",
                     {0, 0},
                     rotation,
                     1000,
                     1}};
                const auto positive =
                    microspice::gui::placedTerminals(state.components[0])[0];
                state.components.push_back(
                    {microspice::gui::ComponentKind::Ground, "G1", positive, 0,
                     0, 1});
                compare(microspice::gui::serializeProject(state));
            }
        }
        std::cout << "Five desktop examples and eight signed source rotations "
                     "match exactly\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
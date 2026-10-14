#pragma once

#include "schematic_model.hpp"

namespace microspice::gui {

inline QString voltageProbe(const QPointF& point,
                            const std::vector<SchematicComponent>& components,
                            const std::vector<QLineF>& wires,
                            const ExtractedCircuit& extracted) {
    for (const auto& component : components) {
        if (point == component.positive) {
            return "V(" +
                   QString::fromStdString(
                       extracted.terminals.at(component.name)[0]) +
                   ")";
        }
        if (component.kind != ComponentKind::Ground &&
            point == component.negative) {
            return "V(" +
                   QString::fromStdString(
                       extracted.terminals.at(component.name)[1]) +
                   ")";
        }
    }
    std::vector<QPointF> reached;
    std::vector<bool> visited(wires.size(), false);
    for (std::size_t index = 0; index < wires.size(); ++index) {
        if (onSegment(point, wires[index])) {
            visited[index] = true;
            reached = {wires[index].p1(), wires[index].p2()};
            break;
        }
    }
    bool progress = true;
    while (progress) {
        progress = false;
        for (std::size_t index = 0; index < wires.size(); ++index) {
            if (visited[index]) {
                continue;
            }
            bool connected = false;
            for (const auto& position : reached) {
                connected = connected || onSegment(position, wires[index]);
            }
            for (std::size_t previous = 0; previous < wires.size();
                 ++previous) {
                if (visited[previous]) {
                    connected = connected ||
                                onSegment(wires[index].p1(), wires[previous]) ||
                                onSegment(wires[index].p2(), wires[previous]);
                }
            }
            if (connected) {
                visited[index] = true;
                reached.push_back(wires[index].p1());
                reached.push_back(wires[index].p2());
                progress = true;
            }
        }
    }
    for (const auto& component : components) {
        const std::vector<QPointF> pins =
            component.kind == ComponentKind::Ground
                ? std::vector<QPointF>{component.positive}
                : std::vector<QPointF>{component.positive, component.negative};
        for (std::size_t pin = 0; pin < pins.size(); ++pin) {
            bool connected = std::find(reached.begin(), reached.end(),
                                       pins[pin]) != reached.end();
            for (std::size_t wire = 0; wire < wires.size(); ++wire) {
                connected = connected || (visited[wire] &&
                                          onSegment(pins[pin], wires[wire]));
            }
            if (connected) {
                return "V(" +
                       QString::fromStdString(
                           extracted.terminals.at(component.name)[pin]) +
                       ")";
            }
        }
    }
    return {};
}

}  // namespace microspice::gui
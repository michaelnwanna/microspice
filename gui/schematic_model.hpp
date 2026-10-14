#pragma once

#include <QLineF>
#include <QPointF>
#include <QString>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "microspice/schematic.hpp"

namespace microspice::gui {

using ComponentKind = schematic::ComponentKind;

struct SchematicComponent {
    ComponentKind kind;
    QString name;
    QPointF positive;
    QPointF negative;
    double value;
    double ideality = 1.0;
};

struct ExtractedCircuit {
    Circuit circuit;
    std::map<QString, std::vector<std::string>> terminals;
};

inline bool onSegment(const QPointF& point, const QLineF& segment) {
    return schematic::onSegment(
        {point.x(), point.y()},
        {{segment.x1(), segment.y1()}, {segment.x2(), segment.y2()}});
}

inline ExtractedCircuit extractCircuit(
    const std::vector<SchematicComponent>& components,
    const std::vector<QLineF>& wires) {
    std::vector<schematic::Component> convertedComponents;
    std::vector<schematic::Wire> convertedWires;
    for (const auto& component : components) {
        convertedComponents.push_back(
            {component.kind,
             component.name.toStdString(),
             {component.positive.x(), component.positive.y()},
             {component.negative.x(), component.negative.y()},
             component.value,
             component.ideality});
    }
    for (const auto& wire : wires) {
        convertedWires.push_back(
            {{wire.x1(), wire.y1()}, {wire.x2(), wire.y2()}});
    }
    auto extracted =
        schematic::extractCircuit(convertedComponents, convertedWires);
    ExtractedCircuit result{std::move(extracted.circuit), {}};
    for (auto& entry : extracted.terminals) {
        result.terminals.emplace(QString::fromStdString(entry.first),
                                 std::move(entry.second));
    }
    return result;
}

}  // namespace microspice::gui
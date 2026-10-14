#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "microspice/circuit.hpp"

namespace microspice::schematic {

enum class ComponentKind {
    Resistor,
    Capacitor,
    Diode,
    Voltage,
    Current,
    Ground
};

struct Point {
    double x;
    double y;
};

inline bool operator==(const Point& first, const Point& second) {
    return first.x == second.x && first.y == second.y;
}

struct Wire {
    Point start;
    Point end;
};

struct Component {
    ComponentKind kind;
    std::string name;
    Point positive;
    Point negative;
    double value;
    double ideality = 1.0;
};

struct ExtractedCircuit {
    Circuit circuit;
    std::map<std::string, std::vector<std::string>> terminals;
};

inline bool onSegment(const Point& point, const Wire& segment) {
    const Point delta{segment.end.x - segment.start.x,
                      segment.end.y - segment.start.y};
    const Point offset{point.x - segment.start.x, point.y - segment.start.y};
    return std::abs(delta.x * offset.y - delta.y * offset.x) < 1e-8 &&
           point.x >= std::min(segment.start.x, segment.end.x) - 1e-8 &&
           point.x <= std::max(segment.start.x, segment.end.x) + 1e-8 &&
           point.y >= std::min(segment.start.y, segment.end.y) - 1e-8 &&
           point.y <= std::max(segment.start.y, segment.end.y) + 1e-8;
}

inline ExtractedCircuit extractCircuit(const std::vector<Component>& components,
                                       const std::vector<Wire>& wires) {
    std::vector<Point> points;
    std::vector<std::vector<std::size_t>> pins;
    bool hasGround = false;
    for (const auto& component : components) {
        pins.push_back({points.size()});
        points.push_back(component.positive);
        if (component.kind == ComponentKind::Ground) {
            hasGround = true;
        } else {
            pins.back().push_back(points.size());
            points.push_back(component.negative);
        }
    }
    if (!hasGround) {
        throw std::invalid_argument(
            "Add a ground reference before running analysis");
    }
    for (const auto& wire : wires) {
        if (wire.start.x != wire.end.x && wire.start.y != wire.end.y) {
            throw std::invalid_argument("Wires must be horizontal or vertical");
        }
        points.push_back(wire.start);
        points.push_back(wire.end);
    }
    std::vector<std::size_t> parents(points.size());
    std::iota(parents.begin(), parents.end(), std::size_t{0});
    const auto root = [&parents](std::size_t index) {
        while (parents[index] != index) {
            index = parents[index];
        }
        return index;
    };
    const auto join = [&parents, &root](std::size_t first, std::size_t second) {
        parents[root(second)] = root(first);
    };
    for (std::size_t first = 0; first < points.size(); ++first) {
        for (std::size_t second = first + 1; second < points.size(); ++second) {
            if (points[first] == points[second]) {
                join(first, second);
            }
        }
    }
    for (const auto& wire : wires) {
        std::size_t first = points.size();
        for (std::size_t index = 0; index < points.size(); ++index) {
            if (onSegment(points[index], wire)) {
                if (first == points.size()) {
                    first = index;
                } else {
                    join(first, index);
                }
            }
        }
    }
    std::map<std::size_t, std::string> names;
    for (std::size_t index = 0; index < components.size(); ++index) {
        if (components[index].kind == ComponentKind::Ground) {
            names[root(pins[index][0])] = "0";
        }
    }
    ExtractedCircuit result;
    std::size_t nextNode = 1;
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto& component = components[index];
        if (result.terminals.count(component.name) != 0) {
            throw std::invalid_argument("Duplicate component reference");
        }
        auto& nodes = result.terminals[component.name];
        for (const auto pin : pins[index]) {
            const auto group = root(pin);
            if (names.count(group) == 0) {
                names[group] = "n" + std::to_string(nextNode++);
            }
            nodes.push_back(names[group]);
        }
        switch (component.kind) {
            case ComponentKind::Resistor:
                result.circuit.addResistor(component.name, nodes[0], nodes[1],
                                           component.value);
                break;
            case ComponentKind::Capacitor:
                result.circuit.addCapacitor(component.name, nodes[0], nodes[1],
                                            component.value);
                break;
            case ComponentKind::Diode:
                result.circuit.addDiode(component.name, nodes[0], nodes[1],
                                        component.value, component.ideality);
                break;
            case ComponentKind::Voltage:
                result.circuit.addVoltageSource(component.name, nodes[0],
                                                nodes[1], component.value);
                break;
            case ComponentKind::Current:
                result.circuit.addCurrentSource(component.name, nodes[0],
                                                nodes[1], component.value);
                break;
            case ComponentKind::Ground:
                break;
        }
    }
    return result;
}

}  // namespace microspice::schematic
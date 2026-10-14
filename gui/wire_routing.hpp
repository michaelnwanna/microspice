#pragma once

#include <QTransform>
#include <map>
#include <utility>

#include "project.hpp"

namespace microspice::gui {

inline std::vector<QPointF> placedTerminals(const PlacedComponent& component) {
    QTransform transform;
    transform.translate(component.position.x(), component.position.y());
    transform.rotate(component.rotation);
    const auto terminal = [&transform](const QPointF& point) {
        const auto position = transform.map(point);
        return QPointF(std::round(position.x() / 20) * 20,
                       std::round(position.y() / 20) * 20);
    };
    if (component.kind == ComponentKind::Ground) {
        return {terminal({0, 0})};
    }
    return {terminal({-60, 0}), terminal({60, 0})};
}

inline std::vector<QLineF> reconnectWires(const ProjectState& before,
                                          const ProjectState& after) {
    using PointKey = std::pair<double, double>;
    const auto key = [](const QPointF& point) {
        return PointKey(point.x(), point.y());
    };
    std::map<QString, const PlacedComponent*> current;
    for (const auto& component : after.components) {
        current[component.reference] = &component;
    }
    std::map<PointKey, std::vector<QPointF>> connections;
    bool moved = false;
    for (const auto& component : before.components) {
        const auto found = current.find(component.reference);
        if (found == current.end()) {
            continue;
        }
        const auto oldPins = placedTerminals(component);
        const auto newPins = placedTerminals(*found->second);
        for (std::size_t index = 0; index < oldPins.size(); ++index) {
            connections[key(oldPins[index])].push_back(newPins[index]);
            moved = moved || oldPins[index] != newPins[index];
        }
    }
    if (!moved) {
        return after.wires;
    }
    std::map<PointKey, QPointF> relocated;
    std::vector<QLineF> leads;
    const auto appendRoute = [](std::vector<QLineF>& result,
                                const QPointF& start, const QPointF& end,
                                bool horizontalFirst) {
        const QPointF corner = horizontalFirst ? QPointF(end.x(), start.y())
                                               : QPointF(start.x(), end.y());
        if (start != corner) {
            result.emplace_back(start, corner);
        }
        if (corner != end) {
            result.emplace_back(corner, end);
        }
    };
    for (const auto& entry : connections) {
        const QPointF oldPin(entry.first.first, entry.first.second);
        auto newPins = entry.second;
        std::sort(newPins.begin(), newPins.end(),
                  [&key](const auto& first, const auto& second) {
                      return key(first) < key(second);
                  });
        newPins.erase(std::unique(newPins.begin(), newPins.end()),
                      newPins.end());
        if (newPins.size() == 1 && newPins.front() == oldPin) {
            continue;
        }
        bool onWire = false;
        bool interior = false;
        for (const auto& wire : before.wires) {
            if (onSegment(oldPin, wire)) {
                onWire = true;
                interior =
                    interior || (wire.p1() != oldPin && wire.p2() != oldPin);
            }
        }
        if (!onWire && entry.second.size() < 2) {
            continue;
        }
        if (newPins.size() == 1 && !interior) {
            if (onWire) {
                relocated[entry.first] = newPins.front();
            }
        } else {
            for (const auto& pin : newPins) {
                if (pin != oldPin) {
                    appendRoute(leads, oldPin, pin, true);
                }
            }
        }
    }
    std::vector<QLineF> result;
    for (const auto& wire : before.wires) {
        const auto first = relocated.find(key(wire.p1()));
        const auto second = relocated.find(key(wire.p2()));
        const auto start = first == relocated.end() ? wire.p1() : first->second;
        const auto end = second == relocated.end() ? wire.p2() : second->second;
        appendRoute(result, start, end, wire.y1() == wire.y2());
    }
    result.insert(result.end(), leads.begin(), leads.end());
    return result;
}

}  // namespace microspice::gui
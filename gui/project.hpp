#pragma once

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

#include "schematic_model.hpp"

namespace microspice::gui {

struct PlacedComponent {
    ComponentKind kind;
    QString reference;
    QPointF position;
    double rotation;
    double value;
    double ideality;
};

struct ProjectState {
    std::vector<PlacedComponent> components;
    std::vector<QLineF> wires;
    std::array<int, 6> counters{};
};

inline bool operator==(const PlacedComponent& first,
                       const PlacedComponent& second) {
    return first.kind == second.kind && first.reference == second.reference &&
           first.position == second.position &&
           first.rotation == second.rotation && first.value == second.value &&
           first.ideality == second.ideality;
}

inline bool operator==(const ProjectState& first, const ProjectState& second) {
    return first.components == second.components &&
           first.wires == second.wires && first.counters == second.counters;
}

inline bool operator!=(const ProjectState& first, const ProjectState& second) {
    return !(first == second);
}

inline constexpr std::array<const char*, 6> referencePrefixes = {"R", "C", "D",
                                                                 "V", "I", "G"};

inline void validateProject(const ProjectState& state) {
    if (state.components.size() > 10000 || state.wires.size() > 50000) {
        throw std::invalid_argument(
            "Project exceeds the component or wire limit");
    }
    const auto validPoint = [](const QPointF& point) {
        for (const double coordinate : {point.x(), point.y()}) {
            if (!std::isfinite(coordinate) || std::abs(coordinate) > 1000000 ||
                std::fmod(coordinate, 20.0) != 0) {
                throw std::invalid_argument(
                    "Project coordinates must be on the 20-unit grid within "
                    "+/-1000000");
            }
        }
    };
    for (const auto counter : state.counters) {
        if (counter < 0 || counter > 999999999) {
            throw std::invalid_argument("Invalid component reference counter");
        }
    }
    std::set<QString> references;
    const QRegularExpression referencePattern("^[RCDVIG][1-9][0-9]{0,8}$");
    for (const auto& component : state.components) {
        const auto index = static_cast<std::size_t>(component.kind);
        if (index >= referencePrefixes.size() ||
            !referencePattern.match(component.reference).hasMatch() ||
            !component.reference.startsWith(
                QString::fromLatin1(referencePrefixes[index])) ||
            !references.insert(component.reference).second) {
            throw std::invalid_argument(
                "Invalid or duplicate component reference");
        }
        validPoint(component.position);
        if (!std::isfinite(component.rotation) || component.rotation < 0 ||
            component.rotation >= 360 ||
            std::fmod(component.rotation, 90.0) != 0) {
            throw std::invalid_argument(
                "Component rotation must be 0, 90, 180, or 270 degrees");
        }
        const bool source = component.kind == ComponentKind::Voltage ||
                            component.kind == ComponentKind::Current;
        const bool ground = component.kind == ComponentKind::Ground;
        if (!std::isfinite(component.value) ||
            (!source && !ground && component.value <= 0) ||
            (ground && component.value != 0) ||
            !std::isfinite(component.ideality) || component.ideality <= 0) {
            throw std::invalid_argument(
                "Invalid component value or diode ideality");
        }
    }
    for (const auto& wire : state.wires) {
        validPoint(wire.p1());
        validPoint(wire.p2());
        if (wire.p1() == wire.p2() ||
            (wire.x1() != wire.x2() && wire.y1() != wire.y2())) {
            throw std::invalid_argument(
                "Project wires must be nonzero horizontal or vertical "
                "segments");
        }
    }
}

inline QJsonArray pointJson(const QPointF& point) {
    return {point.x(), point.y()};
}

inline QByteArray serializeProject(const ProjectState& state) {
    validateProject(state);
    QJsonArray components;
    for (const auto& component : state.components) {
        components.append(
            QJsonObject{{"kind", static_cast<int>(component.kind)},
                        {"reference", component.reference},
                        {"position", pointJson(component.position)},
                        {"rotation", component.rotation},
                        {"value", component.value},
                        {"ideality", component.ideality}});
    }
    QJsonArray wires;
    for (const auto& wire : state.wires) {
        wires.append(QJsonObject{{"start", pointJson(wire.p1())},
                                 {"end", pointJson(wire.p2())}});
    }
    QJsonArray counters;
    for (const auto counter : state.counters) {
        counters.append(counter);
    }
    return QJsonDocument(QJsonObject{{"format", "microspice.project"},
                                     {"version", 1},
                                     {"components", components},
                                     {"wires", wires},
                                     {"counters", counters}})
        .toJson();
}

inline ProjectState parseProject(const QByteArray& contents) {
    if (contents.size() > 16 * 1024 * 1024) {
        throw std::invalid_argument("Project exceeds the 16 MiB file limit");
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(contents, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        throw std::invalid_argument("Invalid project JSON: " +
                                    error.errorString().toStdString());
    }
    const auto object = document.object();
    if (object.value("format").toString() != "microspice.project" ||
        !object.value("version").isDouble() ||
        object.value("version").toDouble() != 1) {
        throw std::invalid_argument(
            "Unsupported microspice project format or version");
    }
    const auto number = [](const QJsonValue& value) {
        if (!value.isDouble() || !std::isfinite(value.toDouble())) {
            throw std::invalid_argument(
                "Project numeric fields must contain finite numbers");
        }
        return value.toDouble();
    };
    const auto point = [&number](const QJsonValue& value) {
        if (!value.isArray() || value.toArray().size() != 2) {
            throw std::invalid_argument(
                "Project positions must contain two coordinates");
        }
        const auto coordinates = value.toArray();
        return QPointF(number(coordinates[0]), number(coordinates[1]));
    };
    if (!object.value("components").isArray() ||
        !object.value("wires").isArray() ||
        !object.value("counters").isArray() ||
        object.value("counters").toArray().size() != 6) {
        throw std::invalid_argument(
            "Project must contain components, wires, and six reference "
            "counters");
    }
    ProjectState state;
    for (const auto entry : object.value("components").toArray()) {
        if (!entry.isObject()) {
            throw std::invalid_argument("Invalid project component");
        }
        const auto component = entry.toObject();
        const double kind = number(component.value("kind"));
        if (kind < 0 || kind >= 6 || std::floor(kind) != kind ||
            !component.value("reference").isString()) {
            throw std::invalid_argument("Invalid component kind or reference");
        }
        state.components.push_back(
            {static_cast<ComponentKind>(static_cast<int>(kind)),
             component.value("reference").toString(),
             point(component.value("position")),
             number(component.value("rotation")),
             number(component.value("value")),
             number(component.value("ideality"))});
    }
    for (const auto entry : object.value("wires").toArray()) {
        if (!entry.isObject()) {
            throw std::invalid_argument("Invalid project wire");
        }
        const auto wire = entry.toObject();
        state.wires.emplace_back(point(wire.value("start")),
                                 point(wire.value("end")));
    }
    const auto counters = object.value("counters").toArray();
    for (std::size_t index = 0; index < state.counters.size(); ++index) {
        const double counter = number(counters[static_cast<qsizetype>(index)]);
        if (counter < 0 || counter > 999999999 ||
            std::floor(counter) != counter) {
            throw std::invalid_argument("Invalid component reference counter");
        }
        state.counters[index] = static_cast<int>(counter);
    }
    validateProject(state);
    return state;
}

inline ProjectState readProject(const QString& filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(file.errorString().toStdString());
    }
    if (file.size() > 16 * 1024 * 1024) {
        throw std::invalid_argument("Project exceeds the 16 MiB file limit");
    }
    const auto contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        throw std::runtime_error(file.errorString().toStdString());
    }
    return parseProject(contents);
}

inline void writeProject(const QString& filename, const ProjectState& state) {
    const auto contents = serializeProject(state);
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(contents) != contents.size() || !file.commit()) {
        throw std::runtime_error(file.errorString().toStdString());
    }
}

}  // namespace microspice::gui
#pragma once

#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>

#include "microspice/schematic.hpp"
#include "microspice/script/compiler.hpp"

namespace microspice::web {

using nlohmann::json;
using schematic::ComponentKind;

inline constexpr std::size_t projectBytes = 16 * 1024 * 1024;
inline constexpr std::size_t simulationComponents = 1000;
inline constexpr std::size_t simulationWires = 2000;

inline double number(const json& value) {
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        throw std::invalid_argument(
            "Numeric fields must contain finite numbers (not booleans)");
    return value.get<double>();
}

inline schematic::Point point(const json& value) {
    if (!value.is_array() || value.size() != 2)
        throw std::invalid_argument(
            "Project positions must contain two coordinates");
    const schematic::Point result{number(value[0]), number(value[1])};
    for (const auto coordinate : {result.x, result.y}) {
        if (std::abs(coordinate) > 1000000 || std::fmod(coordinate, 20.0) != 0)
            throw std::invalid_argument(
                "Project coordinates must be on the 20-unit grid within "
                "+/-1000000");
    }
    return result;
}

inline bool reference(const std::string& name) {
    return name.size() >= 2 && name.size() <= 10 &&
           std::string("RCDVIG").find(name[0]) != std::string::npos &&
           name[1] >= '1' && name[1] <= '9' &&
           std::all_of(name.begin() + 2, name.end(), [](char character) {
               return character >= '0' && character <= '9';
           });
}

struct Project {
    json normalized;
    std::vector<schematic::Component> components;
    std::vector<schematic::Wire> wires;
};

inline Project parseProject(const json& project) {
    if (project.dump().size() > projectBytes)
        throw std::invalid_argument(
            "Project exceeds the 16 MiB serialized UTF-8 limit");
    if (!project.is_object() ||
        project.value("format", json()) != "microspice.project" ||
        !project.contains("version") || number(project.at("version")) != 1)
        throw std::invalid_argument(
            "Unsupported microspice project format or version");
    for (const auto* field : {"components", "wires", "counters"}) {
        if (!project.contains(field) || !project.at(field).is_array())
            throw std::invalid_argument(
                "Project must contain components, wires, and six reference "
                "counters");
    }
    if (project.at("counters").size() != 6)
        throw std::invalid_argument(
            "Project must contain six reference counters");
    if (project.at("components").size() > 10000 ||
        project.at("wires").size() > 50000)
        throw std::invalid_argument(
            "Project exceeds the component or wire limit");
    Project result;
    result.normalized = {{"format", "microspice.project"},
                         {"version", 1},
                         {"components", json::array()},
                         {"wires", json::array()},
                         {"counters", json::array()}};
    for (const auto& entry : project.at("counters")) {
        const double counter = number(entry);
        if (counter < 0 || counter > 999999999 ||
            std::floor(counter) != counter)
            throw std::invalid_argument("Invalid component reference counter");
        result.normalized["counters"].push_back(static_cast<int>(counter));
    }
    std::set<std::string> references;
    for (const auto& entry : project.at("components")) {
        if (!entry.is_object())
            throw std::invalid_argument("Invalid project component");
        const auto kind = number(entry.at("kind"));
        if (kind < 0 || kind >= 6 || std::floor(kind) != kind ||
            !entry.at("reference").is_string())
            throw std::invalid_argument("Invalid component kind or reference");
        const auto name = entry.at("reference").get<std::string>();
        const auto index = static_cast<std::size_t>(kind);
        if (!reference(name) || name[0] != std::string("RCDVIG")[index] ||
            !references.insert(name).second)
            throw std::invalid_argument(
                "Invalid or duplicate component reference");
        const auto position = point(entry.at("position"));
        const auto rotation = number(entry.at("rotation"));
        if (rotation < 0 || rotation >= 360 || std::fmod(rotation, 90.0) != 0)
            throw std::invalid_argument(
                "Component rotation must be 0, 90, 180, or 270 degrees");
        const auto value = number(entry.at("value"));
        const auto ideality = number(entry.at("ideality"));
        if ((index < 3 && value <= 0) || (index == 5 && value != 0) ||
            ideality <= 0)
            throw std::invalid_argument(
                "Invalid component value or diode ideality");
        static constexpr std::array<schematic::Point, 4> offsets{
            {{60, 0}, {0, 60}, {-60, 0}, {0, -60}}};
        const auto offset = offsets[static_cast<std::size_t>(rotation / 90)];
        const auto positive = index == 5
                                  ? position
                                  : schematic::Point{position.x - offset.x,
                                                     position.y - offset.y};
        const schematic::Point negative{position.x + offset.x,
                                        position.y + offset.y};
        result.components.push_back({static_cast<ComponentKind>(index), name,
                                     positive, negative, value, ideality});
        result.normalized["components"].push_back(
            {{"kind", index},
             {"reference", name},
             {"position", {position.x, position.y}},
             {"rotation", rotation},
             {"value", value},
             {"ideality", ideality}});
    }
    for (const auto& entry : project.at("wires")) {
        if (!entry.is_object())
            throw std::invalid_argument("Invalid project wire");
        const auto start = point(entry.at("start"));
        const auto end = point(entry.at("end"));
        if (start == end || (start.x != end.x && start.y != end.y))
            throw std::invalid_argument(
                "Project wires must be nonzero horizontal or vertical "
                "segments");
        result.wires.push_back({start, end});
        result.normalized["wires"].push_back(
            {{"start", {start.x, start.y}}, {"end", {end.x, end.y}}});
    }
    return result;
}

inline std::string precise(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::scientific
           << std::setprecision(std::numeric_limits<double>::max_digits10)
           << value;
    return output.str();
}

struct Simulation {
    script::CompiledSimulation compiled;
    json terminals;
};

inline Simulation compileProject(const Project& project, const json& settings) {
    if (!settings.is_object())
        throw std::invalid_argument("Settings must be an object");
    if (project.components.size() > simulationComponents ||
        project.wires.size() > simulationWires)
        throw std::invalid_argument(
            "Simulation geometry budget exceeded: at most 1000 components and "
            "2000 wires before quadratic connectivity extraction");
    auto extracted =
        schematic::extractCircuit(project.components, project.wires);
    const auto& circuit = extracted.circuit;
    if (circuit.nodes().size() + circuit.voltageSources().size() >
        script::Limits{}.unknowns)
        throw std::invalid_argument("MNA unknown limit exceeded (512)");
    const auto kind = settings.value("kind", std::string("dc"));
    if (kind != "dc" && kind != "transient" && kind != "sweep")
        throw std::invalid_argument(
            "Analysis kind must be dc, transient, or sweep");
    for (const auto* field : {"step", "stop", "start"}) {
        if (settings.contains(field)) number(settings.at(field));
    }
    const auto node = [](const std::string& name) {
        return name == "0" ? std::string("ground") : name;
    };
    static constexpr std::array<const char*, 5> functions{
        {"resistor", "capacitor", "diode", "voltage_source", "current_source"}};
    static constexpr std::array<const char*, 5> units{
        {"ohm", "F", "A", "V", "A"}};
    std::ostringstream source;
    source.imbue(std::locale::classic());
    for (const auto& component : project.components) {
        if (component.kind == ComponentKind::Ground) continue;
        const auto index = static_cast<std::size_t>(component.kind);
        const auto& terminals = extracted.terminals.at(component.name);
        source << functions[index] << "(\"" << component.name << "\", "
               << node(terminals[0]) << ", " << node(terminals[1]) << ", ";
        if (component.kind == ComponentKind::Diode) source << "saturation=";
        source << precise(component.value) << units[index];
        if (component.kind == ComponentKind::Diode)
            source << ", ideality=" << precise(component.ideality);
        source << ")\n";
    }
    if (kind == "dc") {
        source << "dc(\"schematic\")\n";
    } else if (kind == "transient") {
        source << "transient(\"schematic\", step="
               << precise(number(settings.at("step")))
               << "s, stop=" << precise(number(settings.at("stop"))) << "s)\n";
    } else {
        const auto sourceName = settings.at("source").get<std::string>();
        if (!reference(sourceName) || sourceName[0] != 'V')
            throw std::invalid_argument(
                "Sweep requires an existing voltage source reference");
        source << "sweep(\"schematic\", source=\"" << sourceName
               << "\", start=" << precise(number(settings.at("start")))
               << "V, stop=" << precise(number(settings.at("stop")))
               << "V, step=" << precise(number(settings.at("step"))) << "V)\n";
    }
    if (settings.contains("initialVoltages")) {
        const auto& initial = settings.at("initialVoltages");
        if (!initial.is_object())
            throw std::invalid_argument(
                "Initial voltages must be a capacitor reference map");
        for (const auto& entry : initial.items()) {
            if (kind != "transient" || !reference(entry.key()) ||
                entry.key()[0] != 'C')
                throw std::invalid_argument(
                    "Initial voltages require transient capacitor references");
            source << "initial(\"schematic\", \"" << entry.key() << "\", "
                   << precise(number(entry.value())) << "V)\n";
        }
    }
    source << "observe(\"schematic\", voltage(ground)";
    for (const auto& name : circuit.nodes())
        source << ", voltage(" << name << ")";
    for (const auto& component : circuit.voltageSources())
        source << ", current(\"" << component.name << "\")";
    for (const auto& component : circuit.currentSources())
        source << ", current(\"" << component.name << "\")";
    source << ")\n";
    auto compiled = script::compile(source.str());
    compiled.circuit = std::move(extracted.circuit);
    for (auto& entry : compiled.nodes)
        entry.second = entry.first == "ground" ? "0" : entry.first;
    return {std::move(compiled), extracted.terminals};
}

}  // namespace microspice::web
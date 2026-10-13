// circuit.hpp - A GUI-independent description of an electrical circuit.

#ifndef MICROSPICE_CIRCUIT_HPP
#define MICROSPICE_CIRCUIT_HPP

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace microspice {

struct Resistor {
    std::string name;
    std::string positiveNode;
    std::string negativeNode;
    double resistanceOhms;
};

/// Positive current flows from positiveNode to negativeNode.
struct CurrentSource {
    std::string name;
    std::string positiveNode;
    std::string negativeNode;
    double currentAmps;
};

/// voltageVolts is V(positiveNode) - V(negativeNode).
struct VoltageSource {
    std::string name;
    std::string positiveNode;
    std::string negativeNode;
    double voltageVolts;
};

struct Capacitor {
    std::string name;
    std::string positiveNode;
    std::string negativeNode;
    double capacitanceFarads;
};

class Circuit {
public:
    void addResistor(std::string name, std::string positiveNode,
                     std::string negativeNode, double resistanceOhms) {
        if (!std::isfinite(resistanceOhms) || resistanceOhms <= 0.0) {
            throw std::invalid_argument(
                "resistance must be finite and greater than zero");
        }
        registerNode(positiveNode);
        registerNode(negativeNode);
        resistors_.push_back({std::move(name), std::move(positiveNode),
                              std::move(negativeNode), resistanceOhms});
    }

    void addCurrentSource(std::string name, std::string positiveNode,
                          std::string negativeNode, double currentAmps) {
        if (!std::isfinite(currentAmps)) {
            throw std::invalid_argument("current must be finite");
        }
        registerNode(positiveNode);
        registerNode(negativeNode);
        currentSources_.push_back({std::move(name), std::move(positiveNode),
                                   std::move(negativeNode), currentAmps});
    }

    void addVoltageSource(std::string name, std::string positiveNode,
                          std::string negativeNode, double voltageVolts) {
        if (!std::isfinite(voltageVolts)) {
            throw std::invalid_argument("voltage must be finite");
        }
        registerNode(positiveNode);
        registerNode(negativeNode);
        voltageSources_.push_back({std::move(name), std::move(positiveNode),
                                   std::move(negativeNode), voltageVolts});
    }

    void addCapacitor(std::string name, std::string positiveNode,
                      std::string negativeNode, double capacitanceFarads) {
        if (!std::isfinite(capacitanceFarads) || capacitanceFarads <= 0.0) {
            throw std::invalid_argument(
                "capacitance must be finite and greater than zero");
        }
        registerNode(positiveNode);
        registerNode(negativeNode);
        capacitors_.push_back({std::move(name), std::move(positiveNode),
                               std::move(negativeNode), capacitanceFarads});
    }

    const std::vector<std::string>& nodes() const noexcept { return nodes_; }
    const std::vector<Resistor>& resistors() const noexcept {
        return resistors_;
    }
    const std::vector<CurrentSource>& currentSources() const noexcept {
        return currentSources_;
    }
    const std::vector<VoltageSource>& voltageSources() const noexcept {
        return voltageSources_;
    }
    const std::vector<Capacitor>& capacitors() const noexcept {
        return capacitors_;
    }

private:
    static bool isGround(const std::string& node) noexcept {
        return node == "0" || node == "gnd" || node == "GND";
    }

    void registerNode(const std::string& node) {
        if (node.empty()) {
            throw std::invalid_argument("node name cannot be empty");
        }
        if (isGround(node)) {
            return;
        }
        for (const std::string& existing : nodes_) {
            if (existing == node) {
                return;
            }
        }
        nodes_.push_back(node);
    }

    std::vector<std::string> nodes_;
    std::vector<Resistor> resistors_;
    std::vector<CurrentSource> currentSources_;
    std::vector<VoltageSource> voltageSources_;
    std::vector<Capacitor> capacitors_;
};

}  // namespace microspice

#endif  // MICROSPICE_CIRCUIT_HPP
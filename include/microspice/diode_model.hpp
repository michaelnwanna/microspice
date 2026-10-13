// diode_model.hpp - Shockley diode equation and tangent linearization.

#ifndef MICROSPICE_DIODE_MODEL_HPP
#define MICROSPICE_DIODE_MODEL_HPP

#include <cmath>

#include "microspice/circuit.hpp"

namespace microspice {

inline constexpr double kDefaultThermalVoltageVolts = 0.025852;

struct DiodeLinearization {
    double currentAmps;
    double conductanceSiemens;
    double equivalentCurrentAmps;
};

/// Evaluate the diode and its tangent at voltage V(positive)-V(negative).
inline DiodeLinearization linearizeDiode(
    const Diode& diode, double voltageVolts,
    double thermalVoltageVolts = kDefaultThermalVoltageVolts) {
    const double scaledVoltage =
        voltageVolts / (diode.idealityFactor * thermalVoltageVolts);
    const double exponential = std::exp(scaledVoltage);
    const double current = diode.saturationCurrentAmps * (exponential - 1.0);
    const double conductance = diode.saturationCurrentAmps * exponential /
                               (diode.idealityFactor * thermalVoltageVolts);
    return {current, conductance, current - conductance * voltageVolts};
}

}  // namespace microspice

#endif  // MICROSPICE_DIODE_MODEL_HPP
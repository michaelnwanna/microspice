#pragma once

#include <array>
#include <cmath>
#include <map>
#include <string>

#include "microspice/script/source.hpp"

namespace microspice::script {

using Dimension = std::array<int, 3>;
inline constexpr Dimension dimensionless{0, 0, 0};
inline constexpr Dimension volts{1, 0, 0};
inline constexpr Dimension amps{0, 1, 0};
inline constexpr Dimension seconds{0, 0, 1};
inline constexpr Dimension ohms{1, -1, 0};
inline constexpr Dimension farads{-1, 1, 1};

struct Quantity {
    double value = 0;
    Dimension dimension = dimensionless;
};

inline std::string unitName(Dimension dimension) {
    if (dimension == volts) return "V";
    if (dimension == amps) return "A";
    if (dimension == seconds) return "s";
    if (dimension == ohms) return "ohm";
    if (dimension == farads) return "F";
    if (dimension == dimensionless) return "1";
    return "V^" + std::to_string(dimension[0]) + " A^" +
           std::to_string(dimension[1]) + " s^" + std::to_string(dimension[2]);
}

inline Quantity finite(Quantity quantity, Span span) {
    require(std::isfinite(quantity.value), span,
            "arithmetic must remain finite");
    return quantity;
}

inline Quantity literal(double value, const std::string& unit, Span span) {
    if (unit.empty()) return finite({value, dimensionless}, span);
    const std::map<std::string, Dimension> units{{"V", volts},
                                                 {"A", amps},
                                                 {"s", seconds},
                                                 {"ohm", ohms},
                                                 {"F", farads}};
    const std::map<char, double> prefixes{
        {'p', 1e-12}, {'n', 1e-9}, {'u', 1e-6}, {'m', 1e-3},
        {'k', 1e3},   {'M', 1e6},  {'G', 1e9},  {'T', 1e12}};
    auto found = units.find(unit);
    double scale = 1;
    if (found == units.end() && unit.size() > 1) {
        auto prefix = prefixes.find(unit[0]);
        if (prefix != prefixes.end()) {
            scale = prefix->second;
            found = units.find(unit.substr(1));
        }
    }
    require(
        found != units.end(), span,
        "unknown unit '" + unit + "'; use V, A, s, ohm or F with an SI prefix");
    return finite({value * scale, found->second}, span);
}

inline Quantity arithmetic(const std::string& operation, Quantity left,
                           Quantity right, Span span) {
    Dimension dimension = left.dimension;
    if (operation == "+" || operation == "-") {
        require(dimension == right.dimension, span,
                "incompatible dimensions: " + unitName(dimension) + " and " +
                    unitName(right.dimension));
        return finite({operation == "+" ? left.value + right.value
                                        : left.value - right.value,
                       dimension},
                      span);
    }
    require(operation == "*" || operation == "/", span,
            "unknown arithmetic operator");
    if (operation == "/") require(right.value != 0, span, "division by zero");
    for (std::size_t index = 0; index < dimension.size(); ++index) {
        const long exponent =
            static_cast<long>(dimension[index]) +
            (operation == "*" ? static_cast<long>(right.dimension[index])
                              : -static_cast<long>(right.dimension[index]));
        require(exponent >= -1024 && exponent <= 1024, span,
                "dimension exponent limit exceeded");
        dimension[index] = static_cast<int>(exponent);
    }
    return finite(
        {operation == "*" ? left.value * right.value : left.value / right.value,
         dimension},
        span);
}

inline double inUnits(Quantity quantity, Dimension expected, Span span) {
    require(quantity.dimension == expected, span,
            "expected " + unitName(expected) +
                "; add the appropriate unit to the value");
    return quantity.value;
}

}  // namespace microspice::script
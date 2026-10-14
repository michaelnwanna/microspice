#pragma once

#include <QString>
#include <array>
#include <cmath>

#include "microspice/netlist.hpp"

namespace microspice::gui {

inline QString formatEngineeringValue(double value, int precision = 12) {
    if (value == 0) {
        return "0";
    }
    if (!std::isfinite(value)) {
        return QString::number(value, 'g', precision);
    }
    constexpr std::array<double, 9> scales = {1e-12, 1e-9, 1e-6, 1e-3, 1,
                                              1e3,   1e6,  1e9,  1e12};
    constexpr std::array<const char*, 9> suffixes = {"p", "n", "u", "m", "",
                                                     "K", "M", "G", "T"};
    std::size_t index = 0;
    while (index + 1 < scales.size() && std::abs(value) >= scales[index + 1]) {
        ++index;
    }
    QString number = QString::number(value / scales[index], 'g', precision);
    if (std::abs(number.toDouble()) >= 1000 && index + 1 < scales.size()) {
        ++index;
        number = QString::number(value / scales[index], 'g', precision);
    }
    return number + QString::fromLatin1(suffixes[index]);
}

inline double parseGuiEngineeringValue(const QString& text) {
    QString normalized = text.trimmed();
    if (normalized.endsWith('M')) {
        normalized.chop(1);
        normalized += "meg";
    }
    return parseEngineeringValue(normalized.toStdString());
}

}  // namespace microspice::gui
#pragma once

#include <QString>
#include <array>

namespace microspice::gui {

struct ExampleSchematic {
    QString title;
    QString resource;
    bool transient;
};

inline const std::array<ExampleSchematic, 5>& exampleSchematics() {
    static const std::array<ExampleSchematic, 5> examples = {{
        {"Ohm's law - resistor load", ":/examples/resistor_load.microspice",
         false},
        {"Voltage divider", ":/examples/voltage_divider.microspice", false},
        {"Current-driven resistor",
         ":/examples/current_driven_resistor.microspice", false},
        {"RC charging", ":/examples/rc_charging.microspice", true},
        {"Diode bias", ":/examples/diode_bias.microspice", false},
    }};
    return examples;
}

}  // namespace microspice::gui
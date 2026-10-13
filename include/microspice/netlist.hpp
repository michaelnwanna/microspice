// netlist.hpp - A small, strict SPICE-like netlist parser.

#ifndef MICROSPICE_NETLIST_HPP
#define MICROSPICE_NETLIST_HPP

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "microspice/circuit.hpp"

namespace microspice {

class NetlistError : public std::runtime_error {
public:
    NetlistError(std::size_t line, const std::string& message)
        : std::runtime_error("line " + std::to_string(line) + ": " + message),
          line_(line) {}

    std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

struct TransientDirective {
    double timeStepSeconds;
    double stopTimeSeconds;
};

struct ParsedNetlist {
    Circuit circuit;
    std::optional<TransientDirective> transient;
};

inline std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

/// Parse SPICE-style values such as 1k, 4.7u, 2meg, or 10.
inline double parseEngineeringValue(const std::string& text) {
    std::size_t numberEnd = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &numberEnd);
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid numeric value: " + text);
    }

    if (numberEnd == 0 || !std::isfinite(value)) {
        throw std::invalid_argument("invalid numeric value: " + text);
    }

    const std::string suffix = lowercase(text.substr(numberEnd));
    double multiplier = 1.0;
    if (suffix.empty()) {
        multiplier = 1.0;
    } else if (suffix == "t") {
        multiplier = 1e12;
    } else if (suffix == "g") {
        multiplier = 1e9;
    } else if (suffix == "meg") {
        multiplier = 1e6;
    } else if (suffix == "k") {
        multiplier = 1e3;
    } else if (suffix == "m") {
        multiplier = 1e-3;
    } else if (suffix == "u") {
        multiplier = 1e-6;
    } else if (suffix == "n") {
        multiplier = 1e-9;
    } else if (suffix == "p") {
        multiplier = 1e-12;
    } else {
        throw std::invalid_argument("unsupported engineering suffix: " +
                                    suffix);
    }
    return value * multiplier;
}

namespace detail {

inline std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

inline double parseValueAtLine(const std::string& text, std::size_t line) {
    try {
        return parseEngineeringValue(text);
    } catch (const std::invalid_argument& error) {
        throw NetlistError(line, error.what());
    }
}

}  // namespace detail

/// Parse circuit records and analysis directives into a complete document.
inline ParsedNetlist parseNetlistDocument(const std::string& text) {
    ParsedNetlist parsed;
    std::istringstream input(text);
    std::string lineText;
    std::size_t lineNumber = 0;

    while (std::getline(input, lineText)) {
        ++lineNumber;
        const std::size_t comment = lineText.find(';');
        if (comment != std::string::npos) {
            lineText.erase(comment);
        }
        const std::vector<std::string> tokens = detail::tokenize(lineText);
        if (tokens.empty() || tokens[0][0] == '*' || tokens[0][0] == '#') {
            continue;
        }
        if (lowercase(tokens[0]) == ".end") {
            break;
        }

        if (lowercase(tokens[0]) == ".tran") {
            if (tokens.size() != 3) {
                throw NetlistError(
                    lineNumber,
                    "transient syntax is: .tran timestep stop-time");
            }
            if (parsed.transient) {
                throw NetlistError(lineNumber,
                                   "only one .tran directive is allowed");
            }
            const double timeStep =
                detail::parseValueAtLine(tokens[1], lineNumber);
            const double stopTime =
                detail::parseValueAtLine(tokens[2], lineNumber);
            if (timeStep <= 0.0 || stopTime < timeStep) {
                throw NetlistError(
                    lineNumber,
                    ".tran requires a positive timestep and stop >= timestep");
            }
            parsed.transient = TransientDirective{timeStep, stopTime};
            continue;
        }

        try {
            const char type = static_cast<char>(
                std::toupper(static_cast<unsigned char>(tokens[0][0])));
            if (type == 'R') {
                if (tokens.size() != 4) {
                    throw NetlistError(
                        lineNumber,
                        "resistor syntax is: Rname positive negative "
                        "resistance");
                }
                parsed.circuit.addResistor(
                    tokens[0], tokens[1], tokens[2],
                    detail::parseValueAtLine(tokens[3], lineNumber));
                continue;
            }

            if (type == 'C') {
                if (tokens.size() != 4) {
                    throw NetlistError(
                        lineNumber,
                        "capacitor syntax is: Cname positive negative "
                        "capacitance");
                }
                parsed.circuit.addCapacitor(
                    tokens[0], tokens[1], tokens[2],
                    detail::parseValueAtLine(tokens[3], lineNumber));
                continue;
            }

            if (type == 'I' || type == 'V') {
                std::size_t valueIndex = 3;
                if (tokens.size() == 5 && lowercase(tokens[3]) == "dc") {
                    valueIndex = 4;
                } else if (tokens.size() != 4) {
                    throw NetlistError(
                        lineNumber,
                        "source syntax is: Xname positive negative [DC] value");
                }
                const double value =
                    detail::parseValueAtLine(tokens[valueIndex], lineNumber);
                if (type == 'I') {
                    parsed.circuit.addCurrentSource(tokens[0], tokens[1],
                                                    tokens[2], value);
                } else {
                    parsed.circuit.addVoltageSource(tokens[0], tokens[1],
                                                    tokens[2], value);
                }
                continue;
            }

            throw NetlistError(
                lineNumber, "unsupported component or directive: " + tokens[0]);
        } catch (const NetlistError&) {
            throw;
        } catch (const std::invalid_argument& error) {
            throw NetlistError(lineNumber, error.what());
        }
    }
    return parsed;
}

/// Compatibility helper for clients interested only in circuit records.
inline Circuit parseNetlist(const std::string& text) {
    return parseNetlistDocument(text).circuit;
}

}  // namespace microspice

#endif  // MICROSPICE_NETLIST_HPP
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include "microspice/execution.hpp"

namespace microspice::script {

struct Span {
    std::size_t offset = 0;
    std::size_t length = 1;
    std::size_t line = 1;
    std::size_t column = 1;
};

struct Diagnostic {
    Span span;
    std::string message;
    bool warning = false;
};

class Error : public std::runtime_error {
public:
    Diagnostic diagnostic;
    Error(Span span, std::string message)
        : std::runtime_error(message),
          diagnostic{span, std::move(message), false} {}
};

struct Limits {
    std::size_t sourceBytes = 1024 * 1024;
    std::size_t nesting = 32;
    std::size_t components = 1000;
    std::size_t unknowns = 512;
    std::size_t analyses = 8;
    std::size_t samples = 2000;
    std::size_t retainedScalars = 4'000'000;
};

inline void require(bool condition, Span span, const std::string& message) {
    if (!condition) throw Error(span, message);
}

}  // namespace microspice::script
#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>

namespace microspice {

class AnalysisCancelled final : public std::runtime_error {
public:
    AnalysisCancelled() : std::runtime_error("Analysis cancelled") {}
};

struct ExecutionControl {
    std::function<bool()> cancelled;
    std::function<void(std::size_t, std::size_t)> progress;

    void check() const {
        if (cancelled && cancelled()) {
            throw AnalysisCancelled();
        }
    }
    void report(std::size_t completed, std::size_t total) const {
        check();
        if (progress) {
            progress(completed, total);
        }
    }
};

}  // namespace microspice
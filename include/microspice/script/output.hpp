#pragma once

#include <iomanip>
#include <ostream>

#include "microspice/script/executor.hpp"

namespace microspice::script {

inline std::string csvField(const std::string& text) {
    std::string escaped = "\"";
    for (char character : text) {
        if (character == '"') escaped += '"';
        escaped += character;
    }
    return escaped + '"';
}

inline void writeCsv(std::ostream& output, const AnalysisResult& result,
                     const ExecutionControl& execution = {}) {
    execution.check();
    if (result.kind == AnalysisKind::transient)
        output << "time_s";
    else if (result.kind == AnalysisKind::sweep)
        output << "source_V";
    else
        output << "sample";
    for (const auto& trace : result.observations)
        output << ','
               << csvField(trace.label + " [" +
                           unitName(trace.value.dimension) + "]");
    output << '\n' << std::setprecision(17);
    const std::size_t count =
        result.kind == AnalysisKind::dc ? 1 : result.axis.size();
    for (std::size_t index = 0; index < count; ++index) {
        execution.check();
        if (result.kind == AnalysisKind::dc)
            output << 0;
        else
            output << result.axis[index];
        for (const auto& trace : result.observations)
            output << ',' << trace.value.data[index];
        output << '\n';
    }
}

inline void writeDiagnostic(std::ostream& output, const std::string& path,
                            const std::string& source,
                            const Diagnostic& diagnostic) {
    output << path << ':' << diagnostic.span.line << ':'
           << diagnostic.span.column << ": "
           << (diagnostic.warning ? "warning: " : "error: ")
           << diagnostic.message << '\n';
    const auto offset = std::min(diagnostic.span.offset, source.size());
    auto start = source.rfind('\n', offset == 0 ? 0 : offset - 1);
    start = start == std::string::npos ? 0 : start + 1;
    auto end = source.find('\n', offset);
    if (end == std::string::npos) end = source.size();
    if (end >= start) {
        const std::size_t windowStart =
            offset > start + 100 ? offset - 100 : start;
        const auto length = std::min<std::size_t>(end - windowStart, 200);
        std::string line = source.substr(windowStart, length);
        std::replace(line.begin(), line.end(), '\t', ' ');
        output << line << '\n'
               << std::string(offset - windowStart, ' ') << '^' << '\n';
    }
}

}  // namespace microspice::script
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "microspice/script/output.hpp"

namespace {
volatile std::sig_atomic_t interrupted = 0;
void cancel(int) {
    interrupted = 1;
}

int usage(const std::string& message = "") {
    if (!message.empty()) std::cerr << message << '\n';
    std::cerr << "Usage: microspice_script check file.msim\n"
                 "       microspice_script run file.msim [--analysis name] "
                 "[--csv analysis-name]\n";
    return 2;
}
}  // namespace

int main(int argc, char* argv[]) {
    using namespace microspice::script;
    if (argc == 2 && std::string(argv[1]) == "--help") {
        usage();
        return 0;
    }
    if (argc < 3) return usage();
    const std::string command = argv[1];
    if (command != "check" && command != "run")
        return usage("Unknown command: " + command);
    const std::string path = argv[2];
    if (path.empty() || path[0] == '-')
        return usage("Expected script file path");
    std::string selected, csv;
    for (int index = 3; index < argc; ++index) {
        const std::string option = argv[index];
        if (command != "run" || (option != "--analysis" && option != "--csv"))
            return usage("Unknown option: " + option);
        if (index + 1 >= argc || std::string(argv[index + 1]).empty() ||
            std::string(argv[index + 1]).rfind("--", 0) == 0)
            return usage("Missing value for " + option);
        auto& target = option == "--analysis" ? selected : csv;
        if (!target.empty()) return usage("Duplicate option: " + option);
        target = argv[++index];
    }
    if (!selected.empty() && !csv.empty() && selected != csv)
        return usage("--csv must match --analysis when both are provided");
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(path, fileError)) {
        std::cerr << "Script input must be a readable regular file: " << path
                  << '\n';
        return 2;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Cannot open script: " << path << '\n';
        return 2;
    }
    const Limits limits;
    std::string source(limits.sourceBytes + 1, '\0');
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (input.bad()) {
        std::cerr << "Cannot read script: " << path << '\n';
        return 2;
    }
    source.resize(static_cast<std::size_t>(input.gcount()));
    std::signal(SIGINT, cancel);
    const microspice::ExecutionControl execution{
        [] { return interrupted != 0; }, {}};
    try {
        auto simulation = compile(source, limits, execution);
        for (const auto& warning : simulation.warnings)
            writeDiagnostic(std::cerr, path, source, warning);
        if (command == "check") {
            std::cout << "Valid: " << simulation.componentSpans.size()
                      << " components, " << simulation.analyses.size()
                      << " analyses\n";
            return 0;
        }
        for (const auto& name : {selected, csv})
            if (!name.empty()) {
                bool found = false;
                for (const auto& analysis : simulation.analyses)
                    found |= analysis.name == name;
                if (!found) return usage("Unknown analysis: " + name);
            }
        const auto result = run(simulation, execution, selected);
        if (!csv.empty()) {
            for (const auto& analysis : result.analyses)
                if (analysis.name == csv)
                    writeCsv(std::cout, analysis, execution);
        } else {
            std::cout << std::setprecision(12);
            for (const auto& analysis : result.analyses) {
                std::cout << "Analysis " << analysis.name << '\n';
                for (const auto& trace : analysis.observations) {
                    std::cout << "  " << trace.label << " = "
                              << trace.value.data.back() << ' '
                              << unitName(trace.value.dimension);
                    if (!trace.value.scalar())
                        std::cout << " (final of " << trace.value.data.size()
                                  << " samples)";
                    std::cout << '\n';
                }
            }
            for (const auto& measurement : result.measurements)
                std::cout << measurement.name << " = "
                          << measurement.value.value << ' '
                          << unitName(measurement.value.dimension) << '\n';
        }
        for (const auto& check : result.checks)
            std::cerr << (check.passed ? "PASS " : "FAIL ") << check.name
                      << '\n';
        for (const auto& skipped : result.skipped)
            std::cerr << "SKIP " << skipped << " (analysis not selected)\n";
        if (!std::cout) {
            std::cerr << "Output error\n";
            return 2;
        }
        execution.check();
        return result.checksPassed() ? 0 : 3;
    } catch (const microspice::AnalysisCancelled&) {
        std::cerr << "Cancelled\n";
        return 130;
    } catch (const Error& error) {
        writeDiagnostic(std::cerr, path, source, error.diagnostic);
        return 1;
    } catch (const std::exception& error) {
        std::cerr << path << ": error: " << error.what() << '\n';
        return 1;
    }
}
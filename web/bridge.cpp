#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "microspice/script/output.hpp"

using nlohmann::json;
namespace script = microspice::script;

std::string kindName(script::AnalysisKind kind) {
    if (kind == script::AnalysisKind::transient) return "transient";
    if (kind == script::AnalysisKind::sweep) return "sweep";
    return "dc";
}

json diagnosticJson(const script::Diagnostic& diagnostic) {
    return {
        {"message", diagnostic.message},    {"warning", diagnostic.warning},
        {"line", diagnostic.span.line},     {"column", diagnostic.span.column},
        {"offset", diagnostic.span.offset}, {"length", diagnostic.span.length}};
}

int main() {
    json response = {{"ok", false}, {"diagnostics", json::array()}};
    try {
        const auto request = json::parse(std::cin);
        const auto source = request.at("source").get<std::string>();
        const auto operation = request.value("operation", "run");
        if (operation != "run" && operation != "compile")
            throw std::invalid_argument("Unknown operation");
        const auto compiled = script::compile(source);
        response["diagnostics"] = json::array();
        for (const auto& warning : compiled.warnings)
            response["diagnostics"].push_back(diagnosticJson(warning));
        response["analyses"] = json::array();
        for (const auto& analysis : compiled.analyses)
            response["analyses"].push_back(
                {{"name", analysis.name}, {"kind", kindName(analysis.kind)}});
        response["nodes"] = compiled.circuit.nodes();
        response["components"] = compiled.componentSpans.size();
        if (operation == "run") {
            const auto result =
                script::run(compiled, {}, request.value("analysis", ""));
            response["results"] = json::array();
            for (const auto& analysis : result.analyses) {
                json observations = json::array();
                for (const auto& trace : analysis.observations)
                    observations.push_back(
                        {{"label", trace.label},
                         {"unit", script::unitName(trace.value.dimension)},
                         {"scalar", trace.value.scalar()},
                         {"data", trace.value.data}});
                std::ostringstream csv;
                script::writeCsv(csv, analysis);
                response["results"].push_back(
                    {{"name", analysis.name},
                     {"kind", kindName(analysis.kind)},
                     {"axis", analysis.axis},
                     {"axisUnit", script::unitName(analysis.axisDimension)},
                     {"observations", observations},
                     {"csv", csv.str()}});
            }
            response["measurements"] = json::array();
            for (const auto& measurement : result.measurements)
                response["measurements"].push_back(
                    {{"name", measurement.name},
                     {"value", measurement.value.value},
                     {"unit", script::unitName(measurement.value.dimension)}});
            response["checks"] = json::array();
            for (const auto& check : result.checks)
                response["checks"].push_back(
                    {{"name", check.name}, {"passed", check.passed}});
            response["skipped"] = result.skipped;
            response["checksPassed"] = result.checksPassed();
        }
        response["ok"] = true;
    } catch (const script::Error& error) {
        response["diagnostics"].push_back(diagnosticJson(error.diagnostic));
    } catch (const std::exception& error) {
        response["diagnostics"].push_back(
            {{"message", error.what()}, {"warning", false}});
    }
    std::cout << response.dump() << '\n';
}
#pragma once

#include <variant>

#include "microspice/dc_sweep.hpp"
#include "microspice/script/compiler.hpp"
#include "microspice/transient_solver.hpp"

namespace microspice::script {

using SolverResult =
    std::variant<NonlinearDcResult, TransientResult, DcSweepResult>;
struct Trace {
    std::string label;
    Value value;
};
struct AnalysisResult {
    std::string name;
    AnalysisKind kind;
    Dimension axisDimension;
    std::vector<double> axis;
    SolverResult solver;
    std::vector<Trace> observations;
};
struct MeasurementResult {
    std::string name;
    Quantity value;
};
struct CheckResult {
    std::string name;
    bool passed;
};
struct RunResult {
    std::vector<AnalysisResult> analyses;
    std::vector<MeasurementResult> measurements;
    std::vector<CheckResult> checks;
    std::vector<std::string> skipped;
    bool checksPassed() const {
        return std::all_of(
            checks.begin(), checks.end(),
            [](const CheckResult& result) { return result.passed; });
    }
};

inline std::string queryLabel(const Expr& expression) {
    std::string label = expression->text + "(";
    for (std::size_t index = 0; index < expression->arguments.size(); ++index) {
        if (index) label += ",";
        label += expression->arguments[index].value->text;
    }
    return label + ")";
}

inline double solutionQuery(const CompiledSimulation& simulation,
                            const Expr& expression,
                            const std::vector<double>& solution) {
    const auto& circuit = simulation.circuit;
    auto voltage = [&](const std::string& node) {
        if (node == "0") return 0.0;
        const auto found =
            std::find(circuit.nodes().begin(), circuit.nodes().end(), node);
        require(found != circuit.nodes().end(), expression->span,
                "unresolved node in compiled simulation");
        return solution.at(
            static_cast<std::size_t>(found - circuit.nodes().begin()));
    };
    if (expression->text == "voltage") {
        double result = voltage(
            observationNode(simulation, expression->arguments[0].value));
        if (expression->arguments.size() == 2)
            result -= voltage(
                observationNode(simulation, expression->arguments[1].value));
        return finite({result, volts}, expression->span).value;
    }
    const auto name = stringValue(expression->arguments[0].value);
    for (std::size_t index = 0; index < circuit.voltageSources().size();
         ++index)
        if (circuit.voltageSources()[index].name == name)
            return solution.at(circuit.nodes().size() + index);
    for (const auto& resistor : circuit.resistors())
        if (resistor.name == name)
            return finite({(voltage(resistor.positiveNode) -
                            voltage(resistor.negativeNode)) /
                               resistor.resistanceOhms,
                           amps},
                          expression->span)
                .value;
    for (const auto& source : circuit.currentSources())
        if (source.name == name) return source.currentAmps;
    throw Error(expression->span, "unsupported current query");
}

inline Value queryResult(const CompiledSimulation& simulation,
                         const AnalysisRequest& request,
                         const AnalysisResult& result, const Expr& expression,
                         const ExecutionControl& execution) {
    validateQuery(simulation, expression);
    Value value{expression->text == "voltage" ? volts : amps,
                {},
                request.kind == AnalysisKind::dc ? "" : request.name};
    auto append = [&](const std::vector<double>& solution) {
        execution.check();
        const auto sample = solutionQuery(simulation, expression, solution);
        value.data.push_back(
            finite({sample, value.dimension}, expression->span).value);
    };
    if (const auto* dc = std::get_if<NonlinearDcResult>(&result.solver))
        append(dc->operatingPoint.solution);
    else if (const auto* transient =
                 std::get_if<TransientResult>(&result.solver)) {
        for (const auto& sample : transient->samples) append(sample.solution);
    } else {
        for (const auto& point : std::get<DcSweepResult>(result.solver).points)
            append(point.result.operatingPoint.solution);
    }
    return value;
}

inline RunResult run(const CompiledSimulation& simulation,
                     const ExecutionControl& execution = {},
                     const std::string& selectedAnalysis = "") {
    execution.check();
    require(!simulation.analyses.empty(), {},
            "no analyses declared; add dc, transient or sweep");
    if (!selectedAnalysis.empty())
        analysisByName(simulation, selectedAnalysis, {});
    RunResult result;
    std::set<std::string> available;
    for (const auto& request : simulation.analyses) {
        execution.check();
        if (!selectedAnalysis.empty() && request.name != selectedAnalysis)
            continue;
        AnalysisResult analysis{request.name, request.kind,      dimensionless,
                                {},           TransientResult{}, {}};
        try {
            if (request.kind == AnalysisKind::dc) {
                auto solved =
                    solveNonlinearDc(simulation.circuit, {}, execution);
                require(solved.converged, request.span,
                        "DC analysis '" + request.name + "' did not converge");
                for (double sample : solved.operatingPoint.solution)
                    finite({sample, dimensionless}, request.span);
                analysis.solver = std::move(solved);
            } else if (request.kind == AnalysisKind::transient) {
                auto solved = solveTransient(
                    simulation.circuit,
                    {request.step, request.stop, request.initialVoltages},
                    execution);
                analysis.axisDimension = seconds;
                for (const auto& sample : solved.samples) {
                    execution.check();
                    for (double value : sample.solution)
                        finite({value, dimensionless}, request.span);
                    analysis.axis.push_back(sample.timeSeconds);
                }
                analysis.solver = std::move(solved);
            } else {
                auto solved = solveDcSweep(simulation.circuit,
                                           {request.source,
                                            request.start,
                                            request.stop,
                                            request.step,
                                            {}},
                                           execution);
                analysis.axisDimension = volts;
                for (const auto& point : solved.points) {
                    execution.check();
                    require(point.result.converged, request.span,
                            "sweep analysis '" + request.name +
                                "' did not converge at " +
                                std::to_string(point.sourceVoltage) + " V");
                    for (double value : point.result.operatingPoint.solution)
                        finite({value, dimensionless}, request.span);
                    analysis.axis.push_back(point.sourceVoltage);
                }
                analysis.solver = std::move(solved);
            }
        } catch (const AnalysisCancelled&) {
            throw;
        } catch (const Error&) {
            throw;
        } catch (const std::exception& error) {
            throw Error(request.span,
                        "analysis '" + request.name + "': " + error.what());
        }
        for (const auto& observation : simulation.observations)
            if (observation.analysis == request.name) {
                analysis.observations.push_back(
                    {queryLabel(observation.expression),
                     queryResult(simulation, request, analysis,
                                 observation.expression, execution)});
            }
        available.insert(request.name);
        result.analyses.push_back(std::move(analysis));
    }
    auto dependenciesAvailable =
        [&](const std::set<std::string>& dependencies) {
            return std::all_of(dependencies.begin(), dependencies.end(),
                               [&](const std::string& dependency) {
                                   return available.count(dependency) != 0;
                               });
        };
    Environment measurements;
    auto environment = [&](const Environment& parameters,
                           const Expr& expression) {
        Environment combined = parameters;
        const auto referenced =
            referencedEnvironment(expression, measurements, execution);
        combined.insert(referenced.begin(), referenced.end());
        return combined;
    };
    for (const auto& measurement : simulation.measurements) {
        execution.check();
        if (!dependenciesAvailable(measurement.dependencies)) {
            result.skipped.push_back("measurement '" + measurement.name + "'");
            continue;
        }
        const auto& request =
            analysisByName(simulation, measurement.analysis, measurement.span);
        const auto found =
            std::find_if(result.analyses.begin(), result.analyses.end(),
                         [&](const AnalysisResult& analysis) {
                             return analysis.name == request.name;
                         });
        Query query = [&](const Expr& expression) {
            return queryResult(simulation, request, *found, expression,
                               execution);
        };
        auto value = evaluate(
            measurement.expression,
            environment(measurement.parameters, measurement.expression), query,
            execution, false, 1, simulation.limits.nesting);
        require(value.scalar(), measurement.span,
                "measurement did not reduce to scalar");
        result.measurements.push_back(
            {measurement.name,
             finite({value.data[0], value.dimension}, measurement.span)});
        measurements.emplace(measurement.name, std::move(value));
    }
    for (const auto& check : simulation.checks) {
        execution.check();
        if (!dependenciesAvailable(check.dependencies)) {
            result.skipped.push_back("check '" + check.name + "'");
            continue;
        }
        auto combined = environment(check.parameters, check.expression);
        auto left = evaluate(check.expression->arguments[0].value, combined, {},
                             execution, false, 1, simulation.limits.nesting);
        auto right =
            evaluate(check.expression->arguments[1].value, combined, {},
                     execution, false, 1, simulation.limits.nesting);
        const double first =
            finite({left.data[0], left.dimension}, check.span).value;
        const double second =
            finite({right.data[0], right.dimension}, check.span).value;
        const auto& operation = check.expression->text;
        bool passed = false;
        if (operation == "<")
            passed = first < second;
        else if (operation == "<=")
            passed = first <= second;
        else if (operation == ">")
            passed = first > second;
        else if (operation == ">=")
            passed = first >= second;
        else if (operation == "==")
            passed = first == second;
        else if (operation == "!=")
            passed = first != second;
        result.checks.push_back({check.name, passed});
    }
    execution.check();
    return result;
}

}  // namespace microspice::script
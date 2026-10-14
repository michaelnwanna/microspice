#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include "microspice/circuit.hpp"
#include "microspice/script/parser.hpp"

namespace microspice::script {

struct Value {
    Dimension dimension = dimensionless;
    std::vector<double> data{0};
    std::string axis;
    bool known = true;
    bool scalar() const { return axis.empty(); }
};
using Environment = std::map<std::string, Value>;
using Query = std::function<Value(const Expr&)>;

inline Environment referencedEnvironment(const Expr& expression,
                                         const Environment& available,
                                         const ExecutionControl& execution) {
    Environment result;
    std::function<void(const Expr&)> collect = [&](const Expr& current) {
        execution.check();
        if (current->kind == Expression::identifier) {
            const auto found = available.find(current->text);
            if (found != available.end()) result.insert(*found);
        }
        if (current->kind == Expression::call &&
            (current->text == "voltage" || current->text == "current"))
            return;
        for (const auto& argument : current->arguments) collect(argument.value);
    };
    collect(expression);
    return result;
}

inline void positional(const Expr& expression, std::size_t minimum,
                       std::size_t maximum) {
    require(expression->arguments.size() >= minimum &&
                expression->arguments.size() <= maximum,
            expression->span,
            "wrong argument count for '" + expression->text + "'");
    for (const auto& argument : expression->arguments)
        require(argument.name.empty(), argument.value->span,
                "unexpected named argument '" + argument.name + "'");
}

inline std::map<std::string, Expr> signature(
    const Expr& expression, std::size_t count,
    const std::set<std::string>& required,
    const std::set<std::string>& optional = {}) {
    require(expression->arguments.size() >= count, expression->span,
            "missing positional argument for '" + expression->text + "'");
    std::map<std::string, Expr> named;
    for (std::size_t index = 0; index < expression->arguments.size(); ++index) {
        const auto& argument = expression->arguments[index];
        if (index < count)
            require(argument.name.empty(), argument.value->span,
                    "expected positional argument");
        else {
            require(
                required.count(argument.name) || optional.count(argument.name),
                argument.value->span,
                "unexpected argument '" + argument.name + "' for '" +
                    expression->text + "'");
            require(named.emplace(argument.name, argument.value).second,
                    argument.value->span, "duplicate argument");
        }
    }
    for (const auto& name : required)
        require(named.count(name) != 0, expression->span,
                "missing argument '" + name + "'");
    return named;
}

inline std::string stringValue(const Expr& expression) {
    require(expression->kind == Expression::string && !expression->text.empty(),
            expression->span, "expected nonempty quoted name");
    return expression->text;
}
inline std::string identifierValue(const Expr& expression) {
    require(expression->kind == Expression::identifier, expression->span,
            "expected node or template identifier");
    return expression->text;
}

inline bool reserved(const std::string& name) {
    static const std::set<std::string> names{"ground",
                                             "param",
                                             "subcircuit",
                                             "use",
                                             "resistor",
                                             "capacitor",
                                             "voltage_source",
                                             "current_source",
                                             "diode",
                                             "dc",
                                             "transient",
                                             "initial",
                                             "sweep",
                                             "observe",
                                             "measure",
                                             "check",
                                             "voltage",
                                             "current",
                                             "abs",
                                             "final",
                                             "min",
                                             "max",
                                             "mean"};
    return names.count(name) != 0;
}

inline Value evaluate(const Expr& expression, const Environment& environment,
                      const Query& query, const ExecutionControl& execution,
                      bool typeOnly = false, std::size_t depth = 1,
                      std::size_t maxDepth = 32) {
    execution.check();
    require(depth <= maxDepth, expression->span,
            "expression nesting limit exceeded");
    auto child = [&](std::size_t index) {
        return evaluate(expression->arguments[index].value, environment, query,
                        execution, typeOnly, depth + 1, maxDepth);
    };
    if (expression->kind == Expression::quantity)
        return {expression->number.dimension, {expression->number.value}, ""};
    if (expression->kind == Expression::identifier) {
        auto found = environment.find(expression->text);
        require(found != environment.end(), expression->span,
                "undefined quantity '" + expression->text + "'");
        return found->second;
    }
    if (expression->kind == Expression::unary) {
        Value value = child(0);
        if (expression->text == "-")
            for (double& sample : value.data) {
                execution.check();
                sample = -sample;
            }
        return value;
    }
    if (expression->kind == Expression::binary) {
        Value left = child(0), right = child(1);
        require(expression->text == "+" || expression->text == "-" ||
                    expression->text == "*" || expression->text == "/",
                expression->span,
                "comparison is only allowed as the outer expression of check");
        require(left.scalar() || right.scalar() ||
                    (left.axis == right.axis &&
                     left.data.size() == right.data.size()),
                expression->span,
                "cannot mix series with different analysis axes");
        Value result;
        result.known = left.known && right.known;
        result.axis = left.scalar() ? right.axis : left.axis;
        result.data.clear();
        const auto size = std::max(left.data.size(), right.data.size());
        for (std::size_t index = 0; index < size; ++index) {
            execution.check();
            if (expression->text == "/" && right.known)
                require(right.data[right.scalar() ? 0 : index] != 0,
                        expression->span, "division by zero");
            Quantity first{typeOnly && !result.known
                               ? 1
                               : left.data[left.scalar() ? 0 : index],
                           left.dimension};
            Quantity second{typeOnly && !result.known
                                ? 1
                                : right.data[right.scalar() ? 0 : index],
                            right.dimension};
            auto quantity =
                arithmetic(expression->text, first, second, expression->span);
            result.dimension = quantity.dimension;
            result.data.push_back(quantity.value);
        }
        return result;
    }
    require(expression->kind == Expression::call, expression->span,
            "expected numeric expression");
    if (expression->text == "voltage" || expression->text == "current") {
        require(static_cast<bool>(query), expression->span,
                "observations are not allowed in parameter expressions");
        return query(expression);
    }
    require(expression->text == "abs" || expression->text == "final" ||
                expression->text == "min" || expression->text == "max" ||
                expression->text == "mean",
            expression->span,
            "unknown expression function '" + expression->text + "'");
    positional(expression, 1, 1);
    Value value = child(0);
    if (expression->text == "abs") {
        for (double& sample : value.data) {
            execution.check();
            sample = std::abs(sample);
        }
        return value;
    }
    require(!value.scalar(), expression->span,
            "series reduction requires a transient or sweep series");
    double reduced = value.data.back();
    if (expression->text == "min" || expression->text == "max") {
        reduced = value.data.front();
        for (double sample : value.data) {
            execution.check();
            reduced = expression->text == "min" ? std::min(reduced, sample)
                                                : std::max(reduced, sample);
        }
    } else if (expression->text == "mean") {
        reduced = 0;
        for (double sample : value.data) {
            execution.check();
            reduced += sample / static_cast<double>(value.data.size());
            finite({reduced, value.dimension}, expression->span);
        }
    }
    return {value.dimension, {reduced}, "", value.known};
}

enum class AnalysisKind { dc, transient, sweep };
struct AnalysisRequest {
    std::string name;
    AnalysisKind kind = AnalysisKind::dc;
    Span span;
    double step = 0, stop = 0, start = 0;
    std::string source;
    std::vector<double> initialVoltages;
    std::size_t samples = 1;
};
struct ObservationRequest {
    std::string analysis;
    Expr expression;
    Span span;
};
struct MeasurementRequest {
    std::string name, analysis;
    Expr expression;
    Environment parameters;
    std::set<std::string> dependencies;
    Span span;
};
struct CheckRequest {
    std::string name;
    Expr expression;
    Environment parameters;
    std::set<std::string> dependencies;
    Span span;
};
struct CompiledSimulation {
    Circuit circuit;
    Limits limits;
    std::map<std::string, std::string> nodes{{"ground", "0"}};
    std::map<std::string, Span> nodeSpans;
    std::map<std::string, Span> componentSpans;
    std::map<std::string, Span> instanceSpans;
    std::vector<AnalysisRequest> analyses;
    std::vector<ObservationRequest> observations;
    std::vector<MeasurementRequest> measurements;
    std::vector<CheckRequest> checks;
    std::vector<Diagnostic> warnings;
    std::size_t retainedScalarEstimate = 0;
};

inline const AnalysisRequest& analysisByName(
    const CompiledSimulation& simulation, const std::string& name, Span span) {
    for (const auto& analysis : simulation.analyses)
        if (analysis.name == name) return analysis;
    throw Error(span, "unknown analysis '" + name + "'");
}

inline std::string observationNode(const CompiledSimulation& simulation,
                                   const Expr& expression) {
    require(expression->kind == Expression::identifier ||
                expression->kind == Expression::string,
            expression->span,
            "expected node identifier or quoted hierarchical node name");
    auto found = simulation.nodes.find(expression->text);
    require(found != simulation.nodes.end(), expression->span,
            "unknown observation node '" + expression->text +
                "'; observations never create nodes");
    return found->second;
}

inline void validateQuery(const CompiledSimulation& simulation,
                          const Expr& expression) {
    if (expression->text == "voltage") {
        positional(expression, 1, 2);
        for (const auto& argument : expression->arguments)
            observationNode(simulation, argument.value);
    } else {
        positional(expression, 1, 1);
        std::string name = stringValue(expression->arguments[0].value);
        require(simulation.componentSpans.count(name) != 0, expression->span,
                "unknown component '" + name + "'");
        bool supported = false;
        for (const auto& component : simulation.circuit.resistors())
            supported |= component.name == name;
        for (const auto& component : simulation.circuit.voltageSources())
            supported |= component.name == name;
        for (const auto& component : simulation.circuit.currentSources())
            supported |= component.name == name;
        require(supported, expression->span,
                "current queries for capacitors and diodes are not supported");
    }
}

class Compiler {
    CompiledSimulation result_;
    ExecutionControl execution_;
    std::map<std::string, Statement> templates_;
    std::set<std::string> names_;
    std::set<std::string> globalParameterNames_;
    std::map<std::string, std::size_t> terminalCounts_;
    std::map<std::string, std::set<std::string>> adjacency_;
    struct Pending {
        Expr expression;
        Environment parameters;
    };
    std::vector<Pending> pending_;
    std::size_t expressionSlots_ = 0;

    Quantity quantity(const Expr& expression,
                      const Environment& environment) const {
        auto value = evaluate(expression, environment, {}, execution_, false, 1,
                              result_.limits.nesting);
        require(value.scalar(), expression->span, "expected scalar parameter");
        return {value.data[0], value.dimension};
    }
    double value(const Expr& expression, const Environment& environment,
                 Dimension dimension) const {
        return inUnits(quantity(expression, environment), dimension,
                       expression->span);
    }
    void parameter(const Statement& statement, Environment& environment) {
        require(!reserved(statement.name) && !templates_.count(statement.name),
                statement.span,
                "reserved parameter name '" + statement.name + "'");
        require(!environment.count(statement.name), statement.span,
                "duplicate immutable parameter '" + statement.name + "'");
        auto number = quantity(statement.value, environment);
        environment.emplace(statement.name,
                            Value{number.dimension, {number.value}, ""});
    }
    std::string node(const Expr& expression,
                     const std::map<std::string, std::string>& ports,
                     const std::string& prefix) {
        std::string name = identifierValue(expression);
        if (name == "ground") return "0";
        auto port = ports.find(name);
        const std::string logical =
            port != ports.end() ? port->second : prefix + name;
        if (logical == "ground") return "0";
        auto inserted = result_.nodes.emplace(logical, "script:" + logical);
        result_.nodeSpans.emplace(logical, expression->span);
        require(result_.nodes.size() - 1 <= result_.limits.unknowns,
                expression->span, "node/unknown limit exceeded");
        return inserted.first->second;
    }
    std::string logicalNode(const Expr& expression,
                            const std::map<std::string, std::string>& ports,
                            const std::string& prefix) {
        std::string name = identifierValue(expression);
        if (name == "ground") return name;
        auto found = ports.find(name);
        return found == ports.end() ? prefix + name : found->second;
    }
    std::string componentName(const Expr& expression,
                              const std::string& prefix) {
        auto name = stringValue(expression);
        require(name.find('.') == std::string::npos, expression->span,
                "component and instance names cannot contain '.'");
        name = prefix + name;
        require(names_.insert(name).second, expression->span,
                "duplicate component or instance name '" + name + "'");
        return name;
    }
    static bool device(const std::string& name) {
        return name == "resistor" || name == "capacitor" ||
               name == "voltage_source" || name == "current_source" ||
               name == "diode";
    }
    void expand(const Expr& call, const Environment& environment,
                const std::map<std::string, std::string>& ports,
                const std::string& prefix, std::size_t depth) {
        execution_.check();
        require(depth <= result_.limits.nesting, call->span,
                "subcircuit expansion nesting limit exceeded");
        if (call->text == "use") {
            require(call->arguments.size() >= 2, call->span,
                    "use requires instance name and template");
            const auto templateName = identifierValue(call->arguments[1].value);
            auto found = templates_.find(templateName);
            require(found != templates_.end(), call->span,
                    "unknown subcircuit '" + templateName + "'");
            const Statement& definition = found->second;
            std::set<std::string> required, optional;
            for (const auto& formal : definition.formals)
                (formal.value ? optional : required).insert(formal.name);
            auto arguments = signature(call, 2, required, optional);
            auto instance = componentName(call->arguments[0].value, prefix);
            result_.instanceSpans.emplace(instance, call->span);
            require(result_.instanceSpans.size() <= result_.limits.components,
                    call->span, "instance limit exceeded");
            Environment local;
            std::map<std::string, std::string> bindings;
            for (const auto& formal : definition.formals) {
                if (!formal.value)
                    bindings.emplace(
                        formal.name,
                        logicalNode(arguments.at(formal.name), ports, prefix));
                else {
                    Quantity number = quantity(formal.value, local);
                    auto overrideValue = arguments.find(formal.name);
                    if (overrideValue != arguments.end()) {
                        auto supplied =
                            quantity(overrideValue->second, environment);
                        inUnits(supplied, number.dimension,
                                overrideValue->second->span);
                        number = supplied;
                    }
                    local.emplace(formal.name,
                                  Value{number.dimension, {number.value}, ""});
                }
            }
            for (const auto& statement : definition.body) {
                execution_.check();
                if (statement.kind == Statement::parameter)
                    parameter(statement, local);
                else
                    expand(statement.value, local, bindings, instance + ".",
                           depth + 1);
            }
            return;
        }
        require(
            device(call->text), call->span,
            "only components, use and local param are allowed in subcircuits");
        std::map<std::string, Expr> named;
        if (call->text == "diode")
            named = signature(call, 3, {"saturation", "ideality"});
        else
            positional(call, 4, 4);
        auto name = componentName(call->arguments[0].value, prefix);
        require(result_.componentSpans.size() < result_.limits.components,
                call->span, "component limit exceeded");
        auto positive = node(call->arguments[1].value, ports, prefix);
        auto negative = node(call->arguments[2].value, ports, prefix);
        ++terminalCounts_[positive];
        ++terminalCounts_[negative];
        adjacency_[positive].insert(negative);
        adjacency_[negative].insert(positive);
        try {
            if (call->text == "resistor")
                result_.circuit.addResistor(
                    name, positive, negative,
                    value(call->arguments[3].value, environment, ohms));
            else if (call->text == "capacitor")
                result_.circuit.addCapacitor(
                    name, positive, negative,
                    value(call->arguments[3].value, environment, farads));
            else if (call->text == "voltage_source")
                result_.circuit.addVoltageSource(
                    name, positive, negative,
                    value(call->arguments[3].value, environment, volts));
            else if (call->text == "current_source")
                result_.circuit.addCurrentSource(
                    name, positive, negative,
                    value(call->arguments[3].value, environment, amps));
            else
                result_.circuit.addDiode(
                    name, positive, negative,
                    value(named.at("saturation"), environment, amps),
                    value(named.at("ideality"), environment, dimensionless));
        } catch (const Error&) {
            throw;
        } catch (const std::exception& error) {
            throw Error(call->span, error.what());
        }
        result_.componentSpans.emplace(name, call->span);
        require(result_.circuit.nodes().size() +
                        result_.circuit.voltageSources().size() <=
                    result_.limits.unknowns,
                call->span, "MNA unknown limit exceeded");
    }
    void visitTemplate(const std::string& name, std::set<std::string>& visiting,
                       std::set<std::string>& visited, std::size_t depth) {
        execution_.check();
        const auto& definition = templates_.at(name);
        require(depth <= result_.limits.nesting, definition.span,
                "template nesting limit exceeded");
        require(!visiting.count(name), definition.span,
                "recursive subcircuit dependency '" + name + "'");
        if (visited.count(name)) return;
        visiting.insert(name);
        std::set<std::string> formals;
        bool defaults = false;
        for (const auto& formal : definition.formals) {
            require(
                !reserved(formal.name) && formals.insert(formal.name).second,
                definition.span,
                "duplicate or reserved subcircuit formal '" + formal.name +
                    "'");
            if (formal.value)
                defaults = true;
            else
                require(!defaults, definition.span,
                        "ports must precede parameter defaults");
        }
        for (const auto& statement : definition.body) {
            if (statement.kind == Statement::parameter) continue;
            const auto& call = statement.value;
            require(device(call->text) || call->text == "use", call->span,
                    "only components, use and local param are allowed in "
                    "subcircuits");
            if (call->text == "use") {
                require(call->arguments.size() >= 2, call->span,
                        "use requires instance name and template");
                auto child = identifierValue(call->arguments[1].value);
                require(templates_.count(child) != 0, call->span,
                        "unknown subcircuit '" + child + "'");
                visitTemplate(child, visiting, visited, depth + 1);
            }
        }
        visiting.erase(name);
        visited.insert(name);
    }
    void analysis(const Expr& call, const Environment& environment) {
        std::map<std::string, Expr> named;
        if (call->text == "dc")
            positional(call, 1, 1);
        else if (call->text == "transient")
            named = signature(call, 1, {"step", "stop"});
        else
            named = signature(call, 1, {"source", "start", "stop", "step"});
        AnalysisRequest request;
        request.name = stringValue(call->arguments[0].value);
        request.span = call->span;
        for (const auto& existing : result_.analyses)
            require(existing.name != request.name, call->span,
                    "duplicate analysis '" + request.name + "'");
        require(result_.analyses.size() < result_.limits.analyses, call->span,
                "analysis limit exceeded");
        if (call->text != "dc") {
            const auto dimension = call->text == "transient" ? seconds : volts;
            request.step = value(named.at("step"), environment, dimension);
            request.stop = value(named.at("stop"), environment, dimension);
            double count = 0;
            if (call->text == "transient") {
                request.kind = AnalysisKind::transient;
                require(request.step > 0 && request.stop >= request.step,
                        call->span,
                        "transient requires positive step and stop >= step");
                count = std::floor(request.stop / request.step + 1e-12);
            } else {
                request.kind = AnalysisKind::sweep;
                request.start = value(named.at("start"), environment, volts);
                request.source = stringValue(named.at("source"));
                const double span = request.stop - request.start;
                require(std::isfinite(span) && request.step != 0 &&
                            (span == 0 || span / request.step > 0),
                        call->span,
                        "sweep step must be nonzero and point toward stop");
                count =
                    span == 0 ? 1 : std::floor(span / request.step + 1e-12) + 1;
            }
            require(std::isfinite(count) && count >= 1 &&
                        count <= static_cast<double>(result_.limits.samples) &&
                        count <= 1'000'000,
                    call->span, "analysis sample limit exceeded");
            request.samples = static_cast<std::size_t>(count);
        }
        result_.analyses.push_back(std::move(request));
    }
    void countExpressions(const Expr& expression) {
        execution_.check();
        ++expressionSlots_;
        for (const auto& argument : expression->arguments)
            countExpressions(argument.value);
    }
    void dependencies(
        const Expr& expression,
        const std::map<std::string, std::set<std::string>>& measurements,
        std::set<std::string>& result) {
        execution_.check();
        if (expression->kind == Expression::identifier) {
            auto found = measurements.find(expression->text);
            if (found != measurements.end())
                result.insert(found->second.begin(), found->second.end());
        }
        if (expression->kind == Expression::call &&
            (expression->text == "voltage" || expression->text == "current"))
            return;
        for (const auto& argument : expression->arguments)
            dependencies(argument.value, measurements, result);
    }
    void finish() {
        std::map<std::string, std::set<std::string>> measurementDependencies;
        Environment measurements;
        std::set<std::pair<std::string, std::string>> initialNames;
        std::set<std::string> checkNames;
        for (auto& request : result_.analyses) {
            request.initialVoltages.resize(result_.circuit.capacitors().size(),
                                           0);
            if (request.kind == AnalysisKind::transient)
                require(result_.circuit.diodes().empty(), request.span,
                        "transient analysis does not support nonlinear diodes");
            if (request.kind == AnalysisKind::sweep) {
                bool found = false;
                for (const auto& source : result_.circuit.voltageSources())
                    found |= source.name == request.source;
                require(found, request.span,
                        "sweep requires an existing voltage source '" +
                            request.source + "'");
            }
        }
        for (const auto& pending : pending_) {
            execution_.check();
            const auto& call = pending.expression;
            countExpressions(call);
            Environment environment = pending.parameters;
            for (const auto& measurement :
                 referencedEnvironment(call, measurements, execution_)) {
                require(!environment.count(measurement.first), call->span,
                        "measurement conflicts with parameter '" +
                            measurement.first + "'");
                environment.insert(measurement);
            }
            if (call->text == "initial") {
                positional(call, 3, 3);
                auto analysisName = stringValue(call->arguments[0].value);
                auto capacitorName = stringValue(call->arguments[1].value);
                const auto& request =
                    analysisByName(result_, analysisName, call->span);
                require(request.kind == AnalysisKind::transient, call->span,
                        "initial requires a transient analysis");
                require(
                    initialNames.emplace(analysisName, capacitorName).second,
                    call->span, "duplicate initial capacitor override");
                bool found = false;
                for (std::size_t index = 0;
                     index < result_.circuit.capacitors().size(); ++index) {
                    if (result_.circuit.capacitors()[index].name !=
                        capacitorName)
                        continue;
                    found = true;
                    for (auto& analysis : result_.analyses)
                        if (analysis.name == analysisName)
                            analysis.initialVoltages[index] =
                                value(call->arguments[2].value,
                                      pending.parameters, volts);
                }
                require(found, call->span,
                        "unknown capacitor '" + capacitorName + "'");
            } else if (call->text == "observe" || call->text == "measure") {
                std::string analysisName;
                Expr expression;
                if (call->text == "observe") {
                    positional(call, 2, call->arguments.size());
                    analysisName = stringValue(call->arguments[0].value);
                } else {
                    auto named = signature(call, 1, {"from", "value"});
                    analysisName = stringValue(named.at("from"));
                    expression = named.at("value");
                }
                const auto& request =
                    analysisByName(result_, analysisName, call->span);
                Query query = [&](const Expr& queryExpression) {
                    validateQuery(result_, queryExpression);
                    return Value{
                        queryExpression->text == "voltage" ? volts : amps,
                        {1},
                        request.kind == AnalysisKind::dc ? "" : analysisName,
                        false};
                };
                if (call->text == "observe") {
                    for (std::size_t index = 1; index < call->arguments.size();
                         ++index) {
                        auto observed = call->arguments[index].value;
                        require(
                            observed->kind == Expression::call &&
                                (observed->text == "voltage" ||
                                 observed->text == "current"),
                            observed->span,
                            "observe requires voltage(...) or current(...)");
                        query(observed);
                        result_.observations.push_back(
                            {analysisName, observed, observed->span});
                    }
                } else {
                    auto name = stringValue(call->arguments[0].value);
                    require(
                        identifierStart(name[0]) &&
                            std::all_of(name.begin(), name.end(),
                                        [](char character) {
                                            return identifierStart(character) ||
                                                   digit(character);
                                        }) &&
                            !reserved(name),
                        call->span,
                        "measurement name must be a nonreserved identifier");
                    require(
                        !measurements.count(name) &&
                            !globalParameterNames_.count(name) &&
                            !templates_.count(name),
                        call->span,
                        "duplicate or conflicting measurement '" + name + "'");
                    auto type =
                        evaluate(expression, environment, query, execution_,
                                 true, 1, result_.limits.nesting);
                    require(type.scalar(), expression->span,
                            "measurement must be scalar; use final, min, max "
                            "or mean to reduce a series");
                    std::set<std::string> deps{analysisName};
                    dependencies(expression, measurementDependencies, deps);
                    result_.measurements.push_back(
                        {name, analysisName, expression, pending.parameters,
                         deps, call->span});
                    measurementDependencies.emplace(name, deps);
                    measurements.emplace(name, type);
                }
            } else if (call->text == "check") {
                positional(call, 2, 2);
                auto name = stringValue(call->arguments[0].value);
                require(checkNames.insert(name).second, call->span,
                        "duplicate check label '" + name + "'");
                auto expression = call->arguments[1].value;
                require(
                    expression->kind == Expression::binary &&
                        (expression->text == "<" || expression->text == "<=" ||
                         expression->text == ">" || expression->text == ">=" ||
                         expression->text == "==" || expression->text == "!="),
                    expression->span, "check requires a comparison");
                auto left =
                    evaluate(expression->arguments[0].value, environment, {},
                             execution_, true, 1, result_.limits.nesting);
                auto right =
                    evaluate(expression->arguments[1].value, environment, {},
                             execution_, true, 1, result_.limits.nesting);
                require(left.scalar() && right.scalar() &&
                            left.dimension == right.dimension,
                        expression->span,
                        "check requires scalar quantities with compatible "
                        "dimensions");
                std::set<std::string> deps;
                dependencies(expression, measurementDependencies, deps);
                result_.checks.push_back(
                    {name, expression, pending.parameters, deps, call->span});
            } else
                throw Error(call->span,
                            "unknown statement function '" + call->text + "'");
        }
        const std::size_t unknowns = result_.circuit.nodes().size() +
                                     result_.circuit.voltageSources().size();
        std::size_t samples = 0;
        for (const auto& request : result_.analyses) {
            require(request.samples <= result_.limits.samples - samples,
                    request.span, "total analysis sample limit exceeded");
            samples += request.samples;
        }
        const long double perSample =
            static_cast<long double>(unknowns) * unknowns +
            2 * static_cast<long double>(unknowns) +
            4 * static_cast<long double>(result_.circuit.capacitors().size()) +
            500 * static_cast<long double>(result_.circuit.diodes().size()) +
            (result_.circuit.diodes().empty() ? 0 : 300) +
            static_cast<long double>(expressionSlots_) +
            static_cast<long double>(result_.observations.size() +
                                     result_.measurements.size()) +
            2;
        long double initialScalars = 0;
        for (const auto& request : result_.analyses)
            initialScalars +=
                static_cast<long double>(request.initialVoltages.size());
        const long double retained =
            perSample * static_cast<long double>(samples) + initialScalars;
        require(retained <=
                    static_cast<long double>(result_.limits.retainedScalars),
                {}, "retained scalar slot limit exceeded");
        result_.retainedScalarEstimate = static_cast<std::size_t>(retained);
        std::set<std::string> reachable{"0"};
        std::vector<std::string> queue{"0"};
        for (std::size_t index = 0; index < queue.size(); ++index) {
            execution_.check();
            for (const auto& neighbor : adjacency_[queue[index]])
                if (reachable.insert(neighbor).second)
                    queue.push_back(neighbor);
        }
        for (const auto& entry : result_.nodes) {
            if (entry.first == "ground") continue;
            if (terminalCounts_[entry.second] == 1) {
                std::string suggestion;
                for (const auto& other : result_.nodes) {
                    execution_.check();
                    if (other.first == entry.first) continue;
                    const auto& first = entry.first;
                    const auto& second = other.first;
                    if (first.size() > second.size() + 1 ||
                        second.size() > first.size() + 1)
                        continue;
                    std::size_t firstIndex = 0, secondIndex = 0,
                                differences = 0;
                    while (firstIndex < first.size() &&
                           secondIndex < second.size()) {
                        if (first[firstIndex] == second[secondIndex]) {
                            ++firstIndex;
                            ++secondIndex;
                        } else {
                            ++differences;
                            if (first.size() >= second.size()) ++firstIndex;
                            if (second.size() >= first.size()) ++secondIndex;
                        }
                    }
                    differences +=
                        first.size() - firstIndex + second.size() - secondIndex;
                    if (differences == 1) {
                        suggestion = "; similar node: '" + second + "'";
                        break;
                    }
                }
                result_.warnings.push_back(
                    {result_.nodeSpans.at(entry.first),
                     "node '" + entry.first +
                         "' has only one terminal; check its spelling" +
                         suggestion,
                     true});
            }
            if (!reachable.count(entry.second))
                result_.warnings.push_back(
                    {result_.nodeSpans.at(entry.first),
                     "node '" + entry.first +
                         "' has no topological path to ground; the solver may "
                         "be singular",
                     true});
        }
    }

public:
    Compiler(Limits limits = {}, ExecutionControl execution = {})
        : execution_(std::move(execution)) {
        result_.limits = limits;
    }
    CompiledSimulation compile(const std::string& source) {
        const auto limits = result_.limits;
        result_ = CompiledSimulation{};
        result_.limits = limits;
        templates_.clear();
        names_.clear();
        globalParameterNames_.clear();
        terminalCounts_.clear();
        adjacency_.clear();
        pending_.clear();
        expressionSlots_ = 0;
        auto statements = Parser(source, result_.limits, execution_).parse();
        for (const auto& statement : statements)
            if (statement.kind == Statement::parameter)
                globalParameterNames_.insert(statement.name);
        for (const auto& statement : statements)
            if (statement.kind == Statement::subcircuit) {
                require(
                    !reserved(statement.name) &&
                        templates_.emplace(statement.name, statement).second,
                    statement.span, "duplicate or reserved subcircuit name");
            }
        std::set<std::string> visiting, visited;
        for (const auto& definition : templates_)
            visitTemplate(definition.first, visiting, visited, 1);
        Compiler validation(result_.limits, execution_);
        validation.templates_ = templates_;
        for (const auto& definition : templates_) {
            execution_.check();
            validation.result_ = CompiledSimulation{};
            validation.result_.limits = result_.limits;
            validation.names_.clear();
            validation.terminalCounts_.clear();
            validation.adjacency_.clear();
            auto call = std::make_shared<Expression>();
            call->kind = Expression::call;
            call->text = "use";
            call->span = definition.second.span;
            auto instance = std::make_shared<Expression>();
            instance->kind = Expression::string;
            instance->text = "validation";
            instance->span = call->span;
            auto name = std::make_shared<Expression>();
            name->kind = Expression::identifier;
            name->text = definition.first;
            name->span = call->span;
            call->arguments = {{"", instance}, {"", name}};
            for (const auto& formal : definition.second.formals)
                if (!formal.value) {
                    auto port = std::make_shared<Expression>();
                    port->kind = Expression::identifier;
                    port->text = formal.name;
                    port->span = call->span;
                    call->arguments.push_back({formal.name, port});
                }
            validation.expand(call, {}, {}, "", 1);
        }
        Environment environment;
        for (const auto& statement : statements) {
            execution_.check();
            if (statement.kind == Statement::subcircuit) continue;
            if (statement.kind == Statement::parameter)
                parameter(statement, environment);
            else {
                const auto& call = statement.value;
                if (device(call->text) || call->text == "use")
                    expand(call, environment, {}, "", 1);
                else if (call->text == "dc" || call->text == "transient" ||
                         call->text == "sweep")
                    analysis(call, environment);
                else
                    pending_.push_back(
                        {call,
                         referencedEnvironment(call, environment, execution_)});
            }
        }
        finish();
        execution_.check();
        return std::move(result_);
    }
};

inline CompiledSimulation compile(const std::string& source, Limits limits = {},
                                  ExecutionControl execution = {}) {
    return Compiler(limits, std::move(execution)).compile(source);
}

}  // namespace microspice::script
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#include "microspice/script/output.hpp"

namespace {
using namespace microspice;
using namespace microspice::script;
std::size_t assertions = 0;

void expect(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance = 1e-10) {
    expect(std::abs(actual - expected) <= tolerance,
           "expected " + std::to_string(expected) + ", got " +
               std::to_string(actual));
}
void invalid(const std::string& source, const std::string& message,
             Limits limits = {}) {
    try {
        compile(source, limits);
    } catch (const Error& error) {
        expect(std::string(error.what()).find(message) != std::string::npos,
               "wrong diagnostic: " + std::string(error.what()) +
                   "; expected " + message);
        return;
    }
    throw std::runtime_error("expected compile failure: " + source);
}

const std::string divider =
    "param R = 1kohm\n"
    "voltage_source(\"V1\", supply, ground, 10V)\n"
    "resistor(\"R1\", supply, output, R)\n"
    "resistor(\"R2\", output, ground, R)\n"
    "dc(\"bias\")\n"
    "observe(\"bias\", voltage(output), voltage(supply, output), "
    "current(\"V1\"), current(\"R1\"))\n"
    "measure(\"midpoint\", from=\"bias\", value=voltage(output))\n"
    "check(\"divider\", abs(midpoint-5V)<=1mV)\n";

const std::string rc =
    "voltage_source(\"V1\", supply, ground, 10V)\n"
    "resistor(\"R1\", supply, output, 1kohm)\n"
    "capacitor(\"C1\", output, ground, 1uF)\n";

const std::string hierarchy =
    "subcircuit cell(input, output, return_node, R=1kohm) {\n"
    " param half = R / 2\n"
    " resistor(\"R1\", input, internal, half)\n"
    " resistor(\"R2\", internal, output, half)\n"
    " resistor(\"R3\", output, return_node, R)\n"
    "}\n"
    "subcircuit wrapper(input, output, return_node, R=1kohm) {\n"
    " use(\"child\", cell, input=input, output=output, "
    "return_node=return_node, R=R)\n"
    "}\n"
    "voltage_source(\"V1\", supply, ground, 10V)\n"
    "use(\"A\", wrapper, input=supply, output=first, return_node=ground)\n"
    "use(\"B\", wrapper, input=supply, output=second, return_node=ground, "
    "R=2kohm)\n"
    "dc(\"bias\")\n"
    "observe(\"bias\", voltage(\"A.child.internal\"), "
    "voltage(\"B.child.internal\"), voltage(first), current(\"B.child.R1\"))\n";

std::string example(const std::string& name) {
    std::ifstream input(std::string(MS_SCRIPT_EXAMPLES) + "/" + name + ".msim");
    expect(static_cast<bool>(input), "cannot open example " + name);
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

void compilerTests() {
    const auto parsed = Parser(
                            "# comment\nparam R = 1 kohm\nresistor(\n \"R1\", "
                            "supply, ground, R\n)\n")
                            .parse();
    expect(parsed.size() == 2, "multiline parse");
    near(parsed[0].value->number.value, 1000);
    expect(parsed[0].span.line == 2 && parsed[0].span.column == 1,
           "statement source span");
    const auto quantities = compile(
        "param small=1kohm\nparam large=1Mohm\nparam cap=1uF\nparam "
        "tau=small*cap\nparam x=abs(-2V)+3V*2\nparam science=1.5e-3 A\nparam "
        "fraction=-(2+3)/5\n");
    expect(quantities.analyses.empty(), "compile-only circuit");
    near(literal(1, "Mohm", {}).value, 1e6);
    near(literal(1, "mV", {}).value, 1e-3);
    near(literal(1, "MV", {}).value, 1e6);
    near(literal(1, "us", {}).value, 1e-6);
    near(literal(1, "ms", {}).value, 1e-3);
    expect(
        arithmetic("*", {1000, ohms}, {1e-6, farads}, {}).dimension == seconds,
        "RC dimension");
}

void compilerFailures() {
    invalid("param R=1watts\n", "unknown unit");
    invalid("param R=1M\n", "unknown unit");
    invalid("param R=1e999V\n", "out of range");
    invalid("param R=1e-V\n", "exponent");
    invalid("param R=1e300V*1e300\n", "finite");
    invalid("param R=1V/0\n", "division by zero");
    invalid("param R=1V+1ohm\n", "incompatible dimensions");
    invalid("param R=later\nparam later=1ohm\n", "undefined quantity");
    invalid("param R=1ohm\nparam R=2ohm\n", "immutable");
    invalid("param ground=1\n", "reserved");
    invalid("param resistor=1\n", "reserved");
    invalid("resistor(\"R\", n, ground, 1)\n", "expected ohm");
    invalid("resistor(\"R\", n, ground, 1V)\n", "expected ohm");
    invalid("resistor(\"R\", n, ground, -1ohm)\n", "greater than zero");
    invalid("capacitor(\"C\", n, ground, 0F)\n", "greater than zero");
    invalid("diode(\"D\", n, ground, saturation=-1pA, ideality=1)\n",
            "positive");
    invalid("diode(\"D\", n, ground, saturation=1pA, ideality=1V)\n",
            "expected 1");
    invalid("resistor(\"R\", n, ground, 1ohm, 2)\n", "argument count");
    invalid("resistor(\"R\", n, ground, value=1ohm)\n", "named argument");
    invalid("dc(\"bias\", surprise=1)\n", "argument count");
    invalid("transient(\"t\", stop=1s)\n", "missing argument 'step'");
    invalid("transient(\"t\", step=1ms, stop=1s, nope=1)\n",
            "unexpected argument");
    invalid("transient(\"t\", step=1ms, step=2ms, stop=1s)\n",
            "duplicate named");
    invalid("dc(name=\"bias\", \"other\")\n",
            "positional argument after named");
    invalid("include(\"other.msim\")\n", "unknown statement");
    invalid("eval(\"1+1\")\n", "unknown statement");
    invalid("resistor(\"R.x\", n, ground, 1ohm)\n", "cannot contain");
    invalid(
        "resistor(\"R\", n, ground, 1ohm)\nresistor(\"R\", n, ground, 1ohm)\n",
        "duplicate component");
    invalid("voltage_source(\"V\", \"n\", ground, 1V)\n", "identifier");
    invalid("dc(\"b\")\ndc(\"b\")\n", "duplicate analysis");
    invalid("dc(\"b\")\nobserve(\"b\", voltage(typo))\n",
            "unknown observation node");
    invalid(divider + "observe(\"missing\", voltage(output))\n",
            "unknown analysis");
    invalid(divider + "observe(\"bias\", current(\"missing\"))\n",
            "unknown component");
    invalid(rc + "dc(\"b\")\nobserve(\"b\", current(\"C1\"))\n",
            "not supported");
    invalid(
        "diode(\"D\", n, ground, saturation=1pA, "
        "ideality=1)\ndc(\"b\")\nobserve(\"b\", current(\"D\"))\n",
        "not supported");
    invalid(divider + "observe(\"bias\", voltage(output)+1V)\n",
            "observe requires");
    invalid(divider + "measure(\"later\", from=\"bias\", value=missing)\n",
            "undefined quantity");
    invalid(divider +
                "measure(\"bad\", from=\"bias\", value=voltage(output)+1ohm)\n",
            "incompatible dimensions");
    invalid(
        divider + "measure(\"bad\", from=\"bias\", value=voltage(output)/0)\n",
        "division by zero");
    invalid(divider + "measure(\"midpoint\", from=\"bias\", value=1V)\n",
            "duplicate");
    invalid(divider + "param midpoint=1V\n", "conflicting measurement");
    invalid(divider + "check(\"bad\", midpoint<=1ohm)\n",
            "compatible dimensions");
    invalid(divider + "check(\"bad\", midpoint+1V)\n", "comparison");
    invalid(divider + "check(\"bad\", voltage(output)==5V)\n", "not allowed");
    invalid(divider + "check(\"divider\", midpoint==5V)\n", "duplicate check");
    invalid(
        divider +
            "measure(\"bad\", from=\"bias\", value=final(voltage(output)))\n",
        "series reduction");
    invalid(rc + "transient(\"t\", step=100us, stop=1ms)\nmeasure(\"bad\", "
                 "from=\"t\", value=voltage(output))\n",
            "must be scalar");
    invalid(rc + "transient(\"t\", step=-1ms, stop=1s)\n", "positive step");
    invalid(rc + "transient(\"t\", step=2ms, stop=1ms)\n", "stop >= step");
    invalid(rc + "sweep(\"s\", source=\"V1\", start=0V, stop=5V, step=-1V)\n",
            "toward stop");
    invalid(rc + "sweep(\"s\", source=\"V1\", start=0V, stop=5V, step=0V)\n",
            "nonzero");
    invalid(rc + "sweep(\"s\", source=\"no\", start=0V, stop=5V, step=1V)\n",
            "existing voltage source");
    invalid(rc + "dc(\"b\")\ninitial(\"b\", \"C1\", 0V)\n",
            "transient analysis");
    invalid(rc + "transient(\"t\", step=100us, stop=1ms)\ninitial(\"t\", "
                 "\"no\", 0V)\n",
            "unknown capacitor");
    invalid(rc + "transient(\"t\", step=100us, stop=1ms)\ninitial(\"t\", "
                 "\"C1\", 0V)\ninitial(\"t\", \"C1\", 1V)\n",
            "duplicate initial");
    invalid(
        "diode(\"D\", n, ground, saturation=1pA, ideality=1)\ntransient(\"t\", "
        "step=1ms, stop=1ms)\n",
        "does not support");
    invalid("param x=\"unterminated\n", "unterminated string");
    invalid("dc(\"bad\\n\")\n", "escaped quote");
    invalid("param x=1;\n", "unexpected character");
    invalid("param x=1 param y=2\n", "unknown unit");
    invalid("}\n", "expected expression");
    invalid("param x=\n", "expected expression");
    try {
        compile("# line one\n  resistor(\"R\", n, ground, 1V)\n");
    } catch (const Error& error) {
        expect(error.diagnostic.span.line == 2 &&
                   error.diagnostic.span.column == 28,
               "quantity source location");
    }
}

void hierarchyTests() {
    auto simulation = compile(hierarchy);
    expect(simulation.circuit.resistors().size() == 6, "flattened components");
    expect(simulation.instanceSpans.size() == 4, "nested instance source maps");
    expect(simulation.nodes.at("A.child.internal") !=
               simulation.nodes.at("B.child.internal"),
           "isolated internal nodes");
    near(simulation.circuit.resistors()[0].resistanceOhms, 500);
    near(simulation.circuit.resistors()[3].resistanceOhms, 1000);
    const auto result = run(simulation);
    const auto& traces = result.analyses[0].observations;
    near(traces[0].value.data[0], 7.5);
    near(traces[1].value.data[0], 7.5);
    near(traces[2].value.data[0], 5);
    near(traces[3].value.data[0], 0.0025);
    invalid("subcircuit bad(p) {\n dc(\"b\")\n}\n", "only components");
    invalid("subcircuit bad(p) {\n resistor(\"R\", p, ground, 1V)\n}\n",
            "expected ohm");
    invalid(
        "subcircuit bad(p) {\n resistor(\"R\", p, ground, global)\n}\nparam "
        "global=1kohm\n",
        "undefined quantity");
    invalid("subcircuit a(p) {\n use(\"X\", a, p=p)\n}\n", "recursive");
    invalid(
        "subcircuit a(p) {\n use(\"X\", b, p=p)\n}\nsubcircuit b(p) {\n "
        "use(\"Y\", a, p=p)\n}\n",
        "recursive");
    invalid("use(\"X\", missing, p=ground)\n", "unknown subcircuit");
    invalid("subcircuit a(p,p) {}\n", "duplicate or reserved");
    invalid("subcircuit a(R=1ohm,p) {}\n", "ports must precede");
    invalid("subcircuit a(p) {}\nsubcircuit a(q) {}\n",
            "duplicate or reserved");
    invalid("subcircuit a(p) {}\nuse(\"A\", a)\n", "missing argument 'p'");
    invalid("subcircuit a(p) {}\nuse(\"A\", a, p=ground, extra=1)\n",
            "unexpected argument");
    invalid("subcircuit a(p,R=1ohm) {}\nuse(\"A\", a, p=ground, R=1V)\n",
            "expected ohm");
    invalid(
        "subcircuit a(p) {}\nuse(\"A\", a, p=ground)\nuse(\"A\", a, "
        "p=ground)\n",
        "duplicate component or instance");
    invalid("subcircuit a(p,R=1ohm) {\n param R=2ohm\n}\n", "immutable");
    invalid("subcircuit a(p) {\n subcircuit b(q) {}\n}\n", "nested subcircuit");
    invalid("param a=1\nsubcircuit a(p) {}\n", "reserved parameter");
    expect(compile("use(\"A\", later, p=out)\nsubcircuit later(p) {\n "
                   "resistor(\"R\", p, ground, 1kohm)\n}\n")
                   .circuit.resistors()
                   .size() == 1,
           "forward template definition");
}

void limitsAndCancellation() {
    expect(compile("#" + std::string(Limits{}.sourceBytes - 1, ' '))
               .analyses.empty(),
           "exact source byte boundary");
    invalid(std::string(Limits{}.sourceBytes + 1, ' '), "source byte");
    expect(
        compile("param value=" + std::string(31, '-') + "1\n").analyses.empty(),
        "exact expression depth boundary");
    invalid("param value=" + std::string(32, '-') + "1\n", "nesting");
    Limits limits;
    limits.sourceBytes = 5;
    invalid("param x=1\n", "source byte", limits);
    limits = Limits{};
    limits.nesting = 3;
    invalid("param x=((((1))))\n", "nesting", limits);
    invalid("param x=1+1+1+1+1\n", "nesting", limits);
    limits = Limits{};
    limits.components = 1;
    invalid(divider, "component limit", limits);
    limits = Limits{};
    limits.unknowns = 1;
    invalid(divider, "unknown limit", limits);
    limits = Limits{};
    limits.analyses = 1;
    invalid(divider + "dc(\"second\")\n", "analysis limit", limits);
    limits = Limits{};
    limits.samples = 9;
    invalid(rc + "transient(\"t\", step=100us, stop=1ms)\n", "sample limit",
            limits);
    limits = Limits{};
    limits.samples = 15;
    invalid(rc + "transient(\"a\", step=100us, stop=1ms)\ntransient(\"b\", "
                 "step=100us, stop=1ms)\n",
            "total analysis sample", limits);
    limits = Limits{};
    limits.retainedScalars = 1;
    invalid(divider, "scalar slot", limits);
    invalid(rc + "transient(\"t\", step=1e-300s, stop=1e300s)\n",
            "sample limit");
    invalid(rc + "sweep(\"s\", source=\"V1\", start=-1e308V, stop=1e308V, "
                 "step=1V)\n",
            "toward stop");
    limits = Limits{};
    limits.components = 5;
    invalid(hierarchy, "component limit", limits);
    limits = Limits{};
    limits.nesting = 2;
    invalid(
        "subcircuit a(p) {}\nsubcircuit b(p) {\n use(\"A\", a, "
        "p=p)\n}\nsubcircuit c(p) {\n use(\"B\", b, p=p)\n}\n",
        "nesting", limits);
    std::string chain = "param x=1";
    for (std::size_t index = 0; index < 10000; ++index) chain += "+1";
    invalid(chain, "nesting");
    std::size_t checkpoints = 0;
    try {
        compile(divider, {}, {[&] { return ++checkpoints > 10; }, {}});
        throw std::runtime_error("compile cancellation missing");
    } catch (const AnalysisCancelled&) {
        expect(checkpoints > 10, "compile checkpoint");
    }
    const auto simulation =
        compile(rc + "transient(\"t\", step=100us, stop=5ms)\n");
    checkpoints = 0;
    try {
        run(simulation, {[&] { return ++checkpoints > 20; }, {}});
        throw std::runtime_error("run cancellation missing");
    } catch (const AnalysisCancelled&) {
        expect(checkpoints > 20, "solver checkpoint");
    }
    try {
        run(compile(divider), {[] { return true; }, {}});
        throw std::runtime_error("entry cancellation missing");
    } catch (const AnalysisCancelled&) {
        ++assertions;
    }
    auto reduction =
        Parser("param value=mean(voltage(node))\n").parse()[0].value;
    checkpoints = 0;
    try {
        evaluate(reduction, {},
                 [](const Expr&) {
                     return Value{volts, std::vector<double>(100, 1), "test"};
                 },
                 {[&] { return ++checkpoints > 20; }, {}});
        throw std::runtime_error("reduction cancellation missing");
    } catch (const AnalysisCancelled&) {
        expect(checkpoints > 20, "measurement checkpoint");
    }
    expect(
        compile("resistor(\"R\", output, outpu, 1kohm)\n").warnings.size() >= 2,
        "topology warnings");
}

void malformedInputs() {
    std::mt19937 generator(8583);
    const std::string alphabet = "abc_0123\"\\\n#(),{}=+-*/<>! .";
    for (std::size_t iteration = 0; iteration < 1000; ++iteration) {
        std::string source;
        for (std::size_t index = 0; index < iteration % 128; ++index)
            source += alphabet[generator() % alphabet.size()];
        try {
            compile(source);
        } catch (const Error&) {
        }
        ++assertions;
    }
}

void runtimeTests() {
    const auto simulation = compile(divider);
    const auto result = run(simulation);
    expect(result.checksPassed(), "divider check");
    Compiler reusable;
    expect(reusable.compile(divider).measurements.size() == 1 &&
               reusable.compile(rc).measurements.empty(),
           "compiler reuse resets state");
    near(result.measurements[0].value.value, 5);
    const auto& traces = result.analyses[0].observations;
    near(traces[0].value.data[0], 5);
    near(traces[1].value.data[0], 5);
    near(traces[2].value.data[0], -0.005);
    near(traces[3].value.data[0], 0.005);
    auto aliases = run(compile(
        "voltage_source(\"V1\", gnd, ground, 1V)\nvoltage_source(\"V2\", GND, "
        "ground, 2V)\ndc(\"b\")\nobserve(\"b\", voltage(gnd), voltage(GND), "
        "voltage(ground))\n"));
    near(aliases.analyses[0].observations[0].value.data[0], 1);
    near(aliases.analyses[0].observations[1].value.data[0], 2);
    near(aliases.analyses[0].observations[2].value.data[0], 0);
    auto allChecks = run(compile(
        divider +
        "check(\"first failure\", midpoint<0V)\ncheck(\"second failure\", "
        "midpoint!=5V)\ncheck(\"later pass\", midpoint>=5V)\n"));
    expect(!allChecks.checksPassed() && allChecks.checks.size() == 4 &&
               allChecks.checks.back().passed,
           "collect all check results");
    auto transient = run(compile(
        rc +
        "dc(\"before\")\ntransient(\"t\", step=100us, "
        "stop=1.05ms)\ninitial(\"t\", \"C1\", 1V)\n"
        "observe(\"t\", voltage(output), current(\"V1\"))\n"
        "measure(\"last\", from=\"t\", value=final(voltage(output)+1V))\n"
        "measure(\"smallest\", from=\"t\", value=min(voltage(output)))\n"
        "measure(\"largest\", from=\"t\", value=max(voltage(output)))\n"
        "measure(\"average\", from=\"t\", value=mean(voltage(output)))\n"
        "measure(\"difference\", from=\"before\", value=voltage(output)-last)\n"
        "check(\"reduced\", last>5V)\n"));
    const auto& series = transient.analyses[1].observations[0].value;
    expect(series.data.size() == 10 && series.axis == "t", "series axis/count");
    near(transient.analyses[1].axis.back(), 0.001);
    near(series.data.front(), (10 + 10 * 1) / 11.0);
    near(series.data.back(), 10 - 9 * std::pow(10.0 / 11.0, 10));
    near(transient.measurements[0].value.value, series.data.back() + 1);
    near(transient.measurements[1].value.value, series.data.front());
    near(transient.measurements[2].value.value, series.data.back());
    double sum = 0;
    for (double value : series.data) sum += value;
    near(transient.measurements[3].value.value, sum / 10);
    auto separate = compile(
        rc +
        "sweep(\"curve\", source=\"V1\", start=5V, stop=0V, "
        "step=-2V)\nobserve(\"curve\", voltage(supply))\n"
        "dc(\"bias\")\nobserve(\"bias\", voltage(supply))\n"
        "measure(\"swept\", from=\"curve\", value=final(voltage(supply)))\n"
        "measure(\"fixed\", from=\"bias\", value=voltage(supply))\n"
        "check(\"base untouched\", fixed==10V)\ncheck(\"both\", "
        "fixed>swept)\n");
    auto sweep = run(separate);
    expect(sweep.analyses[0].axis.size() == 3, "descending nondivisible sweep");
    near(sweep.analyses[0].axis.back(), 1);
    near(sweep.measurements[0].value.value, 1);
    near(sweep.measurements[1].value.value, 10);
    near(separate.circuit.voltageSources()[0].voltageVolts, 10);
    auto selected = run(separate, {}, "bias");
    expect(selected.measurements.size() == 1 && selected.checks.size() == 1 &&
               selected.skipped.size() == 2,
           "selected dependencies skipped");
    try {
        run(separate, {}, "no");
        throw std::runtime_error("unknown selection accepted");
    } catch (const Error& error) {
        expect(std::string(error.what()).find("unknown analysis") !=
                   std::string::npos,
               "unknown selection");
    }
    try {
        run(compile("param R=1ohm\n"));
        throw std::runtime_error("empty run accepted");
    } catch (const Error& error) {
        expect(
            std::string(error.what()).find("no analyses") != std::string::npos,
            "empty run");
    }
    try {
        run(compile("resistor(\"R\", a, b, 1kohm)\ndc(\"bad\")\n"));
        throw std::runtime_error("singular accepted");
    } catch (const Error& error) {
        expect(error.diagnostic.span.line == 2, "solve failure analysis span");
    }
    const auto nonconvergent =
        "voltage_source(\"V1\", n, ground, 18V)\ndiode(\"D1\", n, ground, "
        "saturation=1e-300A, ideality=1)\n";
    for (const auto& request :
         {std::string("dc(\"bad\")\n"),
          std::string("sweep(\"bad\", source=\"V1\", start=18V, stop=18V, "
                      "step=1V)\n")}) {
        try {
            run(compile(nonconvergent + request));
            throw std::runtime_error("nonconvergence accepted");
        } catch (const Error& error) {
            expect(std::string(error.what()).find("did not converge") !=
                       std::string::npos,
                   "nonlinear failure propagation");
        }
    }
    auto initialOrder = run(compile(
        "voltage_source(\"V\", supply, ground, 10V)\nresistor(\"R1\", supply, "
        "a, 1kohm)\nresistor(\"R2\", supply, b, 1kohm)\ncapacitor(\"C2\", b, "
        "ground, 1uF)\ncapacitor(\"C1\", a, ground, 1uF)\ntransient(\"t\", "
        "step=100us, stop=100us)\ninitial(\"t\", \"C1\", 1V)\ninitial(\"t\", "
        "\"C2\", 2V)\nobserve(\"t\", voltage(a), voltage(b))\n"));
    near(initialOrder.analyses[0].observations[0].value.data[0], 20.0 / 11);
    near(initialOrder.analyses[0].observations[1].value.data[0], 30.0 / 11);
    Environment empty;
    Query cross = [](const Expr& expression) {
        return Value{volts, {1, 2}, expression->arguments[0].value->text};
    };
    auto expression =
        Parser("param x=voltage(a)+voltage(b)\n").parse()[0].value;
    try {
        evaluate(expression, empty, cross, {});
        throw std::runtime_error("axis mixing accepted");
    } catch (const Error& error) {
        expect(std::string(error.what()).find("different analysis axes") !=
                   std::string::npos,
               "axis mismatch");
    }
    std::ostringstream csv;
    writeCsv(csv, result.analyses[0]);
    expect(csv.str().find("sample,\"voltage(output) [V]\"") == 0,
           "CSV observed columns");
    near(run(compile("voltage_source(\"V\", n, ground, "
                     "2V+3V*4)\ndc(\"b\")\nmeasure(\"numeric\", from=\"b\", "
                     "value=voltage(n))\n"))
             .measurements[0]
             .value.value,
         14);
    expect(run(compile("voltage_source(\"V\", n, ground, 1V)\nresistor(\"R\", "
                       "n, ground, 1kohm)\ndc(\"+\")\ncheck(\"(\", 1==1)\n"))
               .checksPassed(),
           "punctuation labels");
    expect(run(compile("voltage_source(\")\", n, ground, "
                       "1V)\ndc(\")\")\nobserve(\")\", "
                       "current(\")\"))\ncheck(\")\", 1==1)\n"))
               .checksPassed(),
           "quoted closing delimiter");
}

void exampleParity() {
    Circuit direct;
    direct.addVoltageSource("V1", "supply", "0", 5);
    direct.addResistor("R1", "supply", "0", 1000);
    auto scripted = run(compile(example("resistor_load")));
    near(scripted.analyses[0].observations[0].value.data[0],
         solveDc(direct).voltage("supply"));
    expect(scripted.checksPassed(), "load example check");
    direct = Circuit{};
    direct.addCurrentSource("I1", "0", "output", 0.002);
    direct.addResistor("R1", "output", "0", 1000);
    scripted = run(compile(example("current_driven_resistor")));
    near(scripted.analyses[0].observations[0].value.data[0],
         solveDc(direct).voltage("output"));
    expect(scripted.checksPassed(), "current example check");
    direct = Circuit{};
    direct.addVoltageSource("V1", "supply", "0", 10);
    direct.addResistor("R1", "supply", "output", 1000);
    direct.addResistor("R2", "output", "0", 1000);
    scripted = run(compile(example("voltage_divider")));
    near(scripted.analyses[0].observations[0].value.data[0],
         solveDc(direct).voltage("output"));
    expect(scripted.checksPassed(), "divider example check");
    direct = Circuit{};
    direct.addVoltageSource("V1", "supply", "0", 10);
    direct.addResistor("R1", "supply", "output", 1000);
    direct.addCapacitor("C1", "output", "0", 1e-6);
    scripted = run(compile(example("rc_charging")));
    const auto nativeTransient = solveTransient(direct, {0.0001, 0.005, {0}});
    const auto& series = scripted.analyses[0].observations[0].value.data;
    expect(series.size() == nativeTransient.samples.size(),
           "RC parity sample count");
    for (std::size_t index = 0; index < series.size(); ++index)
        near(series[index], nativeTransient.voltage(index, "output"));
    expect(scripted.checksPassed(), "RC example check");
    direct = Circuit{};
    direct.addVoltageSource("V1", "supply", "0", 5);
    direct.addResistor("R1", "supply", "output", 1000);
    direct.addDiode("D1", "output", "0");
    scripted = run(compile(example("diode_bias")));
    near(scripted.analyses[0].observations[0].value.data[0],
         solveNonlinearDc(direct).operatingPoint.voltage("output"));
    const auto nativeSweep = solveDcSweep(direct, {"V1", 0, 5, 0.1, {}});
    const auto& swept = scripted.analyses[1].observations[0].value.data;
    expect(swept.size() == nativeSweep.points.size(),
           "diode sweep sample parity");
    for (std::size_t index = 0; index < swept.size(); ++index)
        near(swept[index],
             nativeSweep.points[index].result.operatingPoint.voltage("output"));
    expect(scripted.checksPassed(), "diode example checks");
    expect(run(compile(example("subcircuit_divider"))).checksPassed(),
           "hierarchy example checks");
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        const std::string mode = argc > 1 ? argv[1] : "all";
        if (mode == "compiler" || mode == "all") {
            compilerTests();
            compilerFailures();
            limitsAndCancellation();
            malformedInputs();
        }
        if (mode == "runtime" || mode == "all") {
            runtimeTests();
            hierarchyTests();
            exampleParity();
        }
        std::cout << assertions << " script assertions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "script test failure: " << error.what() << '\n';
        return 1;
    }
}
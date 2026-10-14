#pragma once

#include <QPointF>
#include <QString>
#include <sstream>
#include <vector>

#include "diagnostics.hpp"
#include "microspice/csv.hpp"

namespace microspice::gui {

struct WaveformTrace {
    QString name;
    QString unit;
    std::vector<QPointF> points;
};

struct WaveformData {
    QString horizontalLabel;
    std::vector<WaveformTrace> traces;
    QByteArray csv;
    std::vector<DiagnosticFrame> diagnostics;
};

inline void checkAnalysisSize(const Circuit& circuit, double count,
                              bool nonlinear) {
    const double unknowns = static_cast<double>(
        circuit.nodes().size() + circuit.voltageSources().size());
    const double diagnostics = count *
                               (unknowns * unknowns + unknowns * 4 + 1) *
                               (nonlinear ? 100 : 1);
    if (!std::isfinite(count) || count < 1 || count > 2000 ||
        diagnostics > 4000000) {
        throw std::invalid_argument(
            "Interactive analysis exceeds 2000 samples or the diagnostic "
            "memory budget");
    }
}

inline WaveformData transientWaveforms(const Circuit& circuit,
                                       const TransientOptions& options,
                                       const ExecutionControl& execution = {}) {
    if (!std::isfinite(options.timeStepSeconds) ||
        options.timeStepSeconds <= 0 ||
        !std::isfinite(options.stopTimeSeconds) ||
        options.stopTimeSeconds < options.timeStepSeconds) {
        throw std::invalid_argument(
            "Transient stop time must be at least one positive timestep");
    }
    checkAnalysisSize(
        circuit,
        std::floor(options.stopTimeSeconds / options.timeStepSeconds + 1e-12),
        false);
    const auto result = solveTransient(circuit, options, execution);
    WaveformData data{"Time (s)", {}, {}, {}};
    const auto staticStamps = assembleDcSystem(circuit).stamps;
    for (const auto& sample : result.samples) {
        execution.check();
        data.diagnostics.push_back(
            {QString("t = %1 s").arg(sample.timeSeconds, 0, 'g', 9),
             unknownLabels(result.nodeNames, result.voltageSourceNames),
             sample.systemMatrix,
             sample.rightHandSide,
             sample.solution,
             staticStamps,
             sample.capacitors,
             {},
             true});
    }
    for (std::size_t node = 0; node < result.nodeNames.size(); ++node) {
        WaveformTrace trace{
            QString::fromStdString("V(" + result.nodeNames[node] + ")"),
            "V",
            {}};
        for (std::size_t sample = 0; sample < result.samples.size(); ++sample) {
            trace.points.emplace_back(
                result.samples[sample].timeSeconds,
                result.voltage(sample, result.nodeNames[node]));
        }
        data.traces.push_back(std::move(trace));
    }
    for (std::size_t source = 0; source < result.voltageSourceNames.size();
         ++source) {
        WaveformTrace trace{QString::fromStdString(
                                "I(" + result.voltageSourceNames[source] + ")"),
                            "A",
                            {}};
        for (const auto& sample : result.samples) {
            trace.points.emplace_back(
                sample.timeSeconds,
                sample.solution[result.nodeNames.size() + source]);
        }
        data.traces.push_back(std::move(trace));
    }
    std::ostringstream output;
    writeTransientCsv(output, result);
    data.csv = QByteArray::fromStdString(output.str());
    return data;
}

inline WaveformData sweepWaveforms(const Circuit& circuit,
                                   const DcSweepOptions& options,
                                   const ExecutionControl& execution = {}) {
    const double span = options.stopVoltage - options.startVoltage;
    if (!std::isfinite(options.startVoltage) ||
        !std::isfinite(options.stopVoltage) ||
        !std::isfinite(options.stepVoltage) || options.stepVoltage == 0 ||
        (span > 0 && options.stepVoltage < 0) ||
        (span < 0 && options.stepVoltage > 0)) {
        throw std::invalid_argument(
            "Sweep requires a nonzero step toward the stop voltage");
    }
    checkAnalysisSize(circuit,
                      std::floor(span / options.stepVoltage + 1e-12) + 1,
                      !circuit.diodes().empty());
    const auto result = solveDcSweep(circuit, options, execution);
    WaveformData data{
        QString::fromStdString(options.sourceName) + " (V)", {}, {}, {}};
    const auto& first = result.points.front().result.operatingPoint;
    for (const auto& node : first.nodeNames) {
        data.traces.push_back(
            {QString::fromStdString("V(" + node + ")"), "V", {}});
    }
    for (const auto& source : first.voltageSourceNames) {
        data.traces.push_back(
            {QString::fromStdString("I(" + source + ")"), "A", {}});
    }
    for (const auto& point : result.points) {
        execution.check();
        data.diagnostics.push_back(nonlinearDiagnostics(
            point.result, QString::fromStdString(options.sourceName) + " = " +
                              QString::number(point.sourceVoltage, 'g', 9) +
                              " V"));
        if (!point.result.converged) {
            throw std::runtime_error("DC sweep failed to converge at " +
                                     std::to_string(point.sourceVoltage) +
                                     " V");
        }
        for (std::size_t index = 0; index < data.traces.size(); ++index) {
            data.traces[index].points.emplace_back(
                point.sourceVoltage,
                point.result.operatingPoint.solution[index]);
        }
    }
    std::ostringstream output;
    writeDcSweepCsv(output, result);
    data.csv = QByteArray::fromStdString(output.str());
    return data;
}

}  // namespace microspice::gui
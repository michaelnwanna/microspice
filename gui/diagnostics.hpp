#pragma once

#include <QStringList>

#include "microspice/nonlinear_solver.hpp"
#include "microspice/transient_solver.hpp"

namespace microspice::gui {

struct DiagnosticFrame {
    QString label;
    QStringList unknowns;
    DenseMatrix matrix;
    std::vector<double> rhs;
    std::vector<double> solution;
    std::vector<StampTrace> stamps;
    std::vector<CapacitorStepInfo> capacitors;
    std::vector<NewtonIteration> iterations;
    bool converged = true;
};

inline QStringList unknownLabels(const std::vector<std::string>& nodes,
                                 const std::vector<std::string>& sources) {
    QStringList labels;
    for (const auto& node : nodes) {
        labels.push_back(QString::fromStdString("V(" + node + ")"));
    }
    for (const auto& source : sources) {
        labels.push_back(QString::fromStdString("I(" + source + ")"));
    }
    return labels;
}

inline DiagnosticFrame dcDiagnostics(
    const DcResult& result, const QString& label = "DC operating point") {
    return {label,
            unknownLabels(result.nodeNames, result.voltageSourceNames),
            result.systemMatrix,
            result.rightHandSide,
            result.solution,
            result.stamps,
            {},
            {},
            true};
}

inline DiagnosticFrame nonlinearDiagnostics(const NonlinearDcResult& result,
                                            const QString& label) {
    auto frame = dcDiagnostics(result.operatingPoint, label);
    frame.iterations = result.iterations;
    frame.converged = result.converged;
    return frame;
}

}  // namespace microspice::gui
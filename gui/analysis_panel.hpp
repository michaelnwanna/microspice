#pragma once

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <map>

#include "background_job.hpp"
#include "editor_scene.hpp"
#include "microspice/netlist.hpp"
#include "waveform_plot.hpp"

namespace microspice::gui {

class AnalysisPanel final : public QWidget {
public:
    std::function<void(const std::vector<DiagnosticFrame>&)> diagnosticsChanged;
    std::function<void(bool)> busyChanged;
    explicit AnalysisPanel(EditorScene& scene, QWidget* parent = nullptr)
        : QWidget(parent), scene_(scene) {
        auto* layout = new QVBoxLayout(this);
        auto* toolbar = new QHBoxLayout;
        trace_ = new QComboBox(this);
        trace_->setMinimumContentsLength(12);
        trace_->setSizeAdjustPolicy(
            QComboBox::AdjustToMinimumContentsLengthWithIcon);
        toolbar->addWidget(trace_, 1);
        run_ = button("Run analysis", QStyle::SP_MediaPlay, toolbar);
        cancel_ = button("Cancel analysis", QStyle::SP_MediaStop, toolbar);
        cancel_->setEnabled(false);
        progress_ = new QProgressBar(this);
        progress_->setMaximumWidth(120);
        progress_->hide();
        toolbar->addWidget(progress_);
        export_ = button("Export CSV", QStyle::SP_DialogSaveButton, toolbar);
        auto* reset =
            button("Reset plot view", QStyle::SP_BrowserReload, toolbar);
        layout->addLayout(toolbar);
        auto* splitter = new QSplitter(this);
        tabs_ = new QTabWidget(splitter);
        tabs_->setMinimumWidth(220);
        auto* transient = new QWidget(tabs_);
        auto* transientForm = new QFormLayout(transient);
        timeStep_ = new QLineEdit("100u", transient);
        stopTime_ = new QLineEdit("5m", transient);
        transientForm->addRow("Timestep (s)", timeStep_);
        transientForm->addRow("Stop (s)", stopTime_);
        initial_ = new QTableWidget(0, 2, transient);
        initial_->setHorizontalHeaderLabels({"Capacitor", "Initial V"});
        initial_->setColumnWidth(0, 80);
        initial_->setColumnWidth(1, 85);
        initial_->setMinimumHeight(70);
        transientForm->addRow(initial_);
        tabs_->addTab(transient, "Transient");
        auto* sweep = new QWidget(tabs_);
        auto* sweepForm = new QFormLayout(sweep);
        source_ = new QComboBox(sweep);
        start_ = new QLineEdit("0", sweep);
        stop_ = new QLineEdit("10", sweep);
        step_ = new QLineEdit("0.1", sweep);
        sweepForm->addRow("Source", source_);
        sweepForm->addRow("Start (V)", start_);
        sweepForm->addRow("Stop (V)", stop_);
        sweepForm->addRow("Step (V)", step_);
        tabs_->addTab(sweep, "DC sweep");
        plot_ = new WaveformPlot(splitter);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({240, 640});
        layout->addWidget(splitter, 1);
        status_ = new QLabel(this);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        QObject::connect(run_, &QToolButton::clicked, this,
                         [this] { startAnalysis(); });
        QObject::connect(cancel_, &QToolButton::clicked, this,
                         [this] { cancelAnalysis(); });
        job_.finished = [this](std::optional<WaveformData> data,
                               const QString& error) {
            run_->setEnabled(!externalBusy_);
            cancel_->setEnabled(false);
            progress_->hide();
            if (busyChanged) {
                busyChanged(false);
            }
            if (discardPending_) {
                return;
            }
            if (data) {
                publish(std::move(*data));
            } else {
                status_->setText(error);
            }
        };
        job_.progress = [this](std::size_t completed, std::size_t total) {
            progress_->setRange(0, static_cast<int>(total));
            progress_->setValue(static_cast<int>(completed));
        };
        QObject::connect(reset, &QToolButton::clicked, plot_,
                         [this] { plot_->resetView(); });
        QObject::connect(
            trace_, qOverload<int>(&QComboBox::currentIndexChanged), plot_,
            [this](int index) {
                if (index >= 0 &&
                    static_cast<std::size_t>(index) < data_.traces.size()) {
                    plot_->setTrace(
                        data_.traces[static_cast<std::size_t>(index)],
                        data_.horizontalLabel);
                }
            });
        for (auto* field : {timeStep_, stopTime_, start_, stop_, step_}) {
            QObject::connect(field, &QLineEdit::textChanged, this,
                             [this] { clearResults(); });
        }
        QObject::connect(source_,
                         qOverload<int>(&QComboBox::currentIndexChanged), this,
                         [this](int) { clearResults(); });
        QObject::connect(tabs_, &QTabWidget::currentChanged, this,
                         [this](int) { clearResults(); });
        QObject::connect(initial_, &QTableWidget::itemChanged, this,
                         [this](QTableWidgetItem*) { clearResults(); });
        QObject::connect(export_, &QToolButton::clicked, this, [this] {
            const auto filename = QFileDialog::getSaveFileName(
                this, "Export waveform CSV", "waveform.csv",
                "CSV files (*.csv)");
            if (filename.isEmpty()) {
                return;
            }
            try {
                exportCsv(filename);
                status_->setText("CSV exported");
            } catch (const std::exception& error) {
                status_->setText(QString::fromUtf8(error.what()));
            }
        });
        invalidate();
    }

    void invalidate() {
        clearResults();
        const auto components = scene_.components();
        const QString selected = source_->currentText();
        std::map<QString, QString> initialValues;
        for (int row = 0; row < initial_->rowCount(); ++row) {
            initialValues[initial_->item(row, 0)->text()] =
                initial_->item(row, 1)->text();
        }
        const QSignalBlocker sourceBlocker(source_);
        const QSignalBlocker tableBlocker(initial_);
        source_->clear();
        initial_->setRowCount(0);
        for (const auto& component : components) {
            if (component.kind == ComponentKind::Voltage) {
                source_->addItem(component.name);
            }
            if (component.kind == ComponentKind::Capacitor) {
                const int row = initial_->rowCount();
                initial_->insertRow(row);
                auto* name = new QTableWidgetItem(component.name);
                name->setFlags(name->flags() & ~Qt::ItemIsEditable);
                initial_->setItem(row, 0, name);
                initial_->setItem(
                    row, 1,
                    new QTableWidgetItem(initialValues.count(component.name)
                                             ? initialValues[component.name]
                                             : "0"));
            }
        }
        const int index = source_->findText(selected);
        if (index >= 0) {
            source_->setCurrentIndex(index);
        }
    }

    void resetInitialConditions() {
        initial_->setRowCount(0);
        invalidate();
    }

    bool startAnalysis() {
        if (job_.running() || externalBusy_) {
            return false;
        }
        clearResults();
        try {
            auto work = prepareAnalysis();
            discardPending_ = false;
            job_.start(std::move(work));
            run_->setEnabled(false);
            cancel_->setEnabled(true);
            progress_->setRange(0, 0);
            progress_->show();
            status_->setText("Running analysis");
            if (busyChanged) {
                busyChanged(true);
            }
            return true;
        } catch (const std::exception& error) {
            status_->setText(QString::fromUtf8(error.what()));
            return false;
        }
    }
    void cancelAnalysis() {
        job_.cancel();
        status_->setText("Cancelling analysis");
    }
    bool busy() const { return job_.running(); }
    void setExternalBusy(bool busy) {
        externalBusy_ = busy;
        run_->setEnabled(!busy && !job_.running());
    }

    bool runAnalysis() {
        if (job_.running()) {
            return false;
        }
        clearResults();
        try {
            publish(prepareAnalysis()({}));
            return true;
        } catch (const std::exception& error) {
            status_->setText(QString::fromUtf8(error.what()));
            return false;
        }
    }

private:
    std::function<WaveformData(const ExecutionControl&)> prepareAnalysis()
        const {
        const auto extracted = scene_.extract();
        const bool transient = tabs_->currentIndex() == 0;
        TransientOptions transientOptions{};
        DcSweepOptions sweepOptions{};
        if (transient) {
            std::map<QString, double> initialValues;
            for (int row = 0; row < initial_->rowCount(); ++row) {
                initialValues[initial_->item(row, 0)->text()] =
                    parse(initial_->item(row, 1)->text());
            }
            std::vector<double> voltages;
            for (const auto& capacitor : extracted.circuit.capacitors()) {
                voltages.push_back(
                    initialValues.at(QString::fromStdString(capacitor.name)));
            }
            transientOptions = {parse(timeStep_->text()),
                                parse(stopTime_->text()), voltages};
        } else {
            if (source_->currentText().isEmpty()) {
                throw std::invalid_argument(
                    "Add a voltage source for DC sweep");
            }
            sweepOptions = {source_->currentText().toStdString(),
                            parse(start_->text()),
                            parse(stop_->text()),
                            parse(step_->text()),
                            {}};
        }
        return [extracted, transient, transientOptions,
                sweepOptions](const ExecutionControl& execution) {
            auto data = transient
                            ? transientWaveforms(extracted.circuit,
                                                 transientOptions, execution)
                            : sweepWaveforms(extracted.circuit, sweepOptions,
                                             execution);
            if (!data.traces.empty()) {
                WaveformTrace ground{"V(0)", "V", {}};
                for (const auto& point : data.traces.front().points) {
                    ground.points.emplace_back(point.x(), 0);
                }
                data.traces.push_back(std::move(ground));
            }
            for (auto& waveform : data.traces) {
                for (const auto& terminal : extracted.terminals) {
                    for (std::size_t pin = 0; pin < terminal.second.size();
                         ++pin) {
                        if (waveform.name ==
                            "V(" +
                                QString::fromStdString(terminal.second[pin]) +
                                ")") {
                            waveform.name +=
                                "  " + terminal.first + "." +
                                QString::number(
                                    static_cast<qulonglong>(pin + 1));
                            break;
                        }
                    }
                    if (waveform.name.contains("  ")) {
                        break;
                    }
                }
            }
            return data;
        };
    }
    void publish(WaveformData data) {
        data_ = std::move(data);
        for (const auto& waveform : data_.traces) {
            trace_->addItem(waveform.name);
        }
        std::size_t bestTrace = 0;
        double greatestSpan = -1;
        for (std::size_t index = 0; index < data_.traces.size(); ++index) {
            if (data_.traces[index].unit != "V") {
                continue;
            }
            const auto extrema =
                std::minmax_element(data_.traces[index].points.begin(),
                                    data_.traces[index].points.end(),
                                    [](const auto& first, const auto& second) {
                                        return first.y() < second.y();
                                    });
            const double span = extrema.second->y() - extrema.first->y();
            if (span > greatestSpan) {
                greatestSpan = span;
                bestTrace = index;
            }
        }
        if (!data_.traces.empty()) {
            trace_->setCurrentIndex(static_cast<int>(bestTrace));
        }
        export_->setEnabled(!data_.csv.isEmpty());
        status_->setText(
            QString("%1 samples")
                .arg(data_.traces.empty()
                         ? 0
                         : static_cast<qulonglong>(
                               data_.traces.front().points.size())));
        if (diagnosticsChanged) {
            diagnosticsChanged(data_.diagnostics);
        }
    }

public:
    void exportCsv(const QString& filename) const {
        if (data_.csv.isEmpty()) {
            throw std::runtime_error("Run an analysis before exporting CSV");
        }
        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(data_.csv) != data_.csv.size() || !file.commit()) {
            throw std::runtime_error(file.errorString().toStdString());
        }
    }

    void setMode(bool sweep) { tabs_->setCurrentIndex(sweep ? 1 : 0); }
    void setTransient(const QString& timestep, const QString& stop) {
        timeStep_->setText(timestep);
        stopTime_->setText(stop);
    }
    void setSweep(const QString& source, const QString& start,
                  const QString& stop, const QString& step) {
        source_->setCurrentText(source);
        start_->setText(start);
        stop_->setText(stop);
        step_->setText(step);
    }
    const WaveformData& data() const { return data_; }
    bool selectProbe(const QString& key) {
        for (std::size_t index = 0; index < data_.traces.size(); ++index) {
            const auto& name = data_.traces[index].name;
            if (name == key || name.startsWith(key + "  ")) {
                selectTrace(static_cast<int>(index));
                return true;
            }
        }
        return false;
    }
    void selectTrace(int index) { trace_->setCurrentIndex(index); }
    WaveformPlot& plot() { return *plot_; }
    QString status() const { return status_->text(); }

private:
    QToolButton* button(const QString& text, QStyle::StandardPixmap icon,
                        QHBoxLayout* layout) {
        auto* tool = new QToolButton(this);
        tool->setIcon(style()->standardIcon(icon));
        tool->setToolTip(text);
        tool->setAccessibleName(text);
        layout->addWidget(tool);
        return tool;
    }
    static double parse(const QString& text) {
        return parseGuiEngineeringValue(text);
    }
    void clearResults() {
        if (job_.running()) {
            discardPending_ = true;
            job_.cancel();
        }
        const bool hadDiagnostics = !data_.diagnostics.empty();
        data_ = {};
        trace_->clear();
        plot_->clear();
        export_->setEnabled(false);
        status_->clear();
        if (hadDiagnostics && diagnosticsChanged) {
            diagnosticsChanged({});
        }
    }

    EditorScene& scene_;
    WaveformData data_;
    QComboBox* trace_;
    QComboBox* source_;
    QTabWidget* tabs_;
    QLineEdit* timeStep_;
    QLineEdit* stopTime_;
    QLineEdit* start_;
    QLineEdit* stop_;
    QLineEdit* step_;
    QTableWidget* initial_;
    WaveformPlot* plot_;
    QLabel* status_;
    QToolButton* run_;
    QToolButton* cancel_;
    QProgressBar* progress_;
    QToolButton* export_;
    bool discardPending_ = false;
    bool externalBusy_ = false;
    BackgroundJob<WaveformData> job_;
};

}  // namespace microspice::gui
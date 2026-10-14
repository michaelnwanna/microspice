#pragma once

#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringConverter>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <sstream>

#include "background_job.hpp"
#include "help_dialog.hpp"
#include "inspector.hpp"
#include "microspice/script/output.hpp"
#include "script_highlighter.hpp"
#include "ui_style.hpp"
#include "waveform_plot.hpp"

namespace microspice::gui {

struct ScriptOutcome {
    std::optional<script::CompiledSimulation> compiled;
    std::optional<script::RunResult> result;
    std::optional<script::Diagnostic> diagnostic;
};

class ScriptWorkspace final : public QDialog {
public:
    enum class Decision { save, discard, cancel };
    std::function<Decision()> decisionProvider;
    std::function<QString()> savePathProvider;
    static constexpr qint64 sourceLimit = 1024 * 1024;

    explicit ScriptWorkspace(QWidget* parent = nullptr) : QDialog(parent) {
        setModal(false);
        resize(1100, 760);
        auto* layout = new QVBoxLayout(this);
        auto* toolbar = new QToolBar("Script", this);
        toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        toolbar->setIconSize(QSize(22, 22));
        layout->addWidget(toolbar);
        const auto action =
            [this, toolbar](const QString& title, const QString& icon,
                            const auto& handler, QKeySequence shortcut = {}) {
                auto* command = toolbar->addAction(actionIcon(icon), title);
                command->setToolTip(title);
                command->setShortcut(shortcut);
                command->setShortcutContext(Qt::WidgetWithChildrenShortcut);
                if (auto* button = toolbar->widgetForAction(command))
                    button->setAccessibleName(title);
                QObject::connect(command, &QAction::triggered, this, handler);
                return command;
            };
        action(
            "New script", "file-plus-2", [this] { newDocument(); },
            QKeySequence::New);
        action(
            "Open script", "folder-open",
            [this] {
                const auto filename = QFileDialog::getOpenFileName(
                    this, "Open script", path_, "Simulation scripts (*.msim)");
                if (!filename.isEmpty()) openFile(filename);
            },
            QKeySequence::Open);
        action("Save script", "save", [this] { save(); }, QKeySequence::Save);
        action(
            "Save script as", "save",
            [this] {
                const auto filename =
                    QFileDialog::getSaveFileName(this, "Save script as", path_,
                                                 "Simulation scripts (*.msim)");
                if (!filename.isEmpty()) saveAs(filename);
            },
            QKeySequence::SaveAs);
        auto* examples = new QToolButton(toolbar);
        examples->setIcon(actionIcon("folder-open"));
        examples->setToolTip("Script examples");
        examples->setAccessibleName("Script examples");
        examples->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(examples);
        for (const auto& name : exampleNames()) {
            auto* example = menu->addAction(name);
            QObject::connect(example, &QAction::triggered, this,
                             [this, name] { openExample(name); });
        }
        examples->setMenu(menu);
        toolbar->addWidget(examples);
        toolbar->addSeparator();
        undo_ = action(
            "Undo", "undo-2", [this] { editor_->undo(); }, QKeySequence::Undo);
        redo_ = action(
            "Redo", "redo-2", [this] { editor_->redo(); }, QKeySequence::Redo);
        undo_->setEnabled(false);
        redo_->setEnabled(false);
        toolbar->addSeparator();
        compile_ = action("Compile / Validate", "crosshair",
                          [this] { compileScript(); });
        run_ = action(
            "Run", "play", [this] { runScript(); }, QKeySequence(Qt::Key_F5));
        cancel_ = action("Cancel", "square", [this] { cancelJob(); });
        cancel_->setEnabled(false);
        export_ = action("Export analysis CSV", "save", [this] {
            const auto filename = QFileDialog::getSaveFileName(
                this, "Export analysis", {}, "CSV files (*.csv)");
            if (!filename.isEmpty()) exportCsv(filename);
        });
        export_->setEnabled(false);
        action("Simulation language guide", "folder-open", [this] {
            if (!guide_)
                guide_ = new HelpDialog(this, ":/help/simulation-language.md",
                                        "Simulation Language");
            guide_->show();
            guide_->raise();
            guide_->activateWindow();
        });
        auto* selection = new QHBoxLayout;
        selection->addWidget(new QLabel("Run", this));
        analyses_ = new QComboBox(this);
        analyses_->setAccessibleName("Analysis to run");
        analyses_->addItem("All analyses", QString());
        selection->addWidget(analyses_, 1);
        progress_ = new QProgressBar(this);
        progress_->setAccessibleName("Script progress");
        progress_->hide();
        selection->addWidget(progress_, 1);
        layout->addLayout(selection);
        auto* splitter = new QSplitter(this);
        auto* sourcePanel = new QWidget(splitter);
        auto* sourceLayout = new QVBoxLayout(sourcePanel);
        sourceLayout->setContentsMargins(0, 0, 0, 0);
        auto* searchRow = new QHBoxLayout;
        search_ = new QLineEdit(sourcePanel);
        search_->setPlaceholderText("Find in source");
        search_->setAccessibleName("Find in source");
        searchRow->addWidget(search_, 1);
        for (const bool backwards : {true, false}) {
            auto* button = new QToolButton(sourcePanel);
            button->setIcon(actionIcon(backwards ? "undo-2" : "redo-2"));
            button->setToolTip(backwards ? "Previous match" : "Next match");
            button->setAccessibleName(button->toolTip());
            searchRow->addWidget(button);
            QObject::connect(
                button, &QToolButton::clicked, this,
                [this, backwards] { findSource(search_->text(), backwards); });
        }
        sourceLayout->addLayout(searchRow);
        editor_ = new QPlainTextEdit(this);
        editor_->setAccessibleName("Simulation script source");
        editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
        new ScriptHighlighter(editor_->document());
        sourceLayout->addWidget(editor_, 1);
        auto* find = new QAction("Find in source", this);
        find->setShortcut(QKeySequence::Find);
        find->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(find);
        QObject::connect(find, &QAction::triggered, this, [this] {
            search_->setFocus();
            search_->selectAll();
        });
        QObject::connect(search_, &QLineEdit::returnPressed, this,
                         [this] { findSource(search_->text()); });
        auto* tabs = new QTabWidget(splitter);
        summary_ = new QPlainTextEdit(tabs);
        summary_->setReadOnly(true);
        summary_->setAccessibleName("Script result summary");
        tabs->addTab(summary_, "Summary");
        auto* observations = new QWidget(tabs);
        auto* observationLayout = new QVBoxLayout(observations);
        results_ = new QComboBox(observations);
        results_->setAccessibleName("Result analysis");
        traces_ = new QComboBox(observations);
        traces_->setAccessibleName("Observation trace");
        scalar_ = new QLabel(observations);
        scalar_->setWordWrap(true);
        scalar_->setAccessibleName("DC observation");
        plot_ = new WaveformPlot(observations);
        plot_->setMinimumWidth(410);
        plot_->setAccessibleName("Script waveform");
        auto* reset = new QToolButton(observations);
        reset->setIcon(actionIcon("maximize"));
        reset->setToolTip("Reset waveform view");
        reset->setAccessibleName(reset->toolTip());
        QObject::connect(reset, &QToolButton::clicked, plot_,
                         [this] { plot_->resetView(); });
        observationLayout->addWidget(results_);
        auto* traceRow = new QHBoxLayout;
        traceRow->addWidget(traces_, 1);
        traceRow->addWidget(reset);
        observationLayout->addLayout(traceRow);
        observationLayout->addWidget(scalar_);
        observationLayout->addWidget(plot_, 1);
        tabs->addTab(observations, "Observations");
        inspector_ = new SolverInspector(tabs);
        tabs->addTab(inspector_, "Solver Inspector");
        diagnostics_ = new QListWidget(tabs);
        diagnostics_->setAccessibleName("Script source diagnostics");
        diagnostics_->setWordWrap(true);
        tabs->addTab(diagnostics_, "Diagnostics");
        splitter->setChildrenCollapsible(false);
        splitter->setSizes({550, 500});
        layout->addWidget(splitter, 1);
        status_ = new QLabel(this);
        status_->setWordWrap(true);
        status_->setAccessibleName("Script status");
        layout->addWidget(status_);
        QObject::connect(editor_, &QPlainTextEdit::undoAvailable, undo_,
                         &QAction::setEnabled);
        QObject::connect(editor_, &QPlainTextEdit::redoAvailable, redo_,
                         &QAction::setEnabled);
        QObject::connect(editor_, &QPlainTextEdit::textChanged, this,
                         [this] { invalidate(); });
        QObject::connect(editor_->document(),
                         &QTextDocument::modificationChanged, this,
                         [this] { updateTitle(); });
        QObject::connect(diagnostics_, &QListWidget::currentRowChanged, this,
                         [this](int row) { navigateDiagnostic(row); });
        QObject::connect(diagnostics_, &QListWidget::itemClicked, this,
                         [this](QListWidgetItem* item) {
                             navigateDiagnostic(diagnostics_->row(item));
                         });
        QObject::connect(results_,
                         qOverload<int>(&QComboBox::currentIndexChanged), this,
                         [this](int) { displayAnalysis(); });
        QObject::connect(traces_,
                         qOverload<int>(&QComboBox::currentIndexChanged), this,
                         [this](int) { displayTrace(); });
        job_.progress = [this](std::size_t completed, std::size_t total) {
            if (total) {
                progress_->setRange(0, static_cast<int>(total));
                progress_->setValue(static_cast<int>(completed));
            }
        };
        job_.finished = [this](std::optional<ScriptOutcome> outcome,
                               const QString& error) {
            setBusy(false);
            if (discard_ || jobRevision_ != revision_) return;
            if (!outcome) {
                status_->setText(error);
                return;
            }
            if (outcome->compiled) {
                compiled_ = std::move(outcome->compiled);
                setDiagnostics(compiled_->warnings);
                const auto selected = analyses_->currentData().toString();
                const QSignalBlocker blocker(analyses_);
                analyses_->clear();
                analyses_->addItem("All analyses", QString());
                for (const auto& request : compiled_->analyses) {
                    const auto name = QString::fromStdString(request.name);
                    analyses_->addItem(name, name);
                }
                const int index = analyses_->findData(selected);
                analyses_->setCurrentIndex(index >= 0 ? index : 0);
            }
            if (outcome->diagnostic) {
                auto diagnostics = diagnosticData_;
                diagnostics.push_back(*outcome->diagnostic);
                setDiagnostics(std::move(diagnostics));
                diagnostics_->setCurrentRow(diagnostics_->count() - 1);
                status_->setText(
                    "Script failed: " +
                    QString::fromStdString(outcome->diagnostic->message));
                return;
            }
            if (outcome->result) {
                result_ = std::move(outcome->result);
                publishResults();
                status_->setText(result_->checksPassed()
                                     ? "Run complete"
                                     : "Run complete: checks FAIL");
            } else {
                status_->setText(QString("Validated: %1 analyses, %2 warnings")
                                     .arg(compiled_->analyses.size())
                                     .arg(compiled_->warnings.size()));
            }
        };
        updateTitle();
    }

    QPlainTextEdit* editor() const { return editor_; }
    QString filePath() const { return path_; }
    QString lastError() const { return error_; }
    bool busy() const { return job_.running(); }
    const std::optional<script::RunResult>& runResult() const {
        return result_;
    }
    const std::vector<script::Diagnostic>& sourceDiagnostics() const {
        return diagnosticData_;
    }
    QString summaryText() const { return summary_->toPlainText(); }
    QString statusText() const { return status_->text(); }
    WaveformPlot* plot() const { return plot_; }
    SolverInspector* inspector() const { return inspector_; }
    QComboBox* analysisSelector() const { return analyses_; }
    QComboBox* resultSelector() const { return results_; }
    QComboBox* traceSelector() const { return traces_; }
    bool exportEnabled() const { return export_->isEnabled(); }
    QString scalarText() const { return scalar_->text(); }

    static QStringList exampleNames() {
        return {"voltage_divider", "resistor_load", "current_driven_resistor",
                "rc_charging",     "diode_bias",    "subcircuit_divider"};
    }
    bool openExample(const QString& name) {
        return openFile(":/scripts/" + name + ".msim", true);
    }
    bool compileScript() { return startJob(false); }
    bool runScript() { return startJob(true); }
    void cancelJob() {
        discard_ = true;
        job_.cancel();
        clearResults();
        status_->setText("Script cancelled");
    }
    bool findSource(const QString& text, bool backwards = false) {
        if (text.isEmpty()) return false;
        const auto flags = backwards ? QTextDocument::FindBackward
                                     : QTextDocument::FindFlags();
        if (editor_->find(text, flags)) return true;
        auto cursor = editor_->textCursor();
        cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        editor_->setTextCursor(cursor);
        const bool found = editor_->find(text, flags);
        if (!found) status_->setText("No source matches");
        return found;
    }
    void navigateDiagnostic(int index) {
        if (index < 0 ||
            static_cast<std::size_t>(index) >= diagnosticData_.size())
            return;
        auto cursor = diagnosticCursor(
            diagnosticData_[static_cast<std::size_t>(index)].span);
        editor_->setTextCursor(cursor);
        editor_->ensureCursorVisible();
        editor_->setFocus();
    }
    bool exportCsv(const QString& filename) {
        error_.clear();
        const auto* analysis = selectedResult();
        if (!analysis) return fail("No successful analysis to export");
        std::ostringstream stream;
        script::writeCsv(stream, *analysis);
        const auto bytes = QByteArray::fromStdString(stream.str());
        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(bytes) != bytes.size() || !file.commit())
            return fail(file.errorString());
        status_->setText("Analysis CSV exported");
        return true;
    }

    bool saveAs(const QString& filename) {
        error_.clear();
        const auto bytes = editor_->toPlainText().toUtf8();
        if (bytes.size() > sourceLimit) return fail("Source exceeds 1 MiB");
        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(bytes) != bytes.size() || !file.commit())
            return fail(file.errorString());
        path_ = filename;
        editor_->document()->setModified(false);
        updateTitle();
        return true;
    }

    bool save() {
        const auto filename =
            path_.isEmpty()
                ? (savePathProvider ? savePathProvider()
                                    : QFileDialog::getSaveFileName(
                                          this, "Save script", {},
                                          "Simulation scripts (*.msim)"))
                : path_;
        return !filename.isEmpty() && saveAs(filename);
    }

    bool openFile(const QString& filename, bool example = false) {
        error_.clear();
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
        const auto bytes = file.read(sourceLimit + 1);
        if (file.error() != QFileDevice::NoError)
            return fail(file.errorString());
        file.close();
        if (bytes.size() > sourceLimit) return fail("Source exceeds 1 MiB");
        QStringDecoder decoder(QStringDecoder::Utf8,
                               QStringConverter::Flag::Stateless);
        const QString source = decoder(bytes);
        if (decoder.hasError()) return fail("Source is not valid UTF-8");
        const bool wasModified = editor_->document()->isModified();
        if (!canClose()) return false;
        const bool justSavedTarget =
            !example && wasModified && !editor_->document()->isModified() &&
            !path_.isEmpty() &&
            QFileInfo(path_).canonicalFilePath() ==
                QFileInfo(filename).canonicalFilePath();
        replace(justSavedTarget ? editor_->toPlainText() : source,
                example ? QString() : filename);
        if (example) editor_->document()->setModified(true);
        return true;
    }

    bool newDocument() {
        if (!canClose()) return false;
        replace({}, {});
        return true;
    }

    bool canClose() {
        if (!editor_->document()->isModified()) return true;
        Decision decision;
        if (decisionProvider) {
            decision = decisionProvider();
        } else {
            const auto answer = QMessageBox::warning(
                this, "Unsaved script",
                "Save changes to the simulation script?",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);
            decision = answer == QMessageBox::Save      ? Decision::save
                       : answer == QMessageBox::Discard ? Decision::discard
                                                        : Decision::cancel;
        }
        return decision == Decision::discard ||
               (decision == Decision::save && save());
    }

    void reject() override {
        if (canClose()) {
            cancelJob();
            editor_->document()->setModified(false);
            QDialog::reject();
        }
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (canClose()) {
            cancelJob();
            editor_->document()->setModified(false);
            event->accept();
        } else {
            event->ignore();
        }
    }

private:
    bool fail(const QString& message) {
        error_ = message;
        status_->setText(message);
        return false;
    }
    bool startJob(bool run) {
        if (job_.running()) return false;
        const auto source = editor_->toPlainText().toUtf8().toStdString();
        const auto selected = analyses_->currentData().toString().toStdString();
        clearResults();
        setDiagnostics({});
        discard_ = false;
        jobRevision_ = revision_;
        setBusy(true);
        status_->setText(run ? "Running script" : "Compiling script");
        return job_.start(
            [source, selected, run](const ExecutionControl& execution) {
                ScriptOutcome outcome;
                try {
                    outcome.compiled = script::compile(source, {}, execution);
                    if (run)
                        outcome.result =
                            script::run(*outcome.compiled, execution, selected);
                } catch (const script::Error& error) {
                    outcome.diagnostic = error.diagnostic;
                }
                return outcome;
            });
    }
    void setBusy(bool busy) {
        compile_->setEnabled(!busy);
        run_->setEnabled(!busy);
        analyses_->setEnabled(!busy);
        cancel_->setEnabled(busy);
        progress_->setVisible(busy);
        if (busy) progress_->setRange(0, 0);
    }
    void invalidate() {
        ++revision_;
        discard_ = true;
        job_.cancel();
        compiled_.reset();
        clearResults();
        setDiagnostics({});
        analyses_->clear();
        analyses_->addItem("All analyses", QString());
        status_->clear();
    }
    void clearResults() {
        result_.reset();
        results_->clear();
        traces_->clear();
        summary_->clear();
        scalar_->clear();
        plot_->clear();
        inspector_->clear();
        export_->setEnabled(false);
    }
    QTextCursor diagnosticCursor(const script::Span& span) const {
        const auto bytes = editor_->toPlainText().toUtf8();
        const auto offset =
            std::min(span.offset, static_cast<std::size_t>(bytes.size()));
        const auto length = std::min(
            span.length, static_cast<std::size_t>(bytes.size()) - offset);
        const auto start =
            QString::fromUtf8(bytes.constData(), static_cast<qsizetype>(offset))
                .size();
        const auto end =
            QString::fromUtf8(bytes.constData(),
                              static_cast<qsizetype>(offset + length))
                .size();
        QTextCursor cursor(editor_->document());
        cursor.setPosition(static_cast<int>(start));
        cursor.setPosition(static_cast<int>(end), QTextCursor::KeepAnchor);
        return cursor;
    }
    void setDiagnostics(std::vector<script::Diagnostic> diagnostics) {
        diagnosticData_ = std::move(diagnostics);
        diagnostics_->clear();
        QList<QTextEdit::ExtraSelection> selections;
        for (const auto& diagnostic : diagnosticData_) {
            diagnostics_->addItem(
                QString("%1:%2:%3: %4: %5")
                    .arg(path_.isEmpty() ? "Untitled.msim"
                                         : QFileInfo(path_).fileName())
                    .arg(diagnostic.span.line)
                    .arg(diagnostic.span.column)
                    .arg(diagnostic.warning ? "warning" : "error")
                    .arg(QString::fromStdString(diagnostic.message)));
            QTextEdit::ExtraSelection selection;
            selection.cursor = diagnosticCursor(diagnostic.span);
            selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            selection.format.setUnderlineColor(
                diagnostic.warning ? QColor("#a26b15") : QColor("#ba3c3c"));
            selections.push_back(selection);
        }
        editor_->setExtraSelections(selections);
    }
    const script::AnalysisResult* selectedResult() const {
        const int index = results_->currentIndex();
        return result_ && index >= 0 &&
                       static_cast<std::size_t>(index) <
                           result_->analyses.size()
                   ? &result_->analyses[static_cast<std::size_t>(index)]
                   : nullptr;
    }
    void publishResults() {
        QString text;
        for (const auto& analysis : result_->analyses)
            text += QString::fromStdString(analysis.name) + ": " +
                    QString::number(analysis.kind == script::AnalysisKind::dc
                                        ? 1
                                        : analysis.axis.size()) +
                    " samples\n";
        for (const auto& measurement : result_->measurements)
            text += QString::fromStdString(measurement.name) + " = " +
                    QString::number(measurement.value.value, 'g', 12) + " " +
                    QString::fromStdString(
                        script::unitName(measurement.value.dimension)) +
                    "\n";
        for (const auto& check : result_->checks)
            text += QString(check.passed ? "PASS: " : "FAIL: ") +
                    QString::fromStdString(check.name) + "\n";
        for (const auto& skipped : result_->skipped)
            text += "Skipped: " + QString::fromStdString(skipped) + "\n";
        summary_->setPlainText(text);
        {
            const QSignalBlocker blocker(results_);
            results_->clear();
            for (const auto& analysis : result_->analyses)
                results_->addItem(QString::fromStdString(analysis.name));
        }
        displayAnalysis();
    }
    void displayAnalysis() {
        const auto* analysis = selectedResult();
        export_->setEnabled(analysis != nullptr);
        {
            const QSignalBlocker blocker(traces_);
            traces_->clear();
            if (analysis)
                for (const auto& trace : analysis->observations)
                    traces_->addItem(QString::fromStdString(trace.label));
        }
        std::vector<DiagnosticFrame> frames;
        if (analysis) {
            if (const auto* dc =
                    std::get_if<NonlinearDcResult>(&analysis->solver)) {
                frames.push_back(nonlinearDiagnostics(
                    *dc, QString::fromStdString(analysis->name)));
            } else if (const auto* transient =
                           std::get_if<TransientResult>(&analysis->solver)) {
                const auto stamps = assembleDcSystem(compiled_->circuit).stamps;
                for (const auto& sample : transient->samples)
                    frames.push_back(
                        {QString("t = %1 s").arg(sample.timeSeconds, 0, 'g', 9),
                         unknownLabels(transient->nodeNames,
                                       transient->voltageSourceNames),
                         sample.systemMatrix,
                         sample.rightHandSide,
                         sample.solution,
                         stamps,
                         sample.capacitors,
                         {},
                         true});
            } else {
                for (const auto& point :
                     std::get<DcSweepResult>(analysis->solver).points)
                    frames.push_back(nonlinearDiagnostics(
                        point.result,
                        QString("Source = %1 V")
                            .arg(point.sourceVoltage, 0, 'g', 9)));
            }
        }
        inspector_->setFrames(std::move(frames));
        displayTrace();
    }
    void displayTrace() {
        plot_->clear();
        scalar_->clear();
        const auto* analysis = selectedResult();
        const int index = traces_->currentIndex();
        if (!analysis || index < 0 ||
            static_cast<std::size_t>(index) >= analysis->observations.size())
            return;
        const auto& trace =
            analysis->observations[static_cast<std::size_t>(index)];
        const auto unit =
            QString::fromStdString(script::unitName(trace.value.dimension));
        const bool scalar = trace.value.scalar();
        scalar_->setVisible(scalar);
        plot_->setVisible(!scalar);
        if (scalar) {
            scalar_->setText(
                QString::fromStdString(trace.label) + " = " +
                QString::number(trace.value.data.front(), 'g', 12) + " " +
                unit);
        } else {
            WaveformTrace waveform{
                QString::fromStdString(trace.label), unit, {}};
            for (std::size_t sample = 0; sample < trace.value.data.size();
                 ++sample)
                waveform.points.emplace_back(analysis->axis[sample],
                                             trace.value.data[sample]);
            plot_->setTrace(waveform,
                            analysis->kind == script::AnalysisKind::transient
                                ? "Time (s)"
                                : "Source (V)");
        }
    }
    void replace(const QString& source, const QString& filename) {
        error_.clear();
        editor_->setPlainText(source);
        path_ = filename;
        editor_->document()->setModified(false);
        updateTitle();
    }
    void updateTitle() {
        setWindowTitle(
            "Script Workspace - " +
            (path_.isEmpty() ? "Untitled.msim" : QFileInfo(path_).fileName()) +
            (editor_->document()->isModified() ? " *" : ""));
    }
    QPlainTextEdit* editor_;
    QString path_, error_;
    QAction *undo_, *redo_, *compile_, *run_, *cancel_, *export_;
    QComboBox *analyses_, *results_, *traces_;
    QLineEdit* search_;
    QPlainTextEdit* summary_;
    QListWidget* diagnostics_;
    QLabel *status_, *scalar_;
    QProgressBar* progress_;
    WaveformPlot* plot_;
    SolverInspector* inspector_;
    HelpDialog* guide_ = nullptr;
    std::optional<script::CompiledSimulation> compiled_;
    std::optional<script::RunResult> result_;
    std::vector<script::Diagnostic> diagnosticData_;
    std::size_t revision_ = 0, jobRevision_ = 0;
    bool discard_ = false;
    BackgroundJob<ScriptOutcome> job_;
};

}  // namespace microspice::gui
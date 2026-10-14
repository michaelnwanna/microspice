#include "script_workspace.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QThread>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "project_window.hpp"

using namespace microspice::gui;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void waitForJob(ScriptWorkspace& workspace) {
    QElapsedTimer timer;
    timer.start();
    while (workspace.busy() && timer.elapsed() < 10000)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    expect(!workspace.busy(), "background job completes within timeout");
}

void writeFile(const QString& filename, const QByteArray& source) {
    QFile file(filename);
    expect(file.open(QIODevice::WriteOnly), "create fixture");
    expect(file.write(source) == source.size(), "write fixture");
}

QByteArray readFile(const QString& filename) {
    QFile file(filename);
    expect(file.open(QIODevice::ReadOnly), "read fixture");
    return file.readAll();
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    applyDesktopStyle(application);
    QTextDocument syntax;
    ScriptHighlighter highlighter(&syntax);
    syntax.setPlainText(
        "# comment\nparam R = 1kohm\nsubcircuit divider(a) {\nresistor(\"# "
        "value\", a, ground, R)\n}");
    highlighter.rehighlight();
    const auto tokenColor = [&syntax](int line, int offset) {
        for (const auto& range :
             syntax.findBlockByNumber(line).layout()->formats())
            if (range.start <= offset && offset < range.start + range.length)
                return range.format.foreground().color();
        return QColor();
    };
    expect(tokenColor(0, 0) == QColor("#63736b") &&
               tokenColor(1, 0) == QColor("#9a2955") &&
               tokenColor(2, 0) == QColor("#9a2955") &&
               tokenColor(3, 0) == QColor("#007f78") &&
               tokenColor(3, 10) == QColor("#9b4a2b") &&
               tokenColor(1, 10) == QColor("#226796") &&
               tokenColor(1, 13) == QColor("#226796"),
           "syntax styles comments, strings, calls, param/subcircuit, numbers "
           "and units");
    QTemporaryDir directory;
    ScriptWorkspace workspace;
    workspace.editor()->insertPlainText("dc(\"bias\")\n");
    workspace.decisionProvider = [] {
        return ScriptWorkspace::Decision::cancel;
    };
    const auto source = workspace.editor()->toPlainText();
    expect(!workspace.openFile(directory.filePath("missing.msim")),
           "missing open fails");
    expect(!workspace.saveAs(directory.filePath("missing/file.msim")),
           "failed save");
    expect(workspace.editor()->toPlainText() == source &&
               workspace.editor()->document()->isModified() &&
               workspace.filePath().isEmpty(),
           "failed IO preserves source, path and modified state");
    expect(!workspace.newDocument() && !workspace.canClose(), "cancel guard");
    expect(workspace.saveAs(directory.filePath("saved.msim")), "atomic save");
    expect(!workspace.editor()->document()->isModified(),
           "save clears modified");
    workspace.editor()->insertPlainText("# dirty\n");
    workspace.decisionProvider = [] {
        return ScriptWorkspace::Decision::discard;
    };
    expect(workspace.canClose() && workspace.editor()->document()->isModified(),
           "parent preflight does not discard document");
    expect(workspace.openFile(directory.filePath("saved.msim")),
           "reopen saved source");
    expect(workspace.editor()->toPlainText() == source, "saved UTF8 roundtrip");
    workspace.editor()->insertPlainText("# dirty\n");
    workspace.show();
    workspace.decisionProvider = [] {
        return ScriptWorkspace::Decision::cancel;
    };
    workspace.reject();
    expect(workspace.isVisible(), "reject cannot bypass guard");
    workspace.decisionProvider = [] {
        return ScriptWorkspace::Decision::discard;
    };
    workspace.close();
    expect(
        !workspace.isVisible() && !workspace.editor()->document()->isModified(),
        "discarded close does not reprompt parent");

    workspace.editor()->insertPlainText("# retained\n");
    const auto retained = workspace.editor()->toPlainText();
    const auto retainedPath = workspace.filePath();
    workspace.decisionProvider = [] {
        return ScriptWorkspace::Decision::cancel;
    };
    expect(!workspace.openFile(directory.filePath("saved.msim")) &&
               !workspace.openExample("rc_charging") &&
               workspace.editor()->toPlainText() == retained &&
               workspace.filePath() == retainedPath,
           "cancel on valid open and example preserves current source");
    workspace.decisionProvider = [] {
        return ScriptWorkspace::Decision::discard;
    };
    writeFile(directory.filePath("invalid.msim"), QByteArray("\xc3", 1));
    writeFile(directory.filePath("oversize.msim"),
              QByteArray(ScriptWorkspace::sourceLimit + 1, '#'));
    expect(!workspace.openFile(directory.filePath("invalid.msim")),
           "truncated UTF8 rejected");
    expect(!workspace.openFile(directory.filePath("oversize.msim")),
           "oversize source rejected");
    expect(workspace.editor()->toPlainText() == retained &&
               workspace.filePath() == retainedPath &&
               workspace.editor()->document()->isModified(),
           "invalid source preserves document");

    for (const auto& name : ScriptWorkspace::exampleNames()) {
        expect(workspace.openExample(name), "embedded starter opens");
        expect(workspace.filePath().isEmpty() &&
                   workspace.editor()->document()->isModified(),
               "starter is an unsaved editable copy");
        const auto original = readFile(":/scripts/" + name + ".msim");
        workspace.editor()->insertPlainText("# edited copy\n");
        expect(readFile(":/scripts/" + name + ".msim") == original,
               "embedded source is immutable");
        expect(workspace.openExample(name) && workspace.compileScript(),
               "compile embedded copy");
        waitForJob(workspace);
        expect(workspace.sourceDiagnostics().empty(),
               "starter compiles without warnings");
        expect(workspace.analysisSelector()->count() >= 2,
               "compiled named analysis selector");
    }
    HelpDialog guide(nullptr, ":/help/simulation-language.md");
    expect(guide.content().contains("subcircuit") && guide.topicCount() > 0,
           "embedded simulation guide");

    expect(workspace.openExample("voltage_divider") && workspace.runScript(),
           "run divider asynchronously");
    waitForJob(workspace);
    expect(workspace.runResult() &&
               workspace.runResult()->analyses.size() == 1 &&
               workspace.runResult()->measurements.size() == 1,
           "divider native results retained");
    expect(std::abs(workspace.runResult()->measurements[0].value.value - 5) <
                   1e-10 &&
               workspace.summaryText().contains("PASS: divider output"),
           "divider measurement and check");
    expect(workspace.scalarText().contains("5 V") &&
               workspace.plot()->pointCount() == 0 &&
               workspace.inspector()->frameCount() == 1,
           "DC scalar and native inspector");
    expect(workspace.exportEnabled() &&
               workspace.exportCsv(directory.filePath("dc.csv")),
           "DC CSV export");
    expect(readFile(directory.filePath("dc.csv")).startsWith("sample,"),
           "DC CSV header");
    workspace.traceSelector()->setCurrentIndex(1);
    expect(workspace.scalarText().contains("current(V1)") &&
               workspace.scalarText().contains("A"),
           "DC trace selection");
    const auto beforeUndo = workspace.editor()->toPlainText();
    workspace.editor()->insertPlainText("# edit\n");
    expect(!workspace.runResult() && !workspace.exportEnabled() &&
               workspace.inspector()->frameCount() == 0,
           "source editing clears results and export");
    const auto undoKey = QKeySequence(QKeySequence::Undo)[0];
    QKeyEvent undoEvent(QEvent::KeyPress, static_cast<int>(undoKey.key()),
                        undoKey.keyboardModifiers());
    QApplication::sendEvent(workspace.editor(), &undoEvent);
    expect(workspace.editor()->toPlainText() == beforeUndo,
           "native editor undo");
    const auto redoKey = QKeySequence(QKeySequence::Redo)[0];
    QKeyEvent redoEvent(QEvent::KeyPress, static_cast<int>(redoKey.key()),
                        redoKey.keyboardModifiers());
    QApplication::sendEvent(workspace.editor(), &redoEvent);
    expect(workspace.editor()->toPlainText() != beforeUndo,
           "native editor redo");

    expect(workspace.openExample("rc_charging") && workspace.runScript(),
           "run RC");
    waitForJob(workspace);
    expect(workspace.runResult() && workspace.plot()->pointCount() == 50 &&
               workspace.inspector()->frameCount() == 50,
           "RC native plot and retained diagnostic frames");
    expect(workspace.inspector()->capacitorTable().rowCount() == 1 &&
               workspace.inspector()->systemTable().rowCount() == 3,
           "RC capacitor and matrix diagnostics");
    expect(workspace.summaryText().contains("PASS:") &&
               workspace.exportCsv(directory.filePath("rc.csv")),
           "RC checks and CSV");
    expect(readFile(directory.filePath("rc.csv")).startsWith("time_s,"),
           "RC CSV time axis");
    workspace.traceSelector()->setCurrentIndex(1);
    expect(workspace.plot()->pointCount() == 50, "RC current trace plots");

    const auto multiple =
        workspace.editor()->toPlainText() +
        "\ndc(\"bias\")\nobserve(\"bias\", voltage(output))\n"
        "measure(\"bias_value\", from=\"bias\", value=voltage(output))\n";
    workspace.editor()->setPlainText(multiple);
    expect(workspace.compileScript(), "compile multiple analyses");
    waitForJob(workspace);
    expect(workspace.analysisSelector()->count() == 3,
           "all plus two named analyses");
    workspace.analysisSelector()->setCurrentIndex(
        workspace.analysisSelector()->findData("bias"));
    expect(workspace.runScript(), "run selected bias");
    waitForJob(workspace);
    expect(workspace.runResult() &&
               workspace.runResult()->analyses.size() == 1 &&
               workspace.runResult()->analyses[0].name == "bias" &&
               workspace.summaryText().contains("Skipped:"),
           "selected analysis skips dependent measurements and checks");
    workspace.analysisSelector()->setCurrentIndex(0);
    expect(workspace.runScript(), "run all analyses");
    waitForJob(workspace);
    expect(workspace.runResult() && workspace.resultSelector()->count() == 2,
           "all analyses retained");
    workspace.resultSelector()->setCurrentIndex(1);
    expect(workspace.exportCsv(directory.filePath("chosen.csv")) &&
               readFile(directory.filePath("chosen.csv")).startsWith("sample,"),
           "export chosen result, not first result");
    workspace.resultSelector()->setCurrentIndex(0);
    expect(workspace.exportCsv(directory.filePath("chosen.csv")) &&
               readFile(directory.filePath("chosen.csv")).startsWith("time_s,"),
           "atomic CSV replacement");

    workspace.editor()->setPlainText(
        "# source\nresistor(\"R1\", ground, output, 0ohm)\n");
    expect(workspace.compileScript(), "compile invalid source");
    waitForJob(workspace);
    expect(workspace.sourceDiagnostics().size() == 1 &&
               workspace.sourceDiagnostics()[0].span.line == 2 &&
               !workspace.sourceDiagnostics()[0].warning,
           "structured source diagnostic survives background job");
    workspace.navigateDiagnostic(0);
    expect(!workspace.editor()->textCursor().selectedText().isEmpty() &&
               workspace.editor()->extraSelections().size() == 1,
           "source diagnostic navigation and underline");
    expect(workspace.findSource("resistor") &&
               workspace.editor()->textCursor().selectedText() == "resistor",
           "source search");
    expect(!workspace.findSource("not present"), "source search miss");

    workspace.editor()->setPlainText(
        "# UTF8 comment: \xc3\xa9\nvoltage_source(\"V1\", supply, ground, "
        "10V)\n"
        "resistor(\"R1\", supply, typo, 1kohm)\ndc(\"bias\")\n");
    expect(workspace.compileScript(), "compile source warning");
    waitForJob(workspace);
    expect(workspace.sourceDiagnostics().size() == 1 &&
               workspace.sourceDiagnostics()[0].warning &&
               workspace.sourceDiagnostics()[0].span.line == 3,
           "source warning location");
    workspace.navigateDiagnostic(0);
    expect(workspace.editor()->textCursor().selectedText() == "typo",
           "UTF8 byte spans navigate UTF16 editor correctly");

    workspace.editor()->setPlainText(
        "voltage_source(\"V1\", floating, other, 10V)\ndc(\"singular\")\n");
    expect(workspace.runScript(), "run singular source");
    waitForJob(workspace);
    expect(
        !workspace.runResult() && workspace.sourceDiagnostics().size() >= 3 &&
            workspace.sourceDiagnostics().back().span.line == 2 &&
            !workspace.sourceDiagnostics().back().warning,
        "solver failure preserves topology warnings and analysis source span");

    workspace.editor()->setPlainText(
        QString(ScriptWorkspace::sourceLimit + 1, '#'));
    const auto originalCsv = readFile(directory.filePath("chosen.csv"));
    expect(!workspace.saveAs(directory.filePath("chosen.csv")) &&
               readFile(directory.filePath("chosen.csv")) == originalCsv,
           "oversize save does not overwrite an existing file");
    expect(workspace.compileScript(), "compile oversize editor source");
    waitForJob(workspace);
    expect(!workspace.sourceDiagnostics().empty() && !workspace.exportEnabled(),
           "CLI compiler source limit enforced");

    expect(workspace.openExample("voltage_divider") && workspace.runScript(),
           "start pending run");
    QThread::msleep(100);
    workspace.editor()->insertPlainText("# changed before publication\n");
    waitForJob(workspace);
    expect(!workspace.runResult() && !workspace.exportEnabled() &&
               workspace.summaryText().isEmpty(),
           "edit discards even completed but unpublished result");
    expect(workspace.runScript(), "restart after stale run");
    workspace.cancelJob();
    waitForJob(workspace);
    expect(
        !workspace.runResult() && workspace.statusText() == "Script cancelled",
        "cancel discards pending outcome");
    expect(workspace.runScript(), "run after cancellation");
    waitForJob(workspace);
    expect(workspace.runResult().has_value(), "workspace can run again");

    expect(workspace.openExample("diode_bias") && workspace.runScript(),
           "run diode sweep");
    waitForJob(workspace);
    workspace.resultSelector()->setCurrentIndex(1);
    workspace.inspector()->selectFrame(
        static_cast<int>(workspace.inspector()->frameCount()) - 1);
    expect(workspace.runResult() && workspace.inspector()->frameCount() > 1 &&
               workspace.inspector()->iterationTable().rowCount() > 0 &&
               workspace.inspector()->diodeTable().rowCount() == 1,
           "native sweep Newton and diode diagnostics");

    workspace.show();
    workspace.decisionProvider = [] {
        return ScriptWorkspace::Decision::cancel;
    };
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&workspace, &escape);
    expect(
        workspace.isVisible() && workspace.editor()->document()->isModified(),
        "Escape obeys unsaved guard");
    expect(!workspace.close(), "window close obeys unsaved guard");
    int saveRequests = 0;
    workspace.decisionProvider = [] { return ScriptWorkspace::Decision::save; };
    workspace.savePathProvider = [&] {
        ++saveRequests;
        return QString();
    };
    expect(!workspace.canClose() && saveRequests == 1 &&
               workspace.editor()->document()->isModified(),
           "cancel Save As keeps script dirty");
    workspace.savePathProvider = [&] {
        return directory.filePath("missing/save.msim");
    };
    expect(
        !workspace.canClose() && workspace.editor()->document()->isModified(),
        "save failure blocks close");
    workspace.savePathProvider = [&] {
        return directory.filePath("guard.msim");
    };
    expect(
        workspace.canClose() && !workspace.editor()->document()->isModified(),
        "save decision commits script");

    workspace.editor()->insertPlainText("# saved during reopen\n");
    const auto savedDuringOpen = workspace.editor()->toPlainText();
    expect(workspace.openFile(directory.filePath("guard.msim")) &&
               workspace.editor()->toPlainText() == savedDuringOpen &&
               readFile(directory.filePath("guard.msim")) ==
                   savedDuringOpen.toUtf8(),
           "reopening current file with Save uses newly saved source, not "
           "preflight bytes");

    ProjectWindow parent;
    auto* child = new ScriptWorkspace(&parent);
    child->editor()->insertPlainText("# independent document\n");
    bool schematicAccepts = false;
    int scriptPrompts = 0, schematicPrompts = 0;
    child->decisionProvider = [&] {
        ++scriptPrompts;
        return ScriptWorkspace::Decision::discard;
    };
    parent.allowClose = [&] {
        return child->canClose() && ([&] {
                   ++schematicPrompts;
                   return schematicAccepts;
               })();
    };
    parent.show();
    child->show();
    expect(!parent.close() && parent.isVisible() && child->isVisible() &&
               child->editor()->document()->isModified() &&
               scriptPrompts == 1 && schematicPrompts == 1,
           "schematic cancellation leaves script visible and dirty after "
           "script discard preflight");
    child->decisionProvider = [&] {
        ++scriptPrompts;
        return ScriptWorkspace::Decision::cancel;
    };
    expect(!parent.close() && schematicPrompts == 1,
           "script cancellation short circuits schematic guard");
    child->decisionProvider = [&] {
        ++scriptPrompts;
        return ScriptWorkspace::Decision::discard;
    };
    child->close();
    const auto closedPrompts = scriptPrompts;
    expect(!parent.close() && scriptPrompts == closedPrompts,
           "closed discarded script does not reprompt");
    child->show();
    expect(child->editor()->toPlainText() == "# independent document\n",
           "same workspace reused on reopen");
    schematicAccepts = true;
    expect(parent.close(), "parent closes only after both guards accept");

    if (application.arguments().contains("--capture")) {
        workspace.decisionProvider = [] {
            return ScriptWorkspace::Decision::discard;
        };
        expect(workspace.openExample("rc_charging"), "capture RC source");
        workspace.show();
        workspace.resize(1100, 760);
        const auto captureFrame = [&](int frame) {
            QApplication::processEvents();
            expect(workspace.grab().save(QString("demo-script-%1.png")
                                             .arg(frame, 2, 10, QChar('0'))),
                   "capture script workflow frame");
        };
        captureFrame(0);
        auto valueCursor = workspace.editor()->document()->find("1kohm");
        expect(!valueCursor.isNull(), "find editable resistance");
        workspace.editor()->setTextCursor(valueCursor);
        captureFrame(1);
        workspace.editor()->insertPlainText("900ohm");
        captureFrame(2);
        expect(workspace.compileScript(), "capture script validation");
        waitForJob(workspace);
        captureFrame(3);
        expect(workspace.runScript(), "capture edited RC execution");
        waitForJob(workspace);
        expect(workspace.runResult().has_value() &&
                   workspace.summaryText().contains("PASS"),
               "edited capture script produces successful check");
        captureFrame(4);
        const auto tabs = workspace.findChildren<QTabWidget*>();
        expect(tabs.size() >= 1, "workspace result tabs");
        tabs.front()->setCurrentIndex(1);
        captureFrame(5);
        QApplication::processEvents();
        expect(workspace.grab().save("script-workspace-desktop.png"),
               "capture desktop workspace");
        workspace.resize(760, 640);
        QApplication::processEvents();
        expect(workspace.grab().save("script-workspace-compact.png"),
               "capture compact workspace");
    }
}
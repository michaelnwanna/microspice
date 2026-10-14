#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include "analysis_panel.hpp"
#include "dc_analysis.hpp"
#include "editor_scene.hpp"
#include "inspector.hpp"
#include "microspice/dc_solver.hpp"
#include "microspice/netlist.hpp"
#include "microspice/nonlinear_solver.hpp"
#include "project_document.hpp"
#include "project_window.hpp"

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName("microspice");
    application.setApplicationVersion(MS_APP_VERSION);
    application.setOrganizationName("microspice");
    using namespace microspice::gui;

    std::optional<ProjectDocument> documentStorage;
    bool inspectorFromWaveform = false;
    ProjectWindow window;
    BackgroundJob<DcAnalysis> dcJob;
    bool discardDc = false;
    window.setWindowTitle("microspice - Voltage divider");
    window.resize(1100, 720);

    auto* scene = new EditorScene(&window);
    scene->loadDivider();
    auto& document = documentStorage.emplace(*scene);
    auto* view = new SchematicView(scene, &window);
    view->setMouseTracking(true);
    window.setCentralWidget(view);

    auto* toolbar = window.addToolBar("Schematic");
    toolbar->setMovable(false);
    auto* title = new QLabel("  microspice  ");
    toolbar->addWidget(title);
    toolbar->addSeparator();
    const auto addAction = [&](const QString& name, QStyle::StandardPixmap icon,
                               const auto& handler) {
        QAction* action =
            toolbar->addAction(window.style()->standardIcon(icon), name);
        action->setToolTip(name);
        QObject::connect(action, &QAction::triggered, &window, handler);
        return action;
    };
    auto* tools = new QActionGroup(&window);
    const auto selectTool = [&](QAction* action,
                                std::optional<ComponentKind> kind, bool wire) {
        action->setCheckable(true);
        tools->addAction(action);
        QObject::connect(action, &QAction::triggered, &window, [=, &window] {
            scene->setTool(kind, wire);
            view->setDragMode(kind || wire ? QGraphicsView::NoDrag
                                           : QGraphicsView::RubberBandDrag);
            view->viewport()->setCursor(kind || wire ? Qt::CrossCursor
                                                     : Qt::ArrowCursor);
            window.statusBar()->showMessage(action->toolTip());
        });
    };
    auto* select = toolbar->addAction(
        window.style()->standardIcon(QStyle::SP_ArrowForward), "Select");
    select->setToolTip("Select");
    selectTool(select, std::nullopt, false);
    select->setChecked(true);
    auto* probe = toolbar->addAction(
        window.style()->standardIcon(QStyle::SP_DialogApplyButton), "Probe");
    probe->setToolTip(
        "Probe voltage on terminals/wires or current on a voltage-source body");
    probe->setCheckable(true);
    tools->addAction(probe);
    QObject::connect(probe, &QAction::triggered, &window, [scene, view] {
        scene->setProbeTool();
        view->setDragMode(QGraphicsView::NoDrag);
        view->viewport()->setCursor(Qt::CrossCursor);
    });
    const std::array<QString, 6> toolNames = {
        "Resistor",       "Capacitor",      "Diode",
        "Voltage source", "Current source", "Ground"};
    for (std::size_t index = 0; index < toolNames.size(); ++index) {
        const auto kind = static_cast<ComponentKind>(index);
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(16, 16);
        painter.scale(0.23, 0.23);
        ComponentItem symbol(kind, "");
        QStyleOptionGraphicsItem option;
        symbol.paint(&painter, &option, nullptr);
        painter.end();
        auto* action = toolbar->addAction(QIcon(pixmap), toolNames[index]);
        action->setToolTip(toolNames[index]);
        selectTool(action, kind, false);
    }
    auto* wire = toolbar->addAction(
        window.style()->standardIcon(QStyle::SP_FileLinkIcon), "Wire");
    wire->setToolTip("Wire");
    selectTool(wire, std::nullopt, true);
    toolbar->addSeparator();
    addAction("Rotate", QStyle::SP_BrowserReload,
              [scene] { scene->rotateSelected(); });
    addAction("Delete", QStyle::SP_TrashIcon, [scene] {
        scene->removeSelected();
    })->setShortcut(QKeySequence::Delete);
    addAction("Zoom in", QStyle::SP_ArrowUp, [view] { view->zoomBy(1.25); });
    addAction("Zoom out", QStyle::SP_ArrowDown, [view] { view->zoomBy(0.8); });
    addAction("Reset view", QStyle::SP_DesktopIcon,
              [view] { view->fitSchematic(); });

    auto* properties = new QDockWidget("Component", &window);
    properties->setFeatures(QDockWidget::DockWidgetMovable);
    auto* panel = new QWidget(properties);
    auto* form = new QFormLayout(panel);
    auto* reference = new QLabel(panel);
    auto* valueLabel = new QLabel("Value", panel);
    auto* value = new QLineEdit(panel);
    auto* ideality = new QDoubleSpinBox(panel);
    ideality->setRange(0.01, 100);
    ideality->setDecimals(3);
    ideality->setKeyboardTracking(false);
    auto* idealityLabel = new QLabel("Ideality", panel);
    form->addRow("Reference", reference);
    form->addRow(valueLabel, value);
    form->addRow(idealityLabel, ideality);
    panel->setEnabled(false);
    panel->setMinimumWidth(240);
    properties->setWidget(panel);
    window.addDockWidget(Qt::RightDockWidgetArea, properties);

    auto* resultsDock = new QDockWidget("DC operating point", &window);
    auto* results = new QPlainTextEdit(resultsDock);
    results->setReadOnly(true);
    results->setMinimumWidth(240);
    resultsDock->setWidget(results);
    window.addDockWidget(Qt::RightDockWidgetArea, resultsDock);
    const auto selectedComponent = [scene]() -> ComponentItem* {
        const auto selected = scene->selectedItems();
        return selected.size() == 1
                   ? dynamic_cast<ComponentItem*>(selected.front())
                   : nullptr;
    };
    const auto refreshProperties = [=] {
        auto* component = selectedComponent();
        panel->setEnabled(component != nullptr);
        reference->setText(component ? component->name() : QString());
        value->setText(component ? formatEngineeringValue(component->value())
                                 : QString());
        const bool ground =
            component && component->kind() == ComponentKind::Ground;
        const bool diode =
            component && component->kind() == ComponentKind::Diode;
        value->setEnabled(component && !ground);
        valueLabel->setText(diode       ? "Saturation (A)"
                            : component ? "Value (" + component->unit() + ")"
                                        : "Value");
        idealityLabel->setVisible(diode);
        ideality->setVisible(diode);
        if (diode) {
            const QSignalBlocker blocker(ideality);
            ideality->setValue(component->ideality());
        }
    };
    QObject::connect(scene, &QGraphicsScene::selectionChanged, panel,
                     refreshProperties);
    scene->edited = [results] { results->clear(); };
    QObject::connect(
        value, &QLineEdit::editingFinished, &window,
        [&window, value, selectedComponent, refreshProperties] {
            auto* component = selectedComponent();
            if (!component || component->kind() == ComponentKind::Ground) {
                return;
            }
            try {
                const double parsed = parseGuiEngineeringValue(value->text());
                const bool signedValue =
                    component->kind() == ComponentKind::Voltage ||
                    component->kind() == ComponentKind::Current;
                if (!signedValue && parsed <= 0) {
                    throw std::invalid_argument(
                        "Value must be greater than zero");
                }
                component->setValue(parsed);
            } catch (const std::exception& error) {
                QMessageBox::warning(&window, "Invalid value",
                                     QString::fromUtf8(error.what()));
            }
            refreshProperties();
        });
    QObject::connect(ideality, qOverload<double>(&QDoubleSpinBox::valueChanged),
                     &window, [=](double number) {
                         if (auto* component = selectedComponent()) {
                             component->setIdeality(number);
                         }
                     });
    refreshProperties();

    auto* waveformDock = new QDockWidget("Waveforms", &window);
    auto* analyses = new AnalysisPanel(*scene, waveformDock);
    waveformDock->setWidget(analyses);
    window.addDockWidget(Qt::BottomDockWidgetArea, waveformDock);
    waveformDock->hide();
    auto* inspectorDock = new QDockWidget("Solver inspector", &window);
    auto* inspector = new SolverInspector(inspectorDock);
    inspectorDock->setWidget(inspector);
    window.addDockWidget(Qt::BottomDockWidgetArea, inspectorDock);
    window.tabifyDockWidget(waveformDock, inspectorDock);
    inspectorDock->hide();
    analyses->diagnosticsChanged =
        [inspector, &inspectorFromWaveform](const auto& frames) {
            if (!frames.empty() || inspectorFromWaveform) {
                inspector->setFrames(frames);
            }
            if (!frames.empty()) {
                inspectorFromWaveform = true;
            }
        };
    scene->probed = [analyses, waveformDock, scene,
                     &window](const QString& key) {
        if (analyses->selectProbe(key)) {
            waveformDock->show();
            waveformDock->raise();
        }
        window.statusBar()->showMessage(
            scene->probeReading().isEmpty() ? key : scene->probeReading());
    };
    scene->edited = [results, analyses, inspector, &dcJob, &discardDc] {
        discardDc = true;
        dcJob.cancel();
        results->clear();
        analyses->invalidate();
        inspector->clear();
    };

    const auto publishDc =
        [scene, results, inspector, &window,
         &inspectorFromWaveform](const DcAnalysis& analysis) {
            inspectorFromWaveform = false;
            const auto& extracted = analysis.extracted;
            const auto& nonlinear = analysis.solved;
            inspector->setFrames(
                {nonlinearDiagnostics(nonlinear, "DC operating point")});
            if (!nonlinear.converged) {
                results->setPlainText("Newton-Raphson did not converge");
                window.statusBar()->showMessage("DC analysis failed");
                return false;
            }
            const auto& result = nonlinear.operatingPoint;
            scene->setProbeReadings(result);
            QString output;
            for (const auto& entry : extracted.terminals) {
                for (std::size_t terminal = 0; terminal < entry.second.size();
                     ++terminal) {
                    const auto& node = entry.second[terminal];
                    output += QString("%1.%2  [%3]  %4 V\n")
                                  .arg(entry.first)
                                  .arg(static_cast<qulonglong>(terminal + 1))
                                  .arg(QString::fromStdString(node))
                                  .arg(result.voltage(node), 0, 'g', 9);
                }
            }
            for (const auto& source : result.voltageSourceNames) {
                output += QString("\nI(%1) = %2 A")
                              .arg(QString::fromStdString(source))
                              .arg(result.currentThrough(source), 0, 'g', 9);
            }
            output +=
                QString("\n\nMNA unknowns: %1\nNewton iterations: %2")
                    .arg(static_cast<qulonglong>(result.systemMatrix.size()))
                    .arg(static_cast<qulonglong>(nonlinear.iterations.size()));
            results->setPlainText(output);
            window.statusBar()->showMessage("DC analysis complete");
            return true;
        };
    const auto runDc = [scene, results, inspector, publishDc, &window]() {
        scene->clearProbeReadings();
        inspector->clear();
        try {
            return publishDc(analyzeDc(scene->extract()));
        } catch (const std::exception& error) {
            results->setPlainText(QString::fromUtf8(error.what()));
            window.statusBar()->showMessage("DC analysis failed");
            return false;
        }
    };
    toolbar->addSeparator();
    auto* run = addAction("Run DC", QStyle::SP_MediaPlay, [&] {
        if (dcJob.running() || analyses->busy()) {
            return;
        }
        scene->clearProbeReadings();
        inspector->clear();
        results->clear();
        discardDc = false;
        try {
            auto extracted = scene->extract();
            dcJob.start(
                [extracted = std::move(extracted)](
                    const microspice::ExecutionControl& execution) mutable {
                    return analyzeDc(std::move(extracted), execution);
                });
            analyses->setExternalBusy(true);
            window.statusBar()->showMessage("Running DC analysis");
        } catch (const std::exception& error) {
            results->setPlainText(QString::fromUtf8(error.what()));
        }
    });
    run->setShortcut(QKeySequence(Qt::Key_F5));
    auto* cancelDc = addAction("Cancel DC analysis", QStyle::SP_MediaStop, [&] {
        dcJob.cancel();
        window.statusBar()->showMessage("Cancelling DC analysis");
    });
    cancelDc->setEnabled(false);
    QObject::connect(run, &QAction::triggered, &window,
                     [run, cancelDc, &dcJob] {
                         if (dcJob.running()) {
                             run->setEnabled(false);
                             cancelDc->setEnabled(true);
                         }
                     });
    dcJob.finished = [&, run, cancelDc](std::optional<DcAnalysis> result,
                                        const QString& error) {
        run->setEnabled(true);
        cancelDc->setEnabled(false);
        analyses->setExternalBusy(false);
        if (discardDc) {
            window.statusBar()->clearMessage();
            return;
        }
        if (result) {
            publishDc(*result);
        } else {
            results->setPlainText(error);
            window.statusBar()->showMessage(error);
        }
    };
    analyses->busyChanged = [run](bool busy) { run->setEnabled(!busy); };
    auto* analysisMenu = window.menuBar()->addMenu("&Analysis");
    analysisMenu->addAction(run);
    analysisMenu->addAction(cancelDc);
    const auto showAnalysis = [waveformDock, analyses, view,
                               scene](bool sweep) {
        const bool wasHidden = waveformDock->isHidden();
        analyses->setMode(sweep);
        waveformDock->show();
        waveformDock->raise();
        if (wasHidden) {
            QTimer::singleShot(0, view, [view, scene] {
                if (!scene->components().empty()) {
                    view->fitInView(
                        scene->itemsBoundingRect().adjusted(-30, -30, 30, 30),
                        Qt::KeepAspectRatio);
                }
            });
        }
    };
    auto* transientAction = analysisMenu->addAction("&Transient...");
    QObject::connect(transientAction, &QAction::triggered, &window,
                     [showAnalysis] { showAnalysis(false); });
    auto* sweepAction = analysisMenu->addAction("DC &Sweep...");
    QObject::connect(sweepAction, &QAction::triggered, &window,
                     [showAnalysis] { showAnalysis(true); });
    analysisMenu->addAction(waveformDock->toggleViewAction());
    auto* inspectAction = analysisMenu->addAction("Solver &Inspector");
    QObject::connect(inspectAction, &QAction::triggered, &window,
                     [inspectorDock] {
                         inspectorDock->show();
                         inspectorDock->raise();
                     });
    const auto updateTitle = [&window, &document] {
        window.setWindowTitle("microspice - " + document.title() + "[*]");
        window.setWindowModified(document.modified());
    };
    QObject::connect(&scene->history(), &QUndoStack::cleanChanged, &window,
                     [updateTitle](bool) { updateTitle(); });
    const QString projectFilter =
        "microspice projects (*.microspice);;JSON files (*.json)";
    const auto saveProject = [&window, &document, updateTitle,
                              projectFilter](bool saveAs) {
        QString filename = document.filename();
        if (saveAs || filename.isEmpty()) {
            filename = QFileDialog::getSaveFileName(&window, "Save schematic",
                                                    filename, projectFilter);
            if (filename.isEmpty()) {
                return false;
            }
            if (QFileInfo(filename).suffix().isEmpty()) {
                filename += ".microspice";
            }
        }
        try {
            document.saveTo(filename);
            updateTitle();
            window.statusBar()->showMessage("Project saved");
            return true;
        } catch (const std::exception& error) {
            QMessageBox::warning(&window, "Cannot save project",
                                 QString::fromUtf8(error.what()));
            return false;
        }
    };
    const auto confirmReplace = [&window, &document, saveProject] {
        return document.allowReplace(
            [&window] {
                const auto choice = QMessageBox::warning(
                    &window, "Unsaved changes",
                    "Save changes to the current schematic?",
                    QMessageBox::Save | QMessageBox::Discard |
                        QMessageBox::Cancel,
                    QMessageBox::Save);
                if (choice == QMessageBox::Save) {
                    return UnsavedChoice::Save;
                }
                return choice == QMessageBox::Discard ? UnsavedChoice::Discard
                                                      : UnsavedChoice::Cancel;
            },
            [saveProject] { return saveProject(false); });
    };
    window.allowClose = confirmReplace;

    auto* fileMenu = window.menuBar()->addMenu("&File");
    auto* newProject = addAction(
        "New schematic", QStyle::SP_FileIcon,
        [&document, select, view, analyses, updateTitle, confirmReplace] {
            if (!confirmReplace()) {
                return;
            }
            select->trigger();
            document.newProject();
            analyses->resetInitialConditions();
            view->fitSchematic();
            updateTitle();
        });
    newProject->setShortcut(QKeySequence::New);
    fileMenu->addAction(newProject);
    auto* openProject = addAction(
        "Open project", QStyle::SP_DialogOpenButton,
        [&window, &document, select, view, analyses, updateTitle,
         confirmReplace, projectFilter] {
            const QString filename = QFileDialog::getOpenFileName(
                &window, "Open schematic", document.filename(), projectFilter);
            if (filename.isEmpty() || !confirmReplace()) {
                return;
            }
            try {
                document.open(filename);
                analyses->resetInitialConditions();
                select->trigger();
                view->fitSchematic();
                updateTitle();
                window.statusBar()->showMessage("Project opened");
            } catch (const std::exception& error) {
                QMessageBox::warning(&window, "Cannot open project",
                                     QString::fromUtf8(error.what()));
            }
        });
    openProject->setShortcut(QKeySequence::Open);
    fileMenu->addAction(openProject);
    auto* save = addAction("Save project", QStyle::SP_DialogSaveButton,
                           [saveProject] { saveProject(false); });
    save->setShortcut(QKeySequence::Save);
    fileMenu->addAction(save);
    auto* saveAs = fileMenu->addAction("Save &As...");
    saveAs->setShortcut(QKeySequence::SaveAs);
    QObject::connect(saveAs, &QAction::triggered, &window,
                     [saveProject] { saveProject(true); });
    fileMenu->addSeparator();
    auto* quit = fileMenu->addAction("&Quit");
    quit->setShortcut(QKeySequence::Quit);
    QObject::connect(quit, &QAction::triggered, &window,
                     [&window] { window.close(); });

    auto* editMenu = window.menuBar()->addMenu("&Edit");
    auto* undo = scene->history().createUndoAction(&window, "Undo");
    undo->setIcon(window.style()->standardIcon(QStyle::SP_ArrowBack));
    undo->setShortcuts(QKeySequence::Undo);
    editMenu->addAction(undo);
    toolbar->addAction(undo);
    auto* redo = scene->history().createRedoAction(&window, "Redo");
    redo->setIcon(window.style()->standardIcon(QStyle::SP_ArrowForward));
    redo->setShortcuts(QKeySequence::Redo);
    editMenu->addAction(redo);
    toolbar->addAction(redo);
    QObject::connect(&scene->history(), &QUndoStack::indexChanged, panel,
                     [refreshProperties](int) { refreshProperties(); });
    updateTitle();

    window.statusBar()->showMessage("Voltage divider");
    window.show();
    view->fitSchematic();
    if (application.arguments().contains("--smoke-test")) {
        QTimer::singleShot(0, &window, [&] {
            ComponentItem* firstResistor = nullptr;
            ComponentItem* secondResistor = nullptr;
            for (auto* item : scene->items()) {
                if (auto* component = dynamic_cast<ComponentItem*>(item)) {
                    if (component->name() == "R1") {
                        firstResistor = component;
                    }
                    if (component->name() == "R2") {
                        secondResistor = component;
                    }
                }
            }
            if (!firstResistor || !secondResistor) {
                application.exit(1);
                return;
            }
            firstResistor->setSelected(true);
            const auto waitForDc = [&] {
                QEventLoop loop;
                QTimer poll;
                QTimer deadline;
                deadline.setSingleShot(true);
                QObject::connect(&deadline, &QTimer::timeout, &loop,
                                 &QEventLoop::quit);
                QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                    if (!dcJob.running()) {
                        loop.quit();
                    }
                });
                deadline.start(5000);
                poll.start(5);
                loop.exec();
            };
            run->trigger();
            const bool dcStarted = dcJob.running();
            waitForDc();
            const bool dcBackground = dcStarted && !dcJob.running() &&
                                      results->toPlainText().contains("5 V");
            run->trigger();
            cancelDc->trigger();
            waitForDc();
            const bool dcCancelled =
                results->toPlainText().contains("cancelled");
            run->trigger();
            firstResistor->setValue(1100);
            waitForDc();
            const bool dcDiscarded = results->toPlainText().isEmpty() &&
                                     inspector->frameCount() == 0;
            firstResistor->setValue(1000);
            const bool solved = dcBackground && dcCancelled && dcDiscarded &&
                                runDc() &&
                                results->toPlainText().contains("5 V");
            const bool dcInspected = inspector->frameCount() == 1 &&
                                     inspector->systemTable().rowCount() == 3;
            probe->trigger();
            const bool dcProbed = !scene->probeAt({120, -120}).isEmpty() &&
                                  scene->probeReading().contains("5 V");
            select->trigger();
            const bool desktop =
                window.grab().save(QDir::current().filePath("gui-desktop.png"));
            window.resize(720, 480);
            application.processEvents();
            const bool compact =
                window.grab().save(QDir::current().filePath("gui-compact.png"));
            value->setText("10000");
            QMetaObject::invokeMethod(value, "editingFinished",
                                      Qt::DirectConnection);
            const bool kiloFormatted = firstResistor->value() == 10000 &&
                                       value->text() == "10K" &&
                                       firstResistor->valueText() == "10K ohm";
            value->setText("1M");
            QMetaObject::invokeMethod(value, "editingFinished",
                                      Qt::DirectConnection);
            const bool megaFormatted =
                firstResistor->value() == 1e6 && value->text() == "1M";
            value->setText("2k");
            QMetaObject::invokeMethod(value, "editingFinished",
                                      Qt::DirectConnection);
            const bool propertyEdited =
                kiloFormatted && megaFormatted && value->text() == "2K" &&
                firstResistor->value() == 2000 &&
                results->toPlainText().isEmpty() && runDc() &&
                results->toPlainText().contains("3.33333333 V");
            scene->clearSelection();
            secondResistor->setSelected(true);
            scene->removeSelected();
            auto* diode = scene->place(ComponentKind::Diode, {240, -120});
            diode->setSelected(true);
            const bool nonlinear = runDc() && !results->toPlainText().contains(
                                                  "Newton iterations: 0");
            scene->reset();
            const bool missingGround =
                !runDc() && results->toPlainText().contains("ground reference");
            scene->loadDivider();
            scene->findComponent("R1")->setSelected(true);
            value->setText("4.7k");
            QMetaObject::invokeMethod(value, "editingFinished",
                                      Qt::DirectConnection);
            const bool dirtyTitle = window.isWindowModified();
            undo->trigger();
            const bool undoWorked =
                scene->findComponent("R1")->value() == 1000 && runDc();
            redo->trigger();
            const bool redoWorked = scene->findComponent("R1")->value() == 4700;
            QTemporaryDir directory;
            bool persisted = false;
            try {
                document.saveTo(directory.filePath("smoke.microspice"));
                const bool cleanTitle = !window.isWindowModified();
                document.open(document.filename());
                persisted =
                    directory.isValid() && cleanTitle && !document.modified() &&
                    scene->findComponent("R1")->value() == 4700 && runDc();
            } catch (const std::exception&) {
                persisted = false;
            }
            scene->loadDivider();
            scene->findComponent("R2")->setSelected(true);
            scene->removeSelected();
            scene->place(ComponentKind::Capacitor, {240, -120})->setValue(1e-6);
            window.resize(1100, 900);
            transientAction->trigger();
            analyses->setTransient("100u", "5m");
            const bool transientWorked =
                analyses->runAnalysis() &&
                analyses->data().traces.front().points.size() == 50;
            const bool transientInspected =
                inspector->frameCount() == 50 &&
                inspector->capacitorTable().rowCount() == 1;
            const bool waveformProbed =
                analyses->selectProbe(scene->probeAt({120, -120}));
            application.processEvents();
            const bool waveformDesktop = window.grab().save(
                QDir::current().filePath("gui-waveform-desktop.png"));
            window.resize(760, 720);
            application.processEvents();
            view->fitInView(
                scene->itemsBoundingRect().adjusted(-30, -30, 30, 30),
                Qt::KeepAspectRatio);
            const bool waveformCompact = window.grab().save(
                QDir::current().filePath("gui-waveform-compact.png"));
            sweepAction->trigger();
            analyses->setSweep("V1", "0", "10", "1");
            const bool sweepWorked =
                analyses->runAnalysis() &&
                analyses->data().traces.front().points.size() == 11;
            const bool sweepInspected = inspector->frameCount() == 11;
            inspector->selectFrame(10);
            inspectAction->trigger();
            application.processEvents();
            const bool inspectorCaptured = window.grab().save(
                QDir::current().filePath("gui-inspector.png"));
            bool csvExported = false;
            try {
                analyses->exportCsv(directory.filePath("sweep.csv"));
                QFile exported(directory.filePath("sweep.csv"));
                csvExported = exported.open(QIODevice::ReadOnly) &&
                              exported.readAll() == analyses->data().csv;
            } catch (const std::exception&) {
                csvExported = false;
            }
            scene->findComponent("R1")->setValue(2000);
            const bool staleCleared = analyses->data().csv.isEmpty() &&
                                      analyses->plot().pointCount() == 0;
            application.exit(
                solved && desktop && compact && propertyEdited && nonlinear &&
                        missingGround && dirtyTitle && undoWorked &&
                        redoWorked && persisted && transientWorked &&
                        sweepWorked && csvExported && staleCleared &&
                        waveformDesktop && waveformCompact && dcInspected &&
                        dcProbed && transientInspected && waveformProbed &&
                        sweepInspected && inspectorCaptured &&
                        inspector->frameCount() == 0 &&
                        scene->probeReading().isEmpty()
                    ? 0
                    : 1);
        });
    }
    return application.exec();
}
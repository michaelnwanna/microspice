#include <QApplication>
#include <QImage>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "editor_scene.hpp"
#include "microspice/dc_solver.hpp"

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        using namespace microspice::gui;
        const std::vector<std::pair<double, QString>> engineeringValues = {
            {0, "0"},     {10, "10"},      {10000, "10K"},  {2200, "2.2K"},
            {1e6, "1M"},  {4.7e6, "4.7M"}, {0.001, "1m"},   {4.7e-6, "4.7u"},
            {1e-9, "1n"}, {1e-12, "1p"},   {-0.002, "-2m"}, {-10000, "-10K"},
            {1e9, "1G"},  {1e12, "1T"}};
        for (const auto& entry : engineeringValues) {
            require(formatEngineeringValue(entry.first) == entry.second,
                    "values must display engineering prefixes instead of "
                    "scientific notation");
            require(std::abs(parseGuiEngineeringValue(entry.second) -
                             entry.first) <= std::abs(entry.first) * 1e-12,
                    "displayed engineering values must round-trip through GUI "
                    "input");
        }
        require(parseGuiEngineeringValue(" 1M ") == 1e6 &&
                    parseGuiEngineeringValue("1m") == 1e-3 &&
                    parseGuiEngineeringValue("2meg") == 2e6 &&
                    microspice::parseEngineeringValue("1M") == 1e-3,
                "GUI mega and milli must be distinct without changing SPICE "
                "netlist parsing");
        require(formatEngineeringValue(999999, 4) == "1M",
                "rounded values must promote their engineering prefix");
        ComponentItem formattedResistor(ComponentKind::Resistor);
        formattedResistor.setValue(10000);
        require(formattedResistor.valueText() == "10K ohm",
                "resistor label must use engineering notation");
        ComponentItem formattedCapacitor(ComponentKind::Capacitor);
        formattedCapacitor.setValue(4.7e-6);
        require(formattedCapacitor.valueText() == "4.7u F",
                "capacitor label must use a micro prefix");
        ComponentItem formattedSource(ComponentKind::Current);
        formattedSource.setValue(-0.002);
        require(formattedSource.valueText() == "-2m A",
                "source label must retain its sign and milli prefix");
        QGraphicsScene scene;
        scene.setSceneRect(-2000, -2000, 4000, 4000);
        auto* resistor = new microspice::gui::ResistorItem;
        scene.addItem(resistor);
        resistor->setPos(31, -29);
        require(resistor->pos() == QPointF(40, -20),
                "resistor must snap to grid");
        resistor->setSelected(true);
        require(scene.selectedItems().size() == 1,
                "resistor must be selectable");
        resistor->setResistance(2200);
        require(resistor->resistance() == 2200, "resistance must be editable");
        resistor->setResistance(-1);
        resistor->setResistance(std::numeric_limits<double>::infinity());
        require(resistor->resistance() == 2200,
                "invalid resistance must be rejected");

        microspice::gui::SchematicView view(&scene);
        view.resize(800, 600);
        view.show();
        application.processEvents();
        view.fitSchematic();
        const QPoint anchor(250, 200);
        const QPointF before = view.mapToScene(anchor);
        view.zoomBy(1.25, anchor);
        require(std::abs(view.transform().m11() - 1.25) < 1e-9,
                "zoom must change scale");
        require(QLineF(before, view.mapToScene(anchor)).length() < 2,
                "zoom must preserve the pointer anchor");
        view.zoomBy(1000);
        require(view.transform().m11() == 4, "maximum zoom must be bounded");
        view.zoomBy(0.0001);
        require(view.transform().m11() == 0.25, "minimum zoom must be bounded");
        view.zoomBy(-1);
        require(view.transform().m11() == 0.25, "invalid zoom must be ignored");
        view.fitSchematic();
        require(view.transform().m11() == 1, "reset must restore scale");

        QImage image(320, 240, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        scene.render(&painter, QRectF(0, 0, 320, 240),
                     resistor->sceneBoundingRect());
        painter.end();
        int inkPixels = 0;
        for (int row = 0; row < image.height(); ++row) {
            for (int column = 0; column < image.width(); ++column) {
                if (image.pixelColor(column, row).lightness() < 160) {
                    ++inkPixels;
                }
            }
        }
        require(inkPixels > 500, "schematic symbol must render nonblank");

        microspice::gui::EditorScene editor;
        editor.loadDivider();
        const auto extracted = editor.extract();
        const auto dc = microspice::solveDc(extracted.circuit);
        require(
            std::abs(dc.voltage(extracted.terminals.at("R1")[1]) - 5) < 1e-9,
            "drawn divider must solve to 5 V");
        auto* capacitor =
            editor.place(microspice::gui::ComponentKind::Capacitor, {411, 9});
        require(capacitor->pos() == QPointF(420, 0), "placement must snap");
        capacitor->setSelected(true);
        editor.rotateSelected();
        require(QLineF(capacitor->component().positive, QPointF(420, -60))
                        .length() < 1e-8,
                "rotation must transform terminal positions");
        editor.removeSelected();
        require(editor.components().size() == 4,
                "delete must remove only selection");
        int changes = 0;
        editor.edited = [&changes] { ++changes; };
        auto* currentSource =
            editor.place(microspice::gui::ComponentKind::Current, {600, 0});
        const int afterPlacement = changes;
        currentSource->setSelected(true);
        require(changes == afterPlacement,
                "selection must not invalidate analysis");
        currentSource->setValue(-0.002);
        require(changes > afterPlacement,
                "property changes must invalidate analysis");
        const int afterValue = changes;
        currentSource->setPos(600, 40);
        require(changes > afterValue, "dragging must invalidate analysis");
        editor.reset();
        require(editor.components().empty() && editor.wires().empty(),
                "new scene must be empty");

        microspice::gui::SchematicView editorView(&editor);
        editorView.resize(800, 600);
        editorView.show();
        application.processEvents();
        editorView.centerOn(0, 0);
        const auto click = [&](QPointF position) {
            const QPointF local = editorView.mapFromScene(position);
            const QPointF global =
                editorView.viewport()->mapToGlobal(local.toPoint());
            QMouseEvent press(QEvent::MouseButtonPress, local, global,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(editorView.viewport(), &press);
            QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(editorView.viewport(), &release);
        };
        editorView.setDragMode(QGraphicsView::NoDrag);
        editor.setTool(microspice::gui::ComponentKind::Voltage);
        click({0, 0});
        require(editor.components().size() == 1,
                "placement tool must handle a mouse click");
        const int beforeWireGesture = editor.history().index();
        editor.setTool(std::nullopt, true);
        click({-60, 0});
        click({-180, -120});
        require(editor.wires().size() == 2,
                "wire tool must create an orthogonal path");
        require(editor.history().index() == beforeWireGesture + 1,
                "a two-segment wire must be one undo step");
        editor.history().undo();
        require(editor.wires().empty(),
                "undo must remove the whole wire gesture");
        editor.history().redo();
        require(editor.wires().size() == 2,
                "redo must restore the whole wire gesture");
        click({60, 0});
        editor.cancelWire();
        require(editor.wires().size() == 2,
                "cancel must not commit a wire preview");
        editor.reset();
        editor.addWire({-100, 0}, {100, 0});
        editor.addWire({0, -100}, {0, 100});
        require(editor.junctions().empty(),
                "unconnected crossings must not show junction dots");
        editor.addWire({0, 0}, {100, 0});
        require(!editor.junctions().empty(),
                "an endpoint at a crossing must show a junction dot");
        editor.loadDivider();
        editor.setTool(std::nullopt);
        editorView.setDragMode(QGraphicsView::RubberBandDrag);
        editorView.centerOn(0, 0);
        editor.findComponent("R1")->setSelected(true);
        for (auto* item : editor.items()) {
            if (dynamic_cast<QGraphicsLineItem*>(item)) {
                item->setSelected(true);
            }
        }
        const auto beforeDrag = editor.project();
        const auto mouse = [&](QEvent::Type type, const QPointF& position,
                               Qt::MouseButton button,
                               Qt::MouseButtons buttons) {
            const QPointF local = editorView.mapFromScene(position);
            const QPointF global =
                editorView.viewport()->mapToGlobal(local.toPoint());
            QMouseEvent event(type, local, global, button, buttons,
                              Qt::NoModifier);
            QApplication::sendEvent(editorView.viewport(), &event);
        };
        mouse(QEvent::MouseButtonPress, {0, -120}, Qt::LeftButton,
              Qt::LeftButton);
        mouse(QEvent::MouseMove, {40, -160}, Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, {80, -200}, Qt::NoButton, Qt::LeftButton);
        require(editor.history().index() == 0,
                "a drag must not record intermediate mouse moves");
        mouse(QEvent::MouseButtonRelease, {80, -200}, Qt::LeftButton,
              Qt::NoButton);
        require(editor.findComponent("R1")->pos() == QPointF(80, -200),
                "mouse drag must move the selected component");
        require(editor.history().index() == 1,
                "a complete mouse drag must be one undo step");
        const auto dragged = editor.extract();
        require(std::abs(microspice::solveDc(dragged.circuit)
                             .voltage(dragged.terminals.at("R1")[1]) -
                         5) < 1e-9,
                "mouse-driven movement must preserve connectivity");
        editor.history().undo();
        require(editor.project() == beforeDrag,
                "undo mouse drag must restore complete routing");
        std::cout << "GUI checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
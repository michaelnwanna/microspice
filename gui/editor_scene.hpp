#pragma once

#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QUndoStack>
#include <array>
#include <functional>
#include <optional>
#include <tuple>

#include "edit_command.hpp"
#include "microspice/dc_solver.hpp"
#include "probes.hpp"
#include "project.hpp"
#include "schematic.hpp"
#include "wire_routing.hpp"

namespace microspice::gui {

class EditorScene final : public QGraphicsScene {
public:
    explicit EditorScene(QObject* parent = nullptr) : QGraphicsScene(parent) {
        setSceneRect(-2000, -2000, 4000, 4000);
        history_.setUndoLimit(100);
    }

    std::function<void()> edited;
    std::function<void(const QString&)> probed;

    void setProbeTool() {
        setTool(std::nullopt);
        probing_ = true;
    }
    void clearProbeReadings() {
        readings_.clear();
        probeText_.clear();
        update();
    }
    void setProbeReadings(const DcResult& result) {
        readings_.clear();
        readings_["V(0)"] = "0 V";
        for (const auto& node : result.nodeNames) {
            readings_[QString::fromStdString("V(" + node + ")")] =
                QString::number(result.voltage(node), 'g', 7) + " V";
        }
        for (const auto& source : result.voltageSourceNames) {
            readings_[QString::fromStdString("I(" + source + ")")] =
                QString::number(result.currentThrough(source), 'g', 7) + " A";
        }
        probeText_ = activeProbe_.isEmpty()
                         ? QString()
                         : activeProbe_ + " = " + readings_[activeProbe_];
        update();
    }

    QString probeAt(const QPointF& position) {
        const auto point = snap(position);
        const auto extracted = extract();
        QString trace;
        for (auto* item : items(position)) {
            if (const auto* component = dynamic_cast<ComponentItem*>(item)) {
                if (component->kind() == ComponentKind::Voltage &&
                    std::abs(component->mapFromScene(position).x()) < 25 &&
                    std::abs(component->mapFromScene(position).y()) < 25) {
                    trace = "I(" + component->name() + ")";
                    break;
                }
            }
        }
        if (trace.isEmpty()) {
            trace = voltageProbe(point, components(), wires(), extracted);
        }
        if (!trace.isEmpty()) {
            activeProbe_ = trace;
            probePosition_ = point;
            probeText_ = readings_.count(trace)
                             ? trace + " = " + readings_[trace]
                             : QString();
            update();
            if (probed) {
                probed(trace);
            }
        }
        return trace;
    }
    QString probeReading() const { return probeText_; }
    QUndoStack& history() { return history_; }

    ProjectState project() const {
        ProjectState state;
        state.counters = counts_;
        state.wires = wires();
        for (auto* item : items()) {
            if (const auto* component = dynamic_cast<ComponentItem*>(item)) {
                state.components.push_back(
                    {component->kind(), component->name(), component->pos(),
                     component->rotation(), component->value(),
                     component->ideality()});
            }
        }
        std::sort(state.components.begin(), state.components.end(),
                  [](const auto& first, const auto& second) {
                      return first.reference < second.reference;
                  });
        std::sort(state.wires.begin(), state.wires.end(),
                  [](const QLineF& first, const QLineF& second) {
                      return std::make_tuple(first.x1(), first.y1(), first.x2(),
                                             first.y2()) <
                             std::make_tuple(second.x1(), second.y1(),
                                             second.x2(), second.y2());
                  });
        return state;
    }

    void setProject(const ProjectState& state) {
        validateProject(state);
        dragging_ = false;
        restore(state);
        history_.clear();
        history_.setClean();
    }

    ComponentItem* findComponent(const QString& reference) const {
        for (auto* item : items()) {
            if (auto* component = dynamic_cast<ComponentItem*>(item)) {
                if (component->name() == reference) {
                    return component;
                }
            }
        }
        return nullptr;
    }

private:
    void restore(const ProjectState& state) {
        restoring_ = true;
        clearProbeReadings();
        activeProbe_.clear();
        cancelWire();
        QStringList selection;
        for (auto* item : selectedItems()) {
            if (const auto* component = dynamic_cast<ComponentItem*>(item)) {
                selection.push_back(component->name());
            }
        }
        QSignalBlocker blocker(this);
        clear();
        counts_ = state.counters;
        for (const auto& component : state.components) {
            auto* item = new ComponentItem(component.kind, component.reference);
            item->setPos(component.position);
            item->setRotation(component.rotation);
            item->setValue(component.value);
            item->setIdeality(component.ideality);
            addItem(item);
            item->changed = [this] { notify(); };
            item->setSelected(selection.contains(component.reference));
            const auto index = static_cast<std::size_t>(component.kind);
            counts_[index] =
                std::max(counts_[index], component.reference.mid(1).toInt());
        }
        for (const auto& wire : state.wires) {
            addWire(wire.p1(), wire.p2());
        }
        recorded_ = project();
        updateSceneBounds();
        restoring_ = false;
        blocker.unblock();
        selectionChanged();
        if (edited) {
            edited();
        }
    }

public:
    void reset() { setProject({}); }

    void loadDivider() {
        reset();
        place(ComponentKind::Voltage, {0, 0});
        place(ComponentKind::Resistor, {0, -120});
        place(ComponentKind::Resistor, {240, -120});
        place(ComponentKind::Ground, {60, 120});
        addWire({-60, 0}, {-60, -120});
        addWire({60, -120}, {180, -120});
        addWire({300, -120}, {300, 120});
        addWire({300, 120}, {60, 120});
        addWire({60, 0}, {60, 120});
        history_.clear();
        history_.setClean();
    }

    ComponentItem* place(ComponentKind kind, const QPointF& position) {
        const auto index = static_cast<std::size_t>(kind);
        const QString prefix = QString::fromLatin1(prefixes_[index]);
        auto* item =
            new ComponentItem(kind, prefix + QString::number(++counts_[index]));
        addItem(item);
        item->setPos(snap(position));
        item->changed = [this] { notify(); };
        notify("Place " + item->name());
        return item;
    }

    void addWire(const QPointF& start, const QPointF& end) {
        if (start == end) {
            return;
        }
        auto* wire = addLine(QLineF(start, end), QPen(QColor("#397a63"), 2));
        wire->setFlag(QGraphicsItem::ItemIsSelectable);
        wire->setZValue(-1);
        notify("Draw wire");
    }

    void setTool(std::optional<ComponentKind> component, bool wire = false) {
        cancelWire();
        probing_ = false;
        placement_ = component;
        wiring_ = wire;
    }

    void cancelWire() {
        wireStart_.reset();
        if (preview_) {
            delete preview_;
            preview_ = nullptr;
        }
    }

    void removeSelected() {
        const auto selected = selectedItems();
        for (auto* item : selected) {
            delete item;
        }
        notify("Delete selection");
    }

    void rotateSelected() {
        restoring_ = true;
        for (auto* item : selectedItems()) {
            if (dynamic_cast<ComponentItem*>(item)) {
                item->setRotation(std::fmod(item->rotation() + 90, 360));
            }
        }
        restoring_ = false;
        notify("Rotate selection");
    }

    std::vector<SchematicComponent> components() const {
        std::vector<SchematicComponent> result;
        for (auto* item : items()) {
            if (const auto* component = dynamic_cast<ComponentItem*>(item)) {
                result.push_back(component->component());
            }
        }
        std::sort(result.begin(), result.end(),
                  [](const auto& first, const auto& second) {
                      return first.name < second.name;
                  });
        return result;
    }

    std::vector<QLineF> wires() const {
        std::vector<QLineF> result;
        for (auto* item : items()) {
            if (const auto* wire = dynamic_cast<QGraphicsLineItem*>(item)) {
                result.push_back(wire->line());
            }
        }
        return result;
    }

    ExtractedCircuit extract() const {
        return extractCircuit(components(), wires());
    }

    std::vector<QPointF> junctions() const {
        const auto segments = wires();
        std::vector<QPointF> candidates;
        for (const auto& segment : segments) {
            candidates.push_back(segment.p1());
            candidates.push_back(segment.p2());
        }
        for (const auto& component : components()) {
            candidates.push_back(component.positive);
            if (component.kind != ComponentKind::Ground) {
                candidates.push_back(component.negative);
            }
        }
        std::vector<QPointF> result;
        for (const auto& point : candidates) {
            int branches = 0;
            for (const auto& segment : segments) {
                if (onSegment(point, segment)) {
                    branches +=
                        point == segment.p1() || point == segment.p2() ? 1 : 2;
                }
            }
            if (branches >= 3 && std::find(result.begin(), result.end(),
                                           point) == result.end()) {
                result.push_back(point);
            }
        }
        return result;
    }

    static QPointF snap(const QPointF& position) {
        return {std::round(position.x() / gridSpacing) * gridSpacing,
                std::round(position.y() / gridSpacing) * gridSpacing};
    }

protected:
    void drawForeground(QPainter* painter, const QRectF& rect) override {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#397a63"));
        for (const auto& point : junctions()) {
            if (rect.contains(point)) {
                painter->drawEllipse(point, 3, 3);
            }
        }
        if (!probeText_.isEmpty()) {
            const auto size =
                painter->fontMetrics().size(Qt::TextSingleLine, probeText_);
            const QRectF label(probePosition_ + QPointF(12, -70),
                               QSizeF(size.width() + 16, size.height() + 10));
            painter->fillRect(label, QColor("#fff6e9"));
            painter->setPen(QColor("#734322"));
            painter->drawText(label.adjusted(8, 5, -8, -5), Qt::AlignCenter,
                              probeText_);
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && probing_) {
            try {
                probeAt(event->scenePos());
            } catch (const std::exception& error) {
                if (probed) {
                    probed(QString::fromUtf8(error.what()));
                }
            }
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton) {
            cancelWire();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && placement_) {
            clearSelection();
            place(*placement_, event->scenePos())->setSelected(true);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && wiring_) {
            const QPointF point = snap(event->scenePos());
            if (!wireStart_) {
                wireStart_ = point;
                preview_ = addPath(QPainterPath(),
                                   QPen(QColor("#007f78"), 2, Qt::DashLine));
                preview_->setZValue(1);
            } else {
                const QPointF start = *wireStart_;
                const QPointF corner(point.x(), start.y());
                cancelWire();
                restoring_ = true;
                addWire(start, corner);
                addWire(corner, point);
                restoring_ = false;
                notify("Draw wire");
            }
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            dragging_ = true;
        }
        QGraphicsScene::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (wireStart_ && preview_) {
            const QPointF point = snap(event->scenePos());
            QPainterPath path(*wireStart_);
            path.lineTo(point.x(), wireStart_->y());
            path.lineTo(point);
            preview_->setPath(path);
            event->accept();
            return;
        }
        QGraphicsScene::mouseMoveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            cancelWire();
            event->accept();
        } else {
            QGraphicsScene::keyPressEvent(event);
        }
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsScene::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            notify("Move selection");
        }
    }

private:
    void updateSceneBounds() {
        setSceneRect(
            QRectF(-2000, -2000, 4000, 4000)
                .united(itemsBoundingRect().adjusted(-400, -400, 400, 400)));
    }

    void notify(const QString& description = "Edit component") {
        if (restoring_) {
            return;
        }
        auto current = project();
        if (current == recorded_) {
            return;
        }
        clearProbeReadings();
        activeProbe_.clear();
        if (dragging_) {
            if (edited) {
                edited();
            }
            return;
        }
        const auto routed = reconnectWires(recorded_, current);
        if (routed != current.wires) {
            restoring_ = true;
            QSignalBlocker blocker(this);
            for (auto* item : items()) {
                if (dynamic_cast<QGraphicsLineItem*>(item)) {
                    delete item;
                }
            }
            for (const auto& wire : routed) {
                addWire(wire.p1(), wire.p2());
            }
            restoring_ = false;
            current = project();
        }
        if (current == recorded_) {
            return;
        }
        auto before = recorded_;
        recorded_ = current;
        updateSceneBounds();
        history_.push(new EditCommand(
            std::move(before), current, description,
            [this](const ProjectState& state) { restore(state); }));
        if (edited) {
            edited();
        }
    }
    inline static constexpr std::array<const char*, 6> prefixes_ = {
        "R", "C", "D", "V", "I", "G"};
    std::array<int, 6> counts_{};
    std::optional<ComponentKind> placement_;
    bool wiring_ = false;
    std::optional<QPointF> wireStart_;
    QGraphicsPathItem* preview_ = nullptr;
    ProjectState recorded_;
    QUndoStack history_;
    bool restoring_ = false;
    bool dragging_ = false;
    bool probing_ = false;
    std::map<QString, QString> readings_;
    QString activeProbe_;
    QString probeText_;
    QPointF probePosition_;
};

}  // namespace microspice::gui
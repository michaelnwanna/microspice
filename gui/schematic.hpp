#pragma once

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyleOptionGraphicsItem>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>

#include "engineering_value.hpp"
#include "schematic_model.hpp"

namespace microspice::gui {

inline constexpr double gridSpacing = 20.0;

class ComponentItem final : public QGraphicsItem {
public:
    explicit ComponentItem(ComponentKind kind = ComponentKind::Resistor,
                           QString name = "R1")
        : kind_(kind), name_(std::move(name)) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setCursor(Qt::OpenHandCursor);
        switch (kind_) {
            case ComponentKind::Resistor:
                value_ = 1000;
                break;
            case ComponentKind::Capacitor:
                value_ = 1e-6;
                break;
            case ComponentKind::Diode:
                value_ = 1e-12;
                break;
            case ComponentKind::Voltage:
                value_ = 10;
                break;
            case ComponentKind::Current:
                value_ = 1e-3;
                break;
            case ComponentKind::Ground:
                value_ = 0;
                break;
        }
    }

    QRectF boundingRect() const override { return {-66, -48, 132, 96}; }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget*) override {
        const bool selected = option->state.testFlag(QStyle::State_Selected);
        painter->setPen(
            QPen(selected ? QColor("#007f78") : QColor("#293238"), 2));
        painter->setBrush(Qt::NoBrush);
        if (kind_ == ComponentKind::Ground) {
            painter->drawLine(QPointF(0, 0), QPointF(0, 20));
            for (int level = 0; level < 3; ++level) {
                const double halfWidth = 20 - level * 6;
                painter->drawLine(QPointF(-halfWidth, 20 + level * 6),
                                  QPointF(halfWidth, 20 + level * 6));
            }
        } else {
            const double extent = kind_ == ComponentKind::Resistor ? 30 : 20;
            painter->drawLine(QPointF(-60, 0), QPointF(-extent, 0));
            painter->drawLine(QPointF(extent, 0), QPointF(60, 0));
            switch (kind_) {
                case ComponentKind::Resistor:
                    painter->drawRect(QRectF(-30, -10, 60, 20));
                    break;
                case ComponentKind::Capacitor:
                    painter->drawLine(QPointF(-20, 0), QPointF(-6, 0));
                    painter->drawLine(QPointF(6, 0), QPointF(20, 0));
                    painter->drawLine(QPointF(-6, -18), QPointF(-6, 18));
                    painter->drawLine(QPointF(6, -18), QPointF(6, 18));
                    break;
                case ComponentKind::Diode: {
                    QPolygonF triangle;
                    triangle << QPointF(-16, -16) << QPointF(-16, 16)
                             << QPointF(16, 0);
                    painter->drawPolygon(triangle);
                    painter->drawLine(QPointF(16, -16), QPointF(16, 16));
                    painter->drawLine(QPointF(-20, 0), QPointF(-16, 0));
                    painter->drawLine(QPointF(16, 0), QPointF(20, 0));
                    break;
                }
                case ComponentKind::Voltage:
                    painter->drawEllipse(QRectF(-20, -20, 40, 40));
                    painter->drawText(QRectF(-18, -12, 36, 24), Qt::AlignCenter,
                                      "+  -");
                    break;
                case ComponentKind::Current:
                    painter->drawEllipse(QRectF(-20, -20, 40, 40));
                    painter->drawLine(QPointF(-12, 0), QPointF(12, 0));
                    painter->drawLine(QPointF(12, 0), QPointF(4, -6));
                    painter->drawLine(QPointF(12, 0), QPointF(4, 6));
                    break;
                case ComponentKind::Ground:
                    break;
            }
        }
        painter->setBrush(QColor("#fcfcfc"));
        painter->drawEllipse(
            kind_ == ComponentKind::Ground ? QPointF(0, 0) : QPointF(-60, 0), 3,
            3);
        if (kind_ != ComponentKind::Ground) {
            painter->drawEllipse(QPointF(60, 0), 3, 3);
        }
        painter->drawText(QRectF(-60, -46, 120, 24), Qt::AlignCenter, name_);
        if (kind_ != ComponentKind::Ground) {
            painter->drawText(QRectF(-60, 22, 120, 24), Qt::AlignCenter,
                              valueText());
        }
        if (selected) {
            painter->setPen(QPen(QColor("#007f78"), 1, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(-65, -47, 130, 94));
        }
    }

    ComponentKind kind() const { return kind_; }
    std::function<void()> changed;
    QString name() const { return name_; }
    double resistance() const { return value_; }
    double value() const { return value_; }
    QString valueText() const {
        return formatEngineeringValue(value_, 4) + " " + unit();
    }
    double ideality() const { return ideality_; }
    void setIdeality(double value) {
        if (std::isfinite(value) && value > 0 && ideality_ != value) {
            ideality_ = value;
            if (changed) {
                changed();
            }
        }
    }
    QString unit() const {
        switch (kind_) {
            case ComponentKind::Resistor:
                return QStringLiteral("\u03A9");
            case ComponentKind::Capacitor:
                return "F";
            case ComponentKind::Diode:
                return "A";
            case ComponentKind::Voltage:
                return "V";
            case ComponentKind::Current:
                return "A";
            case ComponentKind::Ground:
                return {};
        }
        return {};
    }
    SchematicComponent component() const {
        const auto terminal = [this](const QPointF& point) {
            const auto position = mapToScene(point);
            return QPointF(
                std::round(position.x() / gridSpacing) * gridSpacing,
                std::round(position.y() / gridSpacing) * gridSpacing);
        };
        return {kind_,
                name_,
                terminal(kind_ == ComponentKind::Ground ? QPointF(0, 0)
                                                        : QPointF(-60, 0)),
                terminal(QPointF(60, 0)),
                value_,
                ideality_};
    }

    void setResistance(double resistance) { setValue(resistance); }

    void setValue(double value) {
        const bool signedValue =
            kind_ == ComponentKind::Voltage || kind_ == ComponentKind::Current;
        if (!std::isfinite(value) || (!signedValue && value <= 0)) {
            return;
        }
        if (value_ == value) {
            return;
        }
        value_ = value;
        update();
        if (changed) {
            changed();
        }
    }

protected:
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override {
        if (change == ItemPositionChange) {
            const QPointF position = value.toPointF();
            return QPointF(
                std::round(position.x() / gridSpacing) * gridSpacing,
                std::round(position.y() / gridSpacing) * gridSpacing);
        }
        if ((change == ItemPositionHasChanged ||
             change == ItemRotationHasChanged) &&
            changed) {
            changed();
        }
        return QGraphicsItem::itemChange(change, value);
    }

private:
    ComponentKind kind_;
    QString name_;
    double value_ = 1000;
    double ideality_ = 1;
};

using ResistorItem = ComponentItem;

class SchematicView final : public QGraphicsView {
public:
    explicit SchematicView(QGraphicsScene* scene, QWidget* parent = nullptr)
        : QGraphicsView(scene, parent) {
        setRenderHint(QPainter::Antialiasing);
        setBackgroundBrush(QColor("#fcfcfc"));
        setFrameShape(QFrame::NoFrame);
        setDragMode(RubberBandDrag);
        setTransformationAnchor(NoAnchor);
        setResizeAnchor(AnchorViewCenter);
        setMinimumSize(240, 200);
    }

    void zoomBy(double factor, const QPoint& viewportPosition) {
        if (!std::isfinite(factor) || factor <= 0) {
            return;
        }
        const double currentScale = transform().m11();
        const double nextScale = std::clamp(currentScale * factor, 0.25, 4.0);
        fitted_ = false;
        const QPointF before = mapToScene(viewportPosition);
        scale(nextScale / currentScale, nextScale / currentScale);
        const QPointF offset = mapToScene(viewportPosition) - before;
        translate(offset.x(), offset.y());
    }

    void zoomBy(double factor) { zoomBy(factor, viewport()->rect().center()); }

    void fitSchematic() {
        resetTransform();
        const auto bounds =
            scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40);
        const double fittedScale =
            std::clamp(std::min(viewport()->width() / bounds.width(),
                                viewport()->height() / bounds.height()),
                       0.25, 1.0);
        scale(fittedScale, fittedScale);
        centerOn(bounds.center());
        fitted_ = true;
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QGraphicsView::resizeEvent(event);
        if (fitted_) {
            fitSchematic();
        }
    }

    void drawBackground(QPainter* painter, const QRectF& rect) override {
        painter->fillRect(rect, backgroundBrush());
        const double spacing =
            transform().m11() < 0.5 ? gridSpacing * 5 : gridSpacing;
        const double left = std::floor(rect.left() / spacing) * spacing;
        const double top = std::floor(rect.top() / spacing) * spacing;
        painter->setPen(QPen(QColor("#ccd4d7"), 0));
        for (double horizontal = left; horizontal <= rect.right();
             horizontal += spacing) {
            for (double vertical = top; vertical <= rect.bottom();
                 vertical += spacing) {
                painter->drawPoint(QPointF(horizontal, vertical));
            }
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        if (event->angleDelta().y() == 0) {
            event->ignore();
            return;
        }
        zoomBy(std::pow(1.0015, event->angleDelta().y()),
               event->position().toPoint());
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            panning_ = true;
            fitted_ = false;
            panPosition_ = event->position().toPoint();
            viewport()->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (panning_) {
            const QPoint position = event->position().toPoint();
            const QPointF delta =
                mapToScene(panPosition_) - mapToScene(position);
            centerOn(mapToScene(viewport()->rect().center()) + delta);
            panPosition_ = position;
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton && panning_) {
            panning_ = false;
            viewport()->unsetCursor();
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    bool fitted_ = true;
    bool panning_ = false;
    QPoint panPosition_;
};

}  // namespace microspice::gui
#pragma once

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <limits>

#include "analysis.hpp"

namespace microspice::gui {

class WaveformPlot final : public QWidget {
public:
    explicit WaveformPlot(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(260, 200);
        setMouseTracking(true);
    }

    void setTrace(const WaveformTrace& trace, const QString& horizontalLabel) {
        trace_ = trace;
        horizontalLabel_ = horizontalLabel;
        resetView();
    }

    void clear() {
        trace_ = {};
        hover_ = false;
        update();
    }

    void resetView() {
        if (trace_.points.empty()) {
            return;
        }
        double minimumX = std::numeric_limits<double>::infinity();
        double maximumX = -minimumX;
        double minimumY = minimumX;
        double maximumY = maximumX;
        for (const auto& point : trace_.points) {
            minimumX = std::min(minimumX, point.x());
            maximumX = std::max(maximumX, point.x());
            minimumY = std::min(minimumY, point.y());
            maximumY = std::max(maximumY, point.y());
        }
        const double xPadding =
            maximumX == minimumX ? std::max(std::abs(minimumX) * 0.1, 1e-6) : 0;
        const double yPadding = maximumY == minimumY
                                    ? std::max(std::abs(minimumY) * 0.1, 1e-6)
                                    : (maximumY - minimumY) * 0.1;
        fullRange_ = QRectF(QPointF(minimumX - xPadding, minimumY - yPadding),
                            QPointF(maximumX + xPadding, maximumY + yPadding));
        range_ = fullRange_;
        hover_ = false;
        update();
    }

    QRectF dataRange() const { return range_; }
    std::size_t pointCount() const { return trace_.points.size(); }

protected:
    QRectF plotRect() const {
        return QRectF(72, 44, width() - 92, height() - 94);
    }

    QPointF screenPoint(const QPointF& point) const {
        const auto rect = plotRect();
        return {rect.left() +
                    (point.x() - range_.left()) / range_.width() * rect.width(),
                rect.bottom() - (point.y() - range_.top()) / range_.height() *
                                    rect.height()};
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#fafcfc"));
        if (trace_.points.empty()) {
            return;
        }
        const auto area = plotRect();
        painter.setPen(QColor("#263638"));
        painter.drawText(QRectF(72, 8, width() - 92, 26),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(
                             trace_.name + " (" + trace_.unit + ")",
                             Qt::ElideRight, width() - 92));
        for (int tick = 0; tick <= 4; ++tick) {
            const double fraction = static_cast<double>(tick) / 4;
            const double horizontal = area.left() + fraction * area.width();
            const double vertical = area.bottom() - fraction * area.height();
            painter.setPen(QPen(QColor("#dce4e5"), 1));
            painter.drawLine(QPointF(horizontal, area.top()),
                             QPointF(horizontal, area.bottom()));
            painter.drawLine(QPointF(area.left(), vertical),
                             QPointF(area.right(), vertical));
            painter.setPen(QColor("#45585b"));
            const double labelLeft =
                std::clamp(horizontal - 36, area.left() - 20,
                           static_cast<double>(width()) - 76);
            painter.drawText(
                QRectF(labelLeft, area.bottom() + 4, 72, 20), Qt::AlignCenter,
                QString::number(range_.left() + fraction * range_.width(), 'g',
                                3));
            painter.drawText(
                QRectF(2, vertical - 10, 64, 20),
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(range_.top() + fraction * range_.height(), 'g',
                                3));
        }
        painter.drawText(QRectF(72, height() - 24, width() - 92, 20),
                         Qt::AlignCenter, horizontalLabel_);
        painter.save();
        painter.setClipRect(area);
        QPainterPath path;
        path.moveTo(screenPoint(trace_.points.front()));
        for (std::size_t index = 1; index < trace_.points.size(); ++index) {
            path.lineTo(screenPoint(trace_.points[index]));
        }
        painter.setPen(QPen(QColor("#007f78"), 2));
        painter.drawPath(path);
        if (trace_.points.size() == 1) {
            painter.setBrush(QColor("#007f78"));
            painter.drawEllipse(screenPoint(trace_.points.front()), 3, 3);
        }
        if (hover_) {
            const auto nearest = std::min_element(
                trace_.points.begin(), trace_.points.end(),
                [this](const auto& first, const auto& second) {
                    return std::abs(screenPoint(first).x() - pointer_.x()) <
                           std::abs(screenPoint(second).x() - pointer_.x());
                });
            const auto position = screenPoint(*nearest);
            painter.setPen(QPen(QColor("#ad5431"), 1, Qt::DashLine));
            painter.drawLine(QPointF(position.x(), area.top()),
                             QPointF(position.x(), area.bottom()));
            painter.setBrush(QColor("#ad5431"));
            painter.drawEllipse(position, 3, 3);
            painter.restore();
            painter.setPen(QColor("#263638"));
            painter.drawText(QRectF(72, 28, width() - 92, 16), Qt::AlignRight,
                             QString::number(nearest->x(), 'g', 6) + "  |  " +
                                 QString::number(nearest->y(), 'g', 6) + " " +
                                 trace_.unit);
        } else {
            painter.restore();
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        if (trace_.points.empty() || !plotRect().contains(event->position())) {
            event->ignore();
            return;
        }
        const double factor =
            std::clamp(std::pow(1.0015, -event->angleDelta().y()), 0.5, 2.0);
        const double newWidth =
            std::clamp(range_.width() * factor, fullRange_.width() / 1000,
                       fullRange_.width() * 10);
        const double fraction =
            (event->position().x() - plotRect().left()) / plotRect().width();
        range_.setLeft(range_.left() + fraction * (range_.width() - newWidth));
        range_.setWidth(newWidth);
        update();
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton &&
            plotRect().contains(event->position())) {
            panning_ = true;
            pointer_ = event->position();
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (panning_) {
            range_.translate(-(event->position().x() - pointer_.x()) /
                                 plotRect().width() * range_.width(),
                             0);
        }
        pointer_ = event->position();
        hover_ = plotRect().contains(pointer_);
        update();
    }

    void mouseReleaseEvent(QMouseEvent*) override {
        panning_ = false;
        unsetCursor();
    }
    void leaveEvent(QEvent*) override {
        hover_ = false;
        update();
    }

private:
    WaveformTrace trace_;
    QString horizontalLabel_;
    QRectF fullRange_;
    QRectF range_;
    QPointF pointer_;
    bool panning_ = false;
    bool hover_ = false;
};

}  // namespace microspice::gui
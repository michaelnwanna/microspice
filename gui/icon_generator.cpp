#include <QDir>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <array>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return EXIT_FAILURE;
    }
    const QDir directory(QString::fromLocal8Bit(argv[1]));
    if (!directory.mkpath(".")) {
        return EXIT_FAILURE;
    }
    for (const int size : std::array<int, 5>{16, 32, 128, 256, 512}) {
        for (const int scale : {1, 2}) {
            QImage image(size * scale, size * scale,
                         QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.scale(static_cast<double>(size * scale) / 512,
                          static_cast<double>(size * scale) / 512);
            QLinearGradient background(40, 40, 460, 470);
            background.setColorAt(0, QColor("#145e52"));
            background.setColorAt(1, QColor("#052f2c"));
            painter.setPen(Qt::NoPen);
            painter.setBrush(background);
            painter.drawRoundedRect(QRectF(24, 24, 464, 464), 96, 96);
            painter.setPen(QPen(QColor("#f4f7f5"), 22, Qt::SolidLine,
                                Qt::RoundCap, Qt::RoundJoin));
            QPainterPath circuit;
            circuit.moveTo(96, 172);
            circuit.lineTo(155, 172);
            circuit.lineTo(175, 140);
            circuit.lineTo(205, 204);
            circuit.lineTo(235, 140);
            circuit.lineTo(265, 204);
            circuit.lineTo(285, 172);
            circuit.lineTo(416, 172);
            circuit.lineTo(416, 344);
            circuit.lineTo(96, 344);
            circuit.lineTo(96, 172);
            painter.drawPath(circuit);
            painter.setPen(QPen(QColor("#edc553"), 18, Qt::SolidLine,
                                Qt::RoundCap, Qt::RoundJoin));
            QPainterPath waveform;
            waveform.moveTo(154, 278);
            waveform.lineTo(200, 278);
            waveform.lineTo(228, 232);
            waveform.lineTo(260, 306);
            waveform.lineTo(290, 256);
            waveform.lineTo(360, 256);
            painter.drawPath(waveform);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#e56f63"));
            painter.drawEllipse(QPointF(416, 172), 24, 24);
            painter.end();
            const QString filename = QString("icon_%1x%1%2.png")
                                         .arg(size)
                                         .arg(scale == 2 ? "@2x" : "");
            if (!image.save(directory.filePath(filename))) {
                return EXIT_FAILURE;
            }
        }
    }
    return EXIT_SUCCESS;
}
#include "util.h"

#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardPaths>

namespace kettle::gui {

static QPixmap renderIcon(int size, bool connected) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);

    const QColor accent("#F08A4B");
    const QColor dark("#14161A");

    // korpus: szerszy u dołu, zaokrąglony
    QPainterPath body;
    body.moveTo(8.2, 9.0);
    body.lineTo(15.8, 9.0);
    body.cubicTo(18.6, 10.6, 20.4, 14.2, 20.4, 17.6);
    body.quadTo(20.4, 21.0, 17.0, 21.0);
    body.lineTo(7.0, 21.0);
    body.quadTo(3.6, 21.0, 3.6, 17.6);
    body.cubicTo(3.6, 14.2, 5.4, 10.6, 8.2, 9.0);
    body.closeSubpath();

    // dzióbek z lewej, skierowany w górę
    QPainterPath spout;
    spout.moveTo(5.6, 12.2);
    spout.lineTo(1.6, 8.6);
    spout.quadTo(1.0, 8.0, 1.8, 7.7);
    spout.lineTo(2.6, 7.5);
    spout.lineTo(6.8, 10.6);
    spout.closeSubpath();

    p.setPen(Qt::NoPen);
    p.fillPath(body.united(spout), accent);

    // pasek na korpusie (detal)
    p.setBrush(dark);
    p.setOpacity(0.18);
    p.drawRoundedRect(QRectF(4.6, 15.6, 14.8, 1.4), 0.7, 0.7);
    p.setOpacity(1.0);

    // gałka pokrywki
    p.setBrush(accent);
    p.drawEllipse(QPointF(12.0, 7.6), 1.4, 1.1);

    // pałąk nad czajnikiem
    QPen pen(accent, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath handle;
    handle.moveTo(7.6, 9.4);
    handle.cubicTo(7.4, 3.0, 16.6, 3.0, 16.4, 9.4);
    p.drawPath(handle);

    // para z dzióbka
    QPen steamPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(steamPen);
    QPainterPath s;
    s.moveTo(2.4, 5.6);
    s.cubicTo(1.6, 4.6, 3.2, 3.8, 2.4, 2.6);
    p.drawPath(s);

    if (connected) {
        p.setPen(QPen(dark, 1.2));
        p.setBrush(QColor("#4CC38A"));
        p.drawEllipse(QPointF(19.2, 19.2), 3.6, 3.6);
    }
    return pm;
}

QPixmap renderAppIcon(int size, bool connected) { return renderIcon(size, connected); }

QIcon makeAppIcon(bool connected) {
    QIcon icon;
    for (int s : {16, 22, 24, 32, 48, 64, 128, 256}) icon.addPixmap(renderIcon(s, connected));
    return icon;
}

QString logFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/kettlelan.log");
}

} // namespace kettle::gui

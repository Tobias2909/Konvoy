#include "theme.h"

#include <QApplication>
#include <QPainterPath>
#include <QPalette>

#include <cmath>

namespace Theme
{

namespace
{
qreal spinAngle = 0;
}

void advanceSpin()
{
    spinAngle = std::fmod(spinAngle + 30, 360);
}

QColor stateColor(PaneState state)
{
    switch (state) {
    case PaneState::Working:
        return QColor(61, 174, 233);
    case PaneState::Background:
        return QColor(41, 128, 185);
    case PaneState::Permission:
        return QColor(246, 116, 0);
    case PaneState::Question:
        return QColor(253, 188, 75);
    case PaneState::Done:
        return QColor(39, 174, 96);
    case PaneState::Exited:
        return QColor(218, 68, 83);
    case PaneState::Idle:
    case PaneState::Starting:
        break;
    }
    return QColor(127, 140, 141);
}

QString stateText(PaneState state)
{
    switch (state) {
    case PaneState::Starting:
        return QStringLiteral("starting");
    case PaneState::Working:
        return QStringLiteral("working");
    case PaneState::Background:
        return QStringLiteral("in background");
    case PaneState::Permission:
        return QStringLiteral("needs permission");
    case PaneState::Question:
        return QStringLiteral("question");
    case PaneState::Done:
        return QStringLiteral("done");
    case PaneState::Idle:
        return QStringLiteral("idle");
    case PaneState::Exited:
        return QStringLiteral("exited");
    }
    return QString();
}

bool needsYou(PaneState state)
{
    return state == PaneState::Permission || state == PaneState::Question;
}

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

QColor accent()
{
    return QApplication::palette().color(QPalette::Highlight);
}

void paintStateIcon(QPainter &p, QPointF c, PaneState state, qreal r)
{
    const QColor col = stateColor(state);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    switch (state) {
    case PaneState::Working: {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(withAlpha(col, 0.25), 2.2));
        p.drawEllipse(c, r - 1, r - 1);
        p.setPen(QPen(col, 2.2, Qt::SolidLine, Qt::RoundCap));
        const QRectF arc(c.x() - r + 1, c.y() - r + 1, 2 * r - 2, 2 * r - 2);
        p.drawArc(arc, int((90 - spinAngle) * 16), -250 * 16);
        break;
    }
    case PaneState::Background: {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(c, r, r);
        p.setBrush(Qt::white);
        for (int i = -1; i <= 1; ++i) {
            p.drawEllipse(QPointF(c.x() + i * 3.2, c.y()), 1.15, 1.15);
        }
        break;
    }
    case PaneState::Permission: {
        QPainterPath path;
        path.moveTo(c.x(), c.y() - r - 0.5);
        path.lineTo(c.x() + r + 0.5, c.y() + r - 0.5);
        path.lineTo(c.x() - r - 0.5, c.y() + r - 0.5);
        path.closeSubpath();
        p.setPen(QPen(col, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(col);
        p.drawPath(path);
        p.setPen(QPen(QColor(30, 20, 10), 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(c.x(), c.y() - 2.5), QPointF(c.x(), c.y() + 1.5));
        p.drawPoint(QPointF(c.x(), c.y() + 4.2));
        break;
    }
    case PaneState::Question: {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(c, r, r);
        QFont f = p.font();
        f.setPointSizeF(8);
        f.setWeight(QFont::Black);
        p.setFont(f);
        p.setPen(QColor(40, 30, 0));
        p.drawText(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), Qt::AlignCenter, QStringLiteral("?"));
        break;
    }
    case PaneState::Done: {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(c, r, r);
        QPainterPath tick;
        tick.moveTo(c.x() - 3.3, c.y() + 0.2);
        tick.lineTo(c.x() - 0.8, c.y() + 2.7);
        tick.lineTo(c.x() + 3.6, c.y() - 2.6);
        p.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(tick);
        break;
    }
    case PaneState::Exited: {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(c, r, r);
        p.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap));
        const qreal d = 2.7;
        p.drawLine(QPointF(c.x() - d, c.y() - d), QPointF(c.x() + d, c.y() + d));
        p.drawLine(QPointF(c.x() - d, c.y() + d), QPointF(c.x() + d, c.y() - d));
        break;
    }
    case PaneState::Idle:
        p.setPen(QPen(col, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r - 1, r - 1);
        break;
    case PaneState::Starting:
        p.setPen(QPen(col, 1.6, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r - 1, r - 1);
        break;
    }
    p.restore();
}

void paintBadge(QPainter &p, const QRectF &rect, const QString &letter, const QColor &color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(rect, 4, 4);
    QFont f = p.font();
    f.setPointSizeF(rect.height() >= 18 ? 9 : 8);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(rect, Qt::AlignCenter, letter);
    p.restore();
}

}

#pragma once

#include <QColor>
#include <QPainter>
#include <QString>

struct Account;

enum class PaneState {
    Starting,
    Working,
    Background, // the turn is over, but shells, monitors or subagents still run
    Permission,
    Question,
    Done,
    Idle,
    Exited,
};

namespace Theme
{
QColor stateColor(PaneState state);
QString stateText(PaneState state);
bool needsYou(PaneState state);

QColor withAlpha(QColor color, qreal alpha);

// The focus outline follows the Plasma accent colour.
QColor accent();

// Painted state icon, about 2r wide. The working arc turns with advanceSpin().
void paintStateIcon(QPainter &p, QPointF center, PaneState state, qreal r = 7);
void advanceSpin();

// Rounded square with the account letter.
void paintBadge(QPainter &p, const QRectF &rect, const QString &letter, const QColor &color);
// A program's own symbol (an emoji, say) in a dark square tinted and framed with its colour,
// so a colourful symbol stays visible on any colour.
void paintSymbolBadge(QPainter &p, const QRectF &rect, const QString &symbol, const QColor &color);
// An account's or program's badge wherever it shows: its Badge symbol if it has one, else
// its key letter.
void paintBadge(QPainter &p, const QRectF &rect, const Account &account);
}

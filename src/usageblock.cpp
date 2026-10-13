#include "usageblock.h"
#include "config.h"
#include "theme.h"
#include "usage.h"

#include <KConfigGroup>

#include <QDateTime>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolTip>

#include <algorithm>

namespace
{
constexpr int headerHeight = 26;
constexpr int nameHeight = 24;
constexpr int windowHeight = 19;
constexpr int lineHeight = 18;
constexpr int accountGap = 8;
const QString resetGlyph = QStringLiteral("↻ ");

// Konvoy speaks English, so the dates do too. Compact leaves out the time of a
// date more than a week away, so it fits the reset column.
QString when(qint64 ts, qint64 now, bool compact = false)
{
    // Resets land a moment before the minute (13:49:59.8); show the minute they mean.
    const QDateTime t = QDateTime::fromSecsSinceEpoch((ts + 30) / 60 * 60);
    const QDateTime n = QDateTime::fromSecsSinceEpoch(now);
    const QLocale c = QLocale::c();
    if (t.date() == n.date()) {
        return c.toString(t, QStringLiteral("HH:mm"));
    }
    if (qAbs(ts - now) < 7 * 86400) {
        return c.toString(t, QStringLiteral("ddd HH:mm"));
    }
    return c.toString(t, compact ? QStringLiteral("d MMM") : QStringLiteral("d MMM HH:mm"));
}

QString timeLeft(qint64 secs)
{
    if (secs <= 0) {
        return QStringLiteral("now");
    }
    const qint64 minutes = secs / 60;
    const qint64 days = minutes / 1440;
    const qint64 hours = minutes % 1440 / 60;
    if (days > 0) {
        return QStringLiteral("%1d %2h").arg(days).arg(hours);
    }
    if (hours > 0) {
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes % 60);
    }
    return QStringLiteral("%1m").arg(qMax<qint64>(1, minutes));
}

QColor percentColor(double percent, const QColor &normal)
{
    if (percent >= 95) {
        return Theme::stateColor(PaneState::Exited);
    }
    if (percent >= 80) {
        return Theme::stateColor(PaneState::Permission);
    }
    return normal;
}

void paintBar(QPainter &p, const QRectF &rect, double percent, const QColor &fill, const QColor &track)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
    if (percent > 0) {
        const qreal w = qMax(rect.height(), rect.width() * qMin(percent, 100.0) / 100);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect.x(), rect.y(), w, rect.height()), rect.height() / 2, rect.height() / 2);
    }
    p.restore();
}
}

UsageBlock::UsageBlock(const Config *config, const UsageModel *model, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
    , m_model(model)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMouseTracking(true);
    m_timeLeft = m_config->state()->group(QStringLiteral("Usage")).readEntry("ShowTimeLeft", false);
    m_height = contentHeight();
    // The time left counts down between two updates of the numbers.
    auto *tick = new QTimer(this);
    tick->start(15000);
    connect(tick, &QTimer::timeout, this, [this] {
        if (m_timeLeft && isVisible()) {
            update();
        }
    });
    connect(model, &UsageModel::changed, this, [this] {
        const int h = contentHeight();
        if (h != m_height) {
            m_height = h;
            updateGeometry();
        }
        update();
    });
}

QSize UsageBlock::sizeHint() const
{
    return QSize(200, m_height);
}

QSize UsageBlock::minimumSizeHint() const
{
    return sizeHint();
}

QString UsageBlock::resetLine(const AccountUsage *u, qint64 now) const
{
    if (!u || u->resetsAvailable <= 0 || (u->resetsExpireAt > 0 && u->resetsExpireAt <= now)) {
        return QString();
    }
    QString line = u->resetsAvailable == 1 ? QStringLiteral("1 reset") : QStringLiteral("%1 resets").arg(u->resetsAvailable);
    if (u->resetsExpireAt > 0) {
        line += QStringLiteral(" · expires ") + QLocale::c().toString(QDateTime::fromSecsSinceEpoch(u->resetsExpireAt), QStringLiteral("d MMM HH:mm"));
    }
    return line;
}

int UsageBlock::accountHeight(const AccountUsage *u, qint64 now) const
{
    return nameHeight + 2 * windowHeight + (resetLine(u, now).isEmpty() ? 0 : lineHeight) + accountGap;
}

int UsageBlock::contentHeight() const
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    int h = headerHeight;
    for (const Account &account : m_config->accounts()) {
        h += accountHeight(m_model->usage(account.key), now);
    }
    return h;
}

void UsageBlock::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::WindowText);
    const QColor dim = pal.color(QPalette::PlaceholderText);
    const QColor dimmer = Theme::withAlpha(dim, 0.75);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qreal w = width();

    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1);
    QFont tiny = font();
    tiny.setPointSizeF(font().pointSizeF() - 2);
    QFont heading = tiny;
    heading.setWeight(QFont::Bold);

    // Section header, with the age of the file when the helper does not run.
    qreal y = 0;
    p.fillRect(QRectF(8, y, w - 16, 1), pal.color(QPalette::Mid));
    p.setFont(heading);
    p.setPen(dimmer);
    p.drawText(QRectF(10, y + 6, w - 20, 16), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("USAGE"));
    if (m_model->outdated(now)) {
        p.setFont(tiny);
        p.drawText(QRectF(10, y + 6, w - 20, 16), Qt::AlignVCenter | Qt::AlignRight,
                   m_model->hasData() ? QStringLiteral("not updated since ") + when(m_model->written(), now) : QStringLiteral("waiting for numbers"));
    }
    y += headerHeight;

    const qreal percentWidth = QFontMetrics(small).horizontalAdvance(QStringLiteral("100%"));
    const QFontMetrics tinyMetrics(tiny);
    const qreal resetWidth = qMax(tinyMetrics.horizontalAdvance(resetGlyph + QStringLiteral("Mon 11:00")),
                                  tinyMetrics.horizontalAdvance(resetGlyph + QStringLiteral("23h 59m")))
        + 4;
    m_resetRects.clear();
    const qreal barX = 36;
    const qreal barWidth = w - barX - percentWidth - resetWidth - 24;

    for (const Account &account : m_config->accounts()) {
        const AccountUsage *u = m_model->usage(account.key);
        const bool stale = !u || m_model->stale(*u);

        // Without an open pane the numbers are the last ones Claude Code had.
        const QString age = !u || u->asOf == 0 ? QStringLiteral("no numbers yet") : !u->live ? QStringLiteral("as of ") + when(u->asOf, now) : QString();
        const qreal ageWidth = age.isEmpty() ? 0 : tinyMetrics.horizontalAdvance(age) + 8;
        Theme::paintBadge(p, QRectF(10, y, 18, 18), account.key, account.color);
        p.setFont(bold);
        p.setPen(text);
        p.drawText(QRectF(34, y, w - 44 - ageWidth, 18), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(bold).elidedText(account.name, Qt::ElideRight, int(w - 44 - ageWidth)));
        if (!age.isEmpty()) {
            p.setFont(tiny);
            p.setPen(dimmer);
            p.drawText(QRectF(34, y, w - 44, 18), Qt::AlignVCenter | Qt::AlignRight, age);
        }
        y += nameHeight;

        const UsageWindow none;
        const QList<std::pair<QString, UsageWindow>> windows{{QStringLiteral("5h"), u ? u->fiveHour : none},
                                                             {QStringLiteral("7d"), u ? u->sevenDay : none}};
        for (const auto &[label, win] : windows) {
            const bool passed = win.passed(now);
            const double percent = passed ? 0 : win.percent;
            p.setFont(small);
            p.setPen(dimmer);
            p.drawText(QRectF(12, y, 22, 16), Qt::AlignVCenter | Qt::AlignLeft, label);
            paintBar(p, QRectF(barX, y + 5, barWidth, 6), win.known() ? percent : 0,
                     stale ? Theme::withAlpha(account.color, 0.45) : account.color, Theme::withAlpha(text, 0.10));
            p.setPen(stale || !win.known() ? dim : percentColor(percent, text));
            p.drawText(QRectF(barX + barWidth + 6, y, percentWidth, 16), Qt::AlignVCenter | Qt::AlignRight,
                       win.known() ? QStringLiteral("%1%").arg(qRound(percent)) : QStringLiteral("–"));
            if (win.known() && win.resetsAt > 0 && !passed) {
                p.setFont(tiny);
                p.setPen(dimmer);
                const QRectF cell(w - resetWidth - 10, y, resetWidth, 16);
                p.drawText(cell, Qt::AlignVCenter | Qt::AlignRight,
                           resetGlyph + (m_timeLeft ? timeLeft(win.resetsAt - now) : when(win.resetsAt, now, true)));
                m_resetRects.append(cell);
            }
            y += windowHeight;
        }

        const QString reset = resetLine(u, now);
        if (!reset.isEmpty()) {
            p.setFont(tiny);
            p.setPen(Theme::stateColor(PaneState::Done));
            p.drawText(QRectF(12, y, w - 22, 16), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("★ ") + reset);
            y += lineHeight;
        }
        y += accountGap;
    }
}

QString UsageBlock::tooltip(int index, qint64 now) const
{
    const Account &account = m_config->accounts().at(index);
    const AccountUsage *u = m_model->usage(account.key);
    QStringList lines{QStringLiteral("%1 (%2)").arg(account.name, account.key)};
    if (!u || u->asOf == 0) {
        lines << QStringLiteral("No numbers yet. A pane of this account brings them with its first answer.");
        return lines.join(QLatin1Char('\n'));
    }
    const QList<std::pair<QString, UsageWindow>> windows{{QStringLiteral("5 hours"), u->fiveHour}, {QStringLiteral("7 days"), u->sevenDay}};
    for (const auto &[label, win] : windows) {
        if (!win.known()) {
            continue;
        }
        if (win.passed(now)) {
            lines << QStringLiteral("%1: reset since %2").arg(label, when(win.resetsAt, now));
        } else if (win.resetsAt > 0) {
            lines << QStringLiteral("%1: %2% used, resets %3 (in %4)").arg(label).arg(qRound(win.percent)).arg(when(win.resetsAt, now), timeLeft(win.resetsAt - now));
        } else {
            lines << QStringLiteral("%1: %2% used").arg(label).arg(qRound(win.percent));
        }
    }
    const QString source = u->source == QLatin1String("cache") ? QStringLiteral("the numbers Claude Code saved")
                                                               : QStringLiteral("a Claude Code session of this account");
    lines << QStringLiteral("From %1 at %2").arg(source, when(u->asOf, now));
    if (!u->live) {
        lines << QStringLiteral("No pane of this account is open, so they may be out of date.");
    }
    lines << (m_timeLeft ? QStringLiteral("Click a reset time to show the clock time.") : QStringLiteral("Click a reset time to show the time left."));
    return lines.join(QLatin1Char('\n'));
}

bool UsageBlock::onResetTime(const QPointF &pos) const
{
    return std::any_of(m_resetRects.cbegin(), m_resetRects.cend(), [&pos](const QRectF &r) {
        return r.contains(pos);
    });
}

void UsageBlock::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !onResetTime(event->position())) {
        return QWidget::mousePressEvent(event);
    }
    m_timeLeft = !m_timeLeft;
    KConfigGroup g = m_config->state()->group(QStringLiteral("Usage"));
    g.writeEntry("ShowTimeLeft", m_timeLeft);
    g.sync();
    update();
}

void UsageBlock::mouseMoveEvent(QMouseEvent *event)
{
    if (onResetTime(event->position())) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

bool UsageBlock::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        int y = headerHeight;
        for (int i = 0; i < m_config->accounts().size(); ++i) {
            const int h = accountHeight(m_model->usage(m_config->accounts().at(i).key), now);
            if (help->pos().y() >= y && help->pos().y() < y + h) {
                QToolTip::showText(help->globalPos(), tooltip(i, now), this);
                return true;
            }
            y += h;
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

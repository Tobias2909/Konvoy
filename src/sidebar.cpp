#include "sidebar.h"
#include "config.h"
#include "pane.h"
#include "theme.h"
#include "usage.h"
#include "usageblock.h"

#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace
{
constexpr int cardHeight = 52;
constexpr int cardGap = 6;
constexpr int margin = 8;
constexpr int holdMsecs = 400;
constexpr int stripRow = 34;
constexpr int stripUsageRow = 34;

QString shortDuration(qint64 msecs)
{
    const qint64 minutes = msecs / 60000;
    if (minutes < 1) {
        return QStringLiteral("<1m");
    }
    if (minutes < 60) {
        return QStringLiteral("%1m").arg(minutes);
    }
    if (minutes < 48 * 60) {
        return QStringLiteral("%1h").arg(minutes / 60);
    }
    return QStringLiteral("%1d").arg(minutes / (24 * 60));
}

QString stateLabel(const Pane *pane)
{
    const QString since = shortDuration(QDateTime::currentMSecsSinceEpoch() - pane->stateSince());
    const PaneState state = pane->state();
    if (Theme::needsYou(state) || state == PaneState::Starting || state == PaneState::Exited) {
        return Theme::stateText(state) + QStringLiteral(" · ") + since;
    }
    if (state == PaneState::Background) {
        const qsizetype n = pane->backgroundTasks().size();
        return (n == 1 ? QStringLiteral("1 task running") : QStringLiteral("%1 tasks running").arg(n)) + QStringLiteral(" · ") + since;
    }
    return since;
}

QString paneTooltip(const Pane *pane, const QString &hint)
{
    QString state = Theme::stateText(pane->state());
    if (pane->state() == PaneState::Background) {
        state += QStringLiteral(": ") + pane->backgroundTasks().join(QStringLiteral(", "));
    }
    return QStringLiteral("%1 · %2\n%3\n%4\n\n%5").arg(pane->account().name, Config::collapseHome(pane->folder()), pane->title(), state, hint);
}

QToolButton *toolButton(const QString &icon, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setIcon(QIcon::fromTheme(icon));
    b->setToolTip(tip);
    return b;
}
}

// ---------------------------------------------------------------- CardList

CardList::CardList(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_hold = new QTimer(this);
    m_hold->setSingleShot(true);
    m_hold->setInterval(holdMsecs);
    connect(m_hold, &QTimer::timeout, this, &CardList::lift);
}

void CardList::setPanes(const QList<Pane *> &panes)
{
    // A pane that comes or goes ends a drag; the indexes would be wrong.
    m_panes = panes;
    m_hover = -1;
    m_pressed = -1;
    m_dragged = -1;
    m_hold->stop();
    unsetCursor();
    updateGeometry();
    update();
}

QSize CardList::sizeHint() const
{
    const int n = int(m_panes.size());
    return QSize(200, margin + n * (cardHeight + cardGap));
}

QSize CardList::minimumSizeHint() const
{
    return QSize(120, sizeHint().height());
}

QRectF CardList::cardRect(int index) const
{
    return QRectF(margin, margin / 2.0 + index * (cardHeight + cardGap), width() - 2 * margin, cardHeight);
}

int CardList::cardAt(const QPoint &pos) const
{
    for (int i = 0; i < m_panes.size(); ++i) {
        if (cardRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void CardList::paintCard(QPainter &p, const Pane *pane, const QRectF &r, int number, bool hover, bool lifted)
{
    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::WindowText);
    const QColor dim = pal.color(QPalette::PlaceholderText);
    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1);
    const QFontMetrics smallMetrics(small);
    const PaneState state = pane->state();
    const QColor stateColor = Theme::stateColor(state);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (lifted) {
        // A soft shadow under the lifted card.
        p.setPen(Qt::NoPen);
        for (int i = 1; i <= 4; ++i) {
            p.setBrush(QColor(0, 0, 0, 22));
            p.drawRoundedRect(r.adjusted(-i + 2, -i + 4, i - 2, i + 2), 6 + i, 6 + i);
        }
    }
    QColor bg = Theme::needsYou(state) ? Theme::withAlpha(stateColor, 0.16) : pal.color(QPalette::Window).lighter(130);
    if (hover || lifted) {
        bg = bg.lighter(115);
    }
    if (lifted && bg.alpha() < 255) {
        // Tinted cards are see-through; the lifted one must hide what it floats over.
        p.setPen(Qt::NoPen);
        p.setBrush(pal.color(QPalette::Window));
        p.drawRoundedRect(r, 6, 6);
    }
    p.setPen(lifted ? QPen(Theme::accent(), 2) : pane->isCurrent() ? QPen(Theme::accent(), 1.3) : QPen(Qt::NoPen));
    p.setBrush(bg);
    p.drawRoundedRect(r, 6, 6);
    QPainterPath clip;
    clip.addRoundedRect(r, 6, 6);
    p.setClipPath(clip);
    p.fillRect(QRectF(r.x(), r.y(), 4, r.height()), stateColor);
    p.restore();

    const qreal line1 = r.y() + 16;
    Theme::paintStateIcon(p, QPointF(r.x() + 18, line1), state);
    Theme::paintBadge(p, QRectF(r.x() + 32, line1 - 9, 18, 18), pane->account().key, pane->account().color);

    // Right side of line 1: state text with its duration, then the Alt number.
    p.setFont(small);
    p.setPen(dim);
    p.drawText(QRectF(r.right() - 18, line1 - 10, 12, 20), Qt::AlignVCenter | Qt::AlignRight, number <= 9 ? QString::number(number) : QString());
    const QString label = stateLabel(pane);
    const int labelWidth = smallMetrics.horizontalAdvance(label);
    p.setPen(Theme::needsYou(state) || state == PaneState::Exited || state == PaneState::Background ? stateColor : dim);
    p.drawText(QRectF(r.right() - 24 - labelWidth, line1 - 10, labelWidth + 2, 20), Qt::AlignVCenter | Qt::AlignLeft, label);

    const qreal folderX = r.x() + 56;
    const qreal folderWidth = r.right() - 32 - labelWidth - folderX;
    p.setFont(bold);
    p.setPen(text);
    p.drawText(QRectF(folderX, line1 - 10, folderWidth, 20),
               Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(bold).elidedText(QDir(pane->folder()).dirName(), Qt::ElideRight, int(folderWidth)));

    const QString title = pane->title().isEmpty() ? Config::collapseHome(pane->folder()) : pane->title();
    p.setFont(small);
    p.setPen(dim);
    p.drawText(QRectF(r.x() + 32, line1 + 10, r.width() - 40, 18),
               Qt::AlignVCenter | Qt::AlignLeft,
               smallMetrics.elidedText(title, Qt::ElideRight, int(r.width() - 40)));
}

void CardList::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);

    if (m_dragged < 0) {
        for (int i = 0; i < m_panes.size(); ++i) {
            paintCard(p, m_panes.at(i), cardRect(i), i + 1, i == m_hover, false);
        }
        return;
    }

    // While a card is lifted the others close ranks around the place it would land.
    QList<Pane *> others = m_panes;
    Pane *dragged = others.takeAt(m_dragged);
    for (int slot = 0; slot < m_panes.size(); ++slot) {
        if (slot == m_target) {
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Theme::withAlpha(Theme::accent(), 0.7), 1.2, Qt::DashLine));
            p.setBrush(Theme::withAlpha(Theme::accent(), 0.08));
            p.drawRoundedRect(cardRect(slot).adjusted(1, 1, -1, -1), 6, 6);
            p.restore();
            continue;
        }
        paintCard(p, others.at(slot < m_target ? slot : slot - 1), cardRect(slot), slot + 1, false, false);
    }
    const QRectF home = cardRect(0);
    paintCard(p, dragged, QRectF(home.x(), m_dragY, home.width(), home.height()), m_target + 1, false, true);
}

void CardList::lift()
{
    if (m_pressed < 0 || m_dragged >= 0) {
        return;
    }
    m_hold->stop();
    m_dragged = m_pressed;
    m_target = m_pressed;
    m_grab = m_pressPos.y() - cardRect(m_pressed).y();
    m_dragY = cardRect(m_pressed).y();
    setCursor(Qt::ClosedHandCursor);
    update();
}

void CardList::mousePressEvent(QMouseEvent *event)
{
    const int i = cardAt(event->position().toPoint());
    if (i < 0) {
        if (event->button() == Qt::LeftButton) {
            Q_EMIT emptyClicked();
        }
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_pressed = i;
        m_pressPos = event->position().toPoint();
        m_hold->start();
        Q_EMIT paneClicked(m_panes.at(i));
    } else if (event->button() == Qt::MiddleButton) {
        Q_EMIT paneCloseRequested(m_panes.at(i));
    }
}

void CardList::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (m_pressed >= 0 && m_dragged < 0 && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        lift();
    }
    if (m_dragged >= 0) {
        const qreal lowest = cardRect(int(m_panes.size()) - 1).y();
        m_dragY = qBound(cardRect(0).y(), pos.y() - m_grab, lowest);
        const qreal centre = m_dragY + cardHeight / 2.0 - margin / 2.0;
        m_target = qBound(0, int(centre / (cardHeight + cardGap)), int(m_panes.size()) - 1);
        update();
        return;
    }
    const int i = cardAt(pos);
    if (i != m_hover) {
        m_hover = i;
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void CardList::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    m_hold->stop();
    const int from = m_dragged;
    const int to = m_target;
    m_pressed = -1;
    m_dragged = -1;
    setCursor(cardAt(event->position().toPoint()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
    if (from >= 0 && to != from) {
        Q_EMIT paneMoved(m_panes.at(from), to);
    }
}

void CardList::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

bool CardList::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const int i = cardAt(help->pos());
        if (i >= 0 && m_dragged < 0) {
            QToolTip::showText(help->globalPos(),
                               paneTooltip(m_panes.at(i), QStringLiteral("Press and hold to move it. Middle click closes it.")),
                               this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

// ---------------------------------------------------------------- PaneStrip

PaneStrip::PaneStrip(const Config *config, const UsageModel *usage, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
    , m_usage(usage)
{
    connect(usage, &UsageModel::changed, this, qOverload<>(&QWidget::update));
}

void PaneStrip::setPanes(const QList<Pane *> &panes)
{
    m_panes = panes;
    update();
}

QRectF PaneStrip::rowRect(int index) const
{
    return QRectF(2, 4 + index * stripRow, width() - 5, 30);
}

int PaneStrip::rowAt(const QPoint &pos) const
{
    for (int i = 0; i < m_panes.size(); ++i) {
        if (rowRect(i).contains(pos) && rowRect(i).bottom() < usageTop() - 8) {
            return i;
        }
    }
    return -1;
}

qreal PaneStrip::usageTop() const
{
    return height() - m_config->accounts().size() * stripUsageRow - 12;
}

void PaneStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal w = width();
    const qreal top = usageTop();

    for (int i = 0; i < m_panes.size(); ++i) {
        const QRectF r = rowRect(i);
        if (r.bottom() >= top - 8) {
            break; // more panes than room; the cards show all of them when expanded
        }
        const Pane *pane = m_panes.at(i);
        const PaneState state = pane->state();
        if (Theme::needsYou(state)) {
            p.fillRect(r, Theme::withAlpha(Theme::stateColor(state), 0.13));
            p.fillRect(QRectF(r.x(), r.y(), 3, r.height()), Theme::stateColor(state));
        }
        if (pane->isCurrent()) {
            p.setPen(QPen(Theme::accent(), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 3, 3);
        }
        const qreal cy = r.center().y();
        Theme::paintStateIcon(p, QPointF(16, cy), state);
        p.setPen(Qt::NoPen);
        p.setBrush(pane->account().color);
        p.drawRoundedRect(QRectF(28, cy - 6, 6, 12), 2, 2);
    }

    // Two small bars per account: 5 hours above 7 days.
    p.fillRect(QRectF(8, top - 6, w - 17, 1), palette().color(QPalette::Mid));
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QColor track = Theme::withAlpha(palette().color(QPalette::WindowText), 0.10);
    p.setPen(Qt::NoPen);
    for (int k = 0; k < m_config->accounts().size(); ++k) {
        const Account &account = m_config->accounts().at(k);
        const AccountUsage *u = m_usage->usage(account.key);
        const QColor fill = !u || m_usage->stale(*u) ? Theme::withAlpha(account.color, 0.45) : account.color;
        const UsageWindow none;
        const UsageWindow windows[] = {u ? u->fiveHour : none, u ? u->sevenDay : none};
        for (int j = 0; j < 2; ++j) {
            const QRectF bar(8, top + k * stripUsageRow + 4 + j * 9, w - 17, 5);
            p.setBrush(track);
            p.drawRoundedRect(bar, 2.5, 2.5);
            const double percent = windows[j].known() && !windows[j].passed(now) ? windows[j].percent : 0;
            if (percent > 0) {
                p.setBrush(fill);
                p.drawRoundedRect(QRectF(bar.x(), bar.y(), qMax(bar.height(), bar.width() * qMin(percent, 100.0) / 100), bar.height()), 2.5, 2.5);
            }
        }
    }
}

void PaneStrip::mousePressEvent(QMouseEvent *event)
{
    const int i = rowAt(event->position().toPoint());
    if (i < 0) {
        // The usage bars at the bottom are not empty space.
        if (event->button() == Qt::LeftButton && event->position().y() < usageTop() - 6) {
            Q_EMIT emptyClicked();
        }
        return;
    }
    if (event->button() == Qt::LeftButton) {
        Q_EMIT paneClicked(m_panes.at(i));
    } else if (event->button() == Qt::MiddleButton) {
        Q_EMIT paneCloseRequested(m_panes.at(i));
    }
}

bool PaneStrip::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const int i = rowAt(help->pos());
        const qreal top = usageTop();
        if (i >= 0) {
            QToolTip::showText(help->globalPos(), paneTooltip(m_panes.at(i), QStringLiteral("Middle click closes it.")), this);
        } else if (help->pos().y() >= top - 6) {
            const int k = qBound(0, int((help->pos().y() - top) / stripUsageRow), int(m_config->accounts().size()) - 1);
            const Account &account = m_config->accounts().at(k);
            QToolTip::showText(help->globalPos(), QStringLiteral("%1 (%2)\n%3").arg(account.name, account.key, m_usage->shortText(account.key)), this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

// ---------------------------------------------------------------- Sidebar

Sidebar::Sidebar(const Config *config, const UsageModel *usage, QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Expanded: buttons, cards, usage block.
    m_full = new QWidget(this);
    auto *layout = new QVBoxLayout(m_full);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(4);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(8, 0, 8, 0);
    bar->setSpacing(4);

    auto *newButton = toolButton(QStringLiteral("list-add"), QStringLiteral("New pane (Ctrl+Shift+T)"), m_full);
    newButton->setText(QStringLiteral("New"));
    newButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(newButton, &QToolButton::clicked, this, &Sidebar::newPaneRequested);
    bar->addWidget(newButton);

    m_focusButton = toolButton(QStringLiteral("view-fullscreen"), QStringLiteral("Focus mode: one pane big (Ctrl+Shift+E)"), m_full);
    m_focusButton->setCheckable(true);
    connect(m_focusButton, &QToolButton::toggled, this, &Sidebar::focusModeToggled);
    bar->addWidget(m_focusButton);

    auto *equalButton = toolButton(QStringLiteral("distribute-horizontal-equal"), QStringLiteral("Equal sizes (Ctrl+Shift+0)"), m_full);
    connect(equalButton, &QToolButton::clicked, this, &Sidebar::equalSizesRequested);
    bar->addWidget(equalButton);

    auto *columnsButton = toolButton(QStringLiteral("view-grid"), QStringLiteral("Columns"), m_full);
    columnsButton->setPopupMode(QToolButton::InstantPopup);
    m_columnsMenu = new QMenu(columnsButton);
    auto *group = new QActionGroup(m_columnsMenu);
    for (int c = 0; c <= 4; ++c) {
        QAction *a = m_columnsMenu->addAction(c == 0 ? QStringLiteral("Automatic") : QStringLiteral("%1 columns").arg(c));
        a->setCheckable(true);
        a->setData(c);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, c] {
            Q_EMIT columnsRequested(c);
        });
    }
    columnsButton->setMenu(m_columnsMenu);
    bar->addWidget(columnsButton);
    bar->addStretch();

    auto *collapseButton = toolButton(QStringLiteral("sidebar-collapse-left"), QStringLiteral("Collapse the sidebar"), m_full);
    connect(collapseButton, &QToolButton::clicked, this, [this] {
        Q_EMIT collapseRequested(true);
    });
    bar->addWidget(collapseButton);
    layout->addLayout(bar);

    m_cards = new CardList(m_full);
    connect(m_cards, &CardList::paneClicked, this, &Sidebar::paneClicked);
    connect(m_cards, &CardList::paneCloseRequested, this, &Sidebar::paneCloseRequested);
    connect(m_cards, &CardList::paneMoved, this, &Sidebar::paneMoved);
    connect(m_cards, &CardList::emptyClicked, this, &Sidebar::emptyClicked);
    auto *scroll = new QScrollArea(m_full);
    scroll->setWidget(m_cards);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    layout->addWidget(scroll, 1);
    layout->addWidget(new UsageBlock(config, usage, m_full));
    outer->addWidget(m_full);

    // Collapsed: an expand button over the strip.
    m_stripBox = new QWidget(this);
    auto *stripLayout = new QVBoxLayout(m_stripBox);
    stripLayout->setContentsMargins(0, 8, 0, 0);
    stripLayout->setSpacing(4);
    auto *expandButton = toolButton(QStringLiteral("sidebar-expand-left"), QStringLiteral("Expand the sidebar"), m_stripBox);
    connect(expandButton, &QToolButton::clicked, this, [this] {
        Q_EMIT collapseRequested(false);
    });
    stripLayout->addWidget(expandButton, 0, Qt::AlignHCenter);
    m_strip = new PaneStrip(config, usage, m_stripBox);
    connect(m_strip, &PaneStrip::paneClicked, this, &Sidebar::paneClicked);
    connect(m_strip, &PaneStrip::paneCloseRequested, this, &Sidebar::paneCloseRequested);
    connect(m_strip, &PaneStrip::emptyClicked, this, &Sidebar::emptyClicked);
    stripLayout->addWidget(m_strip, 1);
    outer->addWidget(m_stripBox);
    m_stripBox->hide();

    // Durations on the cards ("4m") move on by themselves.
    auto *timer = new QTimer(this);
    timer->setInterval(20000);
    connect(timer, &QTimer::timeout, m_cards, qOverload<>(&QWidget::update));
    timer->start();
}

void Sidebar::setPanes(const QList<Pane *> &panes)
{
    m_cards->setPanes(panes);
    m_strip->setPanes(panes);
}

void Sidebar::refresh()
{
    m_cards->update();
    m_strip->update();
}

void Sidebar::setFocusMode(bool on)
{
    const QSignalBlocker blocker(m_focusButton);
    m_focusButton->setChecked(on);
}

void Sidebar::setColumns(int columns)
{
    for (QAction *a : m_columnsMenu->actions()) {
        a->setChecked(a->data().toInt() == columns);
    }
}

void Sidebar::mousePressEvent(QMouseEvent *event)
{
    // Presses nothing else took: the space around the buttons.
    if (event->button() == Qt::LeftButton) {
        Q_EMIT emptyClicked();
    }
}

void Sidebar::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    m_full->setVisible(!collapsed);
    m_stripBox->setVisible(collapsed);
}

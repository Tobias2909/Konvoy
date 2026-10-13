#include "sidebar.h"
#include "config.h"
#include "pane.h"
#include "theme.h"
#include "usageblock.h"

#include <QActionGroup>
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
}

// ---------------------------------------------------------------- CardList

CardList::CardList(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void CardList::setPanes(const QList<Pane *> &panes)
{
    m_panes = panes;
    m_hover = -1;
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

void CardList::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::WindowText);
    const QColor dim = pal.color(QPalette::PlaceholderText);

    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1);
    const QFontMetrics smallMetrics(small);

    for (int i = 0; i < m_panes.size(); ++i) {
        const Pane *pane = m_panes.at(i);
        const PaneState state = pane->state();
        const QColor stateColor = Theme::stateColor(state);
        const QRectF r = cardRect(i);

        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        QColor bg = Theme::needsYou(state) ? Theme::withAlpha(stateColor, 0.16) : pal.color(QPalette::Window).lighter(130);
        if (i == m_hover) {
            bg = bg.lighter(115);
        }
        p.setPen(pane->isCurrent() ? QPen(Theme::accent(), 1.3) : QPen(Qt::NoPen));
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
        const QString number = i < 9 ? QString::number(i + 1) : QString();
        p.setFont(small);
        p.setPen(dim);
        p.drawText(QRectF(r.right() - 18, line1 - 10, 12, 20), Qt::AlignVCenter | Qt::AlignRight, number);
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
}

void CardList::mousePressEvent(QMouseEvent *event)
{
    const int i = cardAt(event->position().toPoint());
    if (i < 0) {
        return;
    }
    if (event->button() == Qt::LeftButton) {
        Q_EMIT paneClicked(m_panes.at(i));
    } else if (event->button() == Qt::MiddleButton) {
        Q_EMIT paneCloseRequested(m_panes.at(i));
    }
}

void CardList::mouseMoveEvent(QMouseEvent *event)
{
    const int i = cardAt(event->position().toPoint());
    if (i != m_hover) {
        m_hover = i;
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
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
        if (i >= 0) {
            const Pane *pane = m_panes.at(i);
            QString state = Theme::stateText(pane->state());
            if (pane->state() == PaneState::Background) {
                state += QStringLiteral(": ") + pane->backgroundTasks().join(QStringLiteral(", "));
            }
            QToolTip::showText(help->globalPos(),
                               QStringLiteral("%1 · %2\n%3\n%4\n\nMiddle click closes the pane.")
                                   .arg(pane->account().name, Config::collapseHome(pane->folder()), pane->title(), state),
                               this);
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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(4);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(8, 0, 8, 0);
    bar->setSpacing(4);

    auto *newButton = new QToolButton(this);
    newButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    newButton->setText(QStringLiteral("New"));
    newButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    newButton->setToolTip(QStringLiteral("New pane (Ctrl+Shift+T)"));
    connect(newButton, &QToolButton::clicked, this, &Sidebar::newPaneRequested);
    bar->addWidget(newButton);

    m_focusButton = new QToolButton(this);
    m_focusButton->setIcon(QIcon::fromTheme(QStringLiteral("view-fullscreen")));
    m_focusButton->setCheckable(true);
    m_focusButton->setToolTip(QStringLiteral("Focus mode: one pane big (Ctrl+Shift+E)"));
    connect(m_focusButton, &QToolButton::toggled, this, &Sidebar::focusModeToggled);
    bar->addWidget(m_focusButton);

    auto *equalButton = new QToolButton(this);
    equalButton->setIcon(QIcon::fromTheme(QStringLiteral("distribute-horizontal-equal")));
    equalButton->setToolTip(QStringLiteral("Equal sizes (Ctrl+Shift+0)"));
    connect(equalButton, &QToolButton::clicked, this, &Sidebar::equalSizesRequested);
    bar->addWidget(equalButton);

    auto *columnsButton = new QToolButton(this);
    columnsButton->setIcon(QIcon::fromTheme(QStringLiteral("view-grid")));
    columnsButton->setToolTip(QStringLiteral("Columns"));
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
    layout->addLayout(bar);

    m_cards = new CardList(this);
    connect(m_cards, &CardList::paneClicked, this, &Sidebar::paneClicked);
    connect(m_cards, &CardList::paneCloseRequested, this, &Sidebar::paneCloseRequested);
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(m_cards);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    layout->addWidget(scroll, 1);
    layout->addWidget(new UsageBlock(config, usage, this));

    // Durations on the cards ("4m") move on by themselves.
    auto *timer = new QTimer(this);
    timer->setInterval(20000);
    connect(timer, &QTimer::timeout, m_cards, qOverload<>(&QWidget::update));
    timer->start();
}

void Sidebar::setPanes(const QList<Pane *> &panes)
{
    m_cards->setPanes(panes);
}

void Sidebar::refresh()
{
    m_cards->update();
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

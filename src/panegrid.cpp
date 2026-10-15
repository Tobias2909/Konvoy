#include "panegrid.h"
#include "pane.h"

#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

namespace
{
constexpr int handleWidth = 4;

QSplitter *makeSplitter(Qt::Orientation orientation, QWidget *parent)
{
    auto *s = new QSplitter(orientation, parent);
    s->setHandleWidth(handleWidth);
    s->setChildrenCollapsible(false);
    s->setOpaqueResize(true);
    return s;
}

void setEqual(QSplitter *s)
{
    const int count = s->count();
    if (count == 0) {
        return;
    }
    const int total = s->orientation() == Qt::Horizontal ? s->width() : s->height();
    s->setSizes(QList<int>(count, qMax(1, total / count)));
}
}

PaneGrid::PaneGrid(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_placeholder = new QLabel(QStringLiteral("No panes yet.\nPress Ctrl+Shift+T or New to open one."), this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setEnabled(false);
    layout->addWidget(m_placeholder);
}

QList<Pane *> PaneGrid::shownPanes() const
{
    QList<Pane *> shown;
    for (Pane *pane : m_panes) {
        if (!pane->isMinimized()) {
            shown.append(pane);
        }
    }
    return shown;
}

// The first shown pane from index on, else the last one before it.
Pane *PaneGrid::shownNear(qsizetype index) const
{
    for (qsizetype i = index; i < m_panes.size(); ++i) {
        if (!m_panes.at(i)->isMinimized()) {
            return m_panes.at(i);
        }
    }
    for (qsizetype i = qMin(index, m_panes.size()) - 1; i >= 0; --i) {
        if (!m_panes.at(i)->isMinimized()) {
            return m_panes.at(i);
        }
    }
    return nullptr;
}

int PaneGrid::effectiveColumns() const
{
    const int n = int(shownPanes().size());
    if (n == 0) {
        return 1;
    }
    if (m_columns > 0) {
        return qMin(m_columns, n);
    }
    // 1, 2 side by side, 3 as 2+1, 4 as 2x2, 5 and 6 as 3 columns, then 3 or 4 columns.
    if (n <= 2) {
        return n;
    }
    if (n <= 4) {
        return 2;
    }
    if (n <= 9) {
        return 3;
    }
    return 4;
}

void PaneGrid::addPane(Pane *pane)
{
    m_panes.append(pane);
    if (!m_current) {
        m_current = pane;
    }
    rebuild();
    Q_EMIT panesChanged();
}

void PaneGrid::removePane(Pane *pane)
{
    const qsizetype index = m_panes.indexOf(pane);
    if (index < 0) {
        return;
    }
    const QPointer<Pane> focus = holdFocus();
    m_panes.removeAt(index);
    pane->hide();
    pane->setParent(nullptr);
    if (m_current == pane) {
        m_current = shownNear(index);
    }
    rebuild();
    if (focus && focus != pane && !focus->isMinimized()) {
        focus->focusTerminal();
    }
    Q_EMIT panesChanged();
}

void PaneGrid::reorder(const std::function<void()> &change, Pane *focus)
{
    const QList<QList<int>> sizes = slotSizes();
    change();
    rebuild();
    setSlotSizes(sizes);
    if (focus) {
        focus->focusTerminal();
    }
    Q_EMIT panesChanged();
}

void PaneGrid::movePane(Pane *pane, int delta)
{
    // Counted in the grid, so minimized panes are skipped and keep their place.
    const QList<Pane *> shown = shownPanes();
    const qsizetype from = shown.indexOf(pane);
    const qsizetype to = from + delta;
    if (from < 0 || to < 0 || to >= shown.size()) {
        return;
    }
    const qsizetype i = m_panes.indexOf(pane);
    const qsizetype j = m_panes.indexOf(shown.at(to));
    reorder([&] {
        m_panes.swapItemsAt(i, j);
    }, pane);
}

void PaneGrid::movePaneTo(Pane *pane, int index)
{
    const qsizetype from = m_panes.indexOf(pane);
    if (from < 0 || index < 0 || index >= m_panes.size() || index == from) {
        return;
    }
    reorder([&] {
        m_panes.move(from, index);
    }, pane);
}

void PaneGrid::swapPanes(Pane *a, Pane *b)
{
    const qsizetype i = m_panes.indexOf(a);
    const qsizetype j = m_panes.indexOf(b);
    if (i < 0 || j < 0 || i == j) {
        return;
    }
    reorder([&] {
        m_panes.swapItemsAt(i, j);
    }, a);
}

QList<QList<int>> PaneGrid::slotSizes() const
{
    return m_focusMode ? m_overviewSizes : currentSlotSizes();
}

void PaneGrid::setSlotSizes(const QList<QList<int>> &sizes)
{
    if (m_focusMode) {
        m_overviewSizes = sizes;
    } else {
        applySlotSizes(sizes);
    }
}

QList<QList<int>> PaneGrid::currentSlotSizes() const
{
    QList<QList<int>> sizes;
    if (!m_rows) {
        return sizes;
    }
    sizes.append(m_rows->sizes());
    for (QSplitter *row : std::as_const(m_rowSplitters)) {
        sizes.append(row->sizes());
    }
    return sizes;
}

void PaneGrid::applySlotSizes(const QList<QList<int>> &sizes)
{
    // Sizes from another layout (other pane or column count) do not fit and are skipped.
    if (!m_rows || sizes.isEmpty()) {
        return;
    }
    if (sizes.first().size() == m_rows->count()) {
        m_rows->setSizes(sizes.first());
    }
    for (int i = 0; i < m_rowSplitters.size() && i + 1 < sizes.size(); ++i) {
        if (sizes.at(i + 1).size() == m_rowSplitters.at(i)->count()) {
            m_rowSplitters.at(i)->setSizes(sizes.at(i + 1));
        }
    }
}

void PaneGrid::movePaneVertically(Pane *pane, int rows)
{
    // One row down is one row's worth of panes further. Below a short last row the
    // pane goes to the last place.
    const QList<Pane *> shown = shownPanes();
    const qsizetype from = shown.indexOf(pane);
    if (from < 0) {
        return;
    }
    const qsizetype to = qBound(qsizetype(0), from + qsizetype(rows) * effectiveColumns(), shown.size() - 1);
    if (to != from) {
        movePane(pane, int(to - from));
    }
}

void PaneGrid::setCurrent(Pane *pane)
{
    if (m_current == pane || (pane && pane->isMinimized())) {
        return;
    }
    m_current = pane;
    if (m_focusMode) {
        applyFocusMode();
    }
}

void PaneGrid::setMinimized(Pane *pane, bool minimized)
{
    if (!m_panes.contains(pane) || pane->isMinimized() == minimized) {
        return;
    }
    pane->setMinimized(minimized);
    if (minimized && m_current == pane) {
        m_current = shownNear(m_panes.indexOf(pane));
    } else if (!minimized && !m_current) {
        m_current = pane;
    }
    rebuild();
    Q_EMIT panesChanged();
}

void PaneGrid::setFocusMode(bool on)
{
    if (on == m_focusMode) {
        return;
    }
    if (on) {
        m_overviewSizes = currentSlotSizes();
    }
    m_focusMode = on;
    applyFocusMode();
    if (!on) {
        if (m_overviewSizes.isEmpty()) {
            equalizeSizes();
        } else {
            applySlotSizes(m_overviewSizes);
        }
    }
    if (m_current) {
        m_current->focusTerminal();
    }
    Q_EMIT focusModeChanged(on);
}

void PaneGrid::setColumns(int columns)
{
    if (columns == m_columns) {
        return;
    }
    m_columns = columns;
    rebuild();
}

void PaneGrid::equalizeSizes()
{
    if (m_focusMode) {
        m_overviewSizes.clear(); // equal once the overview comes back
    } else {
        if (m_rows) {
            setEqual(m_rows);
        }
        for (QSplitter *row : std::as_const(m_rowSplitters)) {
            setEqual(row);
        }
    }
    Q_EMIT sizesChanged();
}

QPointer<Pane> PaneGrid::holdFocus()
{
    QWidget *focus = window()->focusWidget();
    for (Pane *pane : std::as_const(m_panes)) {
        if (focus && pane->isAncestorOf(focus)) {
            setFocus(Qt::OtherFocusReason);
            return pane;
        }
    }
    return nullptr;
}

void PaneGrid::rebuild()
{
    // Take the panes out, drop the old splitters, build a fresh grid. The panes'
    // terminals keep running while they move between parents.
    const QPointer<Pane> focus = holdFocus();
    for (Pane *pane : std::as_const(m_panes)) {
        pane->setParent(this);
        pane->hide();
    }
    delete m_rows;
    m_rows = nullptr;
    m_rowSplitters.clear();

    const QList<Pane *> shown = shownPanes();
    m_placeholder->setText(m_panes.isEmpty() ? QStringLiteral("No panes yet.\nPress Ctrl+Shift+T or New to open one.")
                                             : QStringLiteral("All panes are minimized.\nClick one in the sidebar to bring it back."));
    m_placeholder->setVisible(shown.isEmpty());
    if (shown.isEmpty()) {
        return;
    }

    const int cols = effectiveColumns();
    m_rows = makeSplitter(Qt::Vertical, this);
    connect(m_rows, &QSplitter::splitterMoved, this, &PaneGrid::sizesChanged);
    for (int i = 0; i < shown.size(); i += cols) {
        QSplitter *row = makeSplitter(Qt::Horizontal, m_rows);
        connect(row, &QSplitter::splitterMoved, this, &PaneGrid::sizesChanged);
        for (int j = i; j < qMin(i + cols, int(shown.size())); ++j) {
            row->addWidget(shown.at(j));
            shown.at(j)->show();
        }
        m_rows->addWidget(row);
        m_rowSplitters.append(row);
    }
    layout()->addWidget(m_rows);
    m_rows->show();
    if (m_focusMode) {
        m_overviewSizes.clear();
    } else {
        equalizeSizes();
    }
    applyFocusMode();
    if (focus && !focus->isMinimized()) {
        focus->focusTerminal();
    }
}

void PaneGrid::applyFocusMode()
{
    if (!m_rows) {
        return;
    }
    for (Pane *pane : std::as_const(m_panes)) {
        if (!pane->isMinimized()) {
            pane->setVisible(!m_focusMode || pane == m_current);
        }
    }
    for (QSplitter *row : std::as_const(m_rowSplitters)) {
        bool any = false;
        for (int i = 0; i < row->count(); ++i) {
            any = any || row->widget(i)->isVisibleTo(row);
        }
        row->setVisible(any);
    }
}

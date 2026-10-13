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

int PaneGrid::effectiveColumns() const
{
    const int n = int(m_panes.size());
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
    m_panes.removeAt(index);
    pane->hide();
    pane->setParent(nullptr);
    if (m_current == pane) {
        m_current = m_panes.isEmpty() ? nullptr : m_panes.at(qMin(index, m_panes.size() - 1));
    }
    rebuild();
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
    const qsizetype from = m_panes.indexOf(pane);
    const qsizetype to = from + delta;
    if (from < 0 || to < 0 || to >= m_panes.size()) {
        return;
    }
    reorder([&] {
        m_panes.swapItemsAt(from, to);
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
    const qsizetype from = m_panes.indexOf(pane);
    if (from < 0) {
        return;
    }
    const qsizetype to = qBound(qsizetype(0), from + qsizetype(rows) * effectiveColumns(), m_panes.size() - 1);
    if (to != from) {
        movePane(pane, int(to - from));
    }
}

void PaneGrid::setCurrent(Pane *pane)
{
    if (m_current == pane) {
        return;
    }
    m_current = pane;
    if (m_focusMode) {
        applyFocusMode();
    }
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

void PaneGrid::rebuild()
{
    // Take the panes out, drop the old splitters, build a fresh grid. The panes'
    // terminals keep running while they move between parents.
    for (Pane *pane : std::as_const(m_panes)) {
        pane->setParent(this);
        pane->hide();
    }
    delete m_rows;
    m_rows = nullptr;
    m_rowSplitters.clear();

    m_placeholder->setVisible(m_panes.isEmpty());
    if (m_panes.isEmpty()) {
        return;
    }

    const int cols = effectiveColumns();
    m_rows = makeSplitter(Qt::Vertical, this);
    connect(m_rows, &QSplitter::splitterMoved, this, &PaneGrid::sizesChanged);
    for (int i = 0; i < m_panes.size(); i += cols) {
        QSplitter *row = makeSplitter(Qt::Horizontal, m_rows);
        connect(row, &QSplitter::splitterMoved, this, &PaneGrid::sizesChanged);
        for (int j = i; j < qMin(i + cols, int(m_panes.size())); ++j) {
            row->addWidget(m_panes.at(j));
            m_panes.at(j)->show();
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
}

void PaneGrid::applyFocusMode()
{
    if (!m_rows) {
        return;
    }
    for (Pane *pane : std::as_const(m_panes)) {
        pane->setVisible(!m_focusMode || pane == m_current);
    }
    for (QSplitter *row : std::as_const(m_rowSplitters)) {
        bool any = false;
        for (int i = 0; i < row->count(); ++i) {
            any = any || row->widget(i)->isVisibleTo(row);
        }
        row->setVisible(any);
    }
}

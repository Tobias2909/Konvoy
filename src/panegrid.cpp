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

void PaneGrid::movePane(Pane *pane, int delta)
{
    const qsizetype from = m_panes.indexOf(pane);
    const qsizetype to = from + delta;
    if (from < 0 || to < 0 || to >= m_panes.size()) {
        return;
    }
    // Swap places and keep every slot's size.
    QList<QList<int>> sizes;
    for (QSplitter *row : std::as_const(m_rowSplitters)) {
        sizes.append(row->sizes());
    }
    const QList<int> rowSizes = m_rows ? m_rows->sizes() : QList<int>();
    m_panes.swapItemsAt(from, to);
    rebuild();
    if (m_rows && rowSizes.size() == m_rows->count()) {
        m_rows->setSizes(rowSizes);
    }
    for (int i = 0; i < m_rowSplitters.size() && i < sizes.size(); ++i) {
        if (sizes.at(i).size() == m_rowSplitters.at(i)->count()) {
            m_rowSplitters.at(i)->setSizes(sizes.at(i));
        }
    }
    pane->focusTerminal();
    Q_EMIT panesChanged();
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
    m_focusMode = on;
    applyFocusMode();
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
    if (m_rows) {
        setEqual(m_rows);
    }
    for (QSplitter *row : std::as_const(m_rowSplitters)) {
        setEqual(row);
    }
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
    for (int i = 0; i < m_panes.size(); i += cols) {
        QSplitter *row = makeSplitter(Qt::Horizontal, m_rows);
        for (int j = i; j < qMin(i + cols, int(m_panes.size())); ++j) {
            row->addWidget(m_panes.at(j));
            m_panes.at(j)->show();
        }
        m_rows->addWidget(row);
        m_rowSplitters.append(row);
    }
    layout()->addWidget(m_rows);
    m_rows->show();
    equalizeSizes();
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

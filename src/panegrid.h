#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

#include <functional>

class Pane;
class QLabel;
class QSplitter;

// The panes on the right. Overview mode puts them in an automatic grid of
// splitters, focus mode shows one of them big while the others keep running hidden.
// Minimized panes keep their place in the order but stay out of the grid.
class PaneGrid : public QWidget
{
    Q_OBJECT
public:
    explicit PaneGrid(QWidget *parent = nullptr);

    const QList<Pane *> &panes() const { return m_panes; }
    QList<Pane *> shownPanes() const; // the panes that are not minimized
    Pane *current() const { return m_current; }

    void addPane(Pane *pane);
    void removePane(Pane *pane);
    void movePane(Pane *pane, int delta); // swaps with the shown pane delta places away
    void movePaneVertically(Pane *pane, int rows);
    void movePaneTo(Pane *pane, int index); // takes it out and puts it in at index
    void swapPanes(Pane *a, Pane *b);
    void setCurrent(Pane *pane);
    void setMinimized(Pane *pane, bool minimized);

    bool focusMode() const { return m_focusMode; }
    void setFocusMode(bool on);

    int columns() const { return m_columns; }
    void setColumns(int columns); // 0 = automatic
    int effectiveColumns() const;

    void equalizeSizes();

    // The sizes of the grid slots: first the row heights, then each row's widths.
    QList<QList<int>> slotSizes() const;
    void setSlotSizes(const QList<QList<int>> &sizes);

Q_SIGNALS:
    void panesChanged();
    void focusModeChanged(bool on);
    void sizesChanged();

private:
    void rebuild();
    // Hiding the pane that has the keys would pass them on to the next pane in the
    // focus chain, which counts as looking at it. The grid holds them meanwhile.
    QPointer<Pane> holdFocus();
    Pane *shownNear(qsizetype index) const;
    QList<QList<int>> currentSlotSizes() const;
    void applySlotSizes(const QList<QList<int>> &sizes);
    // Changes the order of the panes; every slot keeps its size.
    void reorder(const std::function<void()> &change, Pane *focus);
    void applyFocusMode();

    QList<Pane *> m_panes;
    QPointer<Pane> m_current;
    bool m_focusMode = false;
    int m_columns = 0;

    // Focus mode hides rows, which squeezes their splitter. The overview sizes are
    // kept here meanwhile; empty means equal.
    QList<QList<int>> m_overviewSizes;

    QSplitter *m_rows = nullptr;
    QList<QSplitter *> m_rowSplitters;
    QLabel *m_placeholder = nullptr;
};

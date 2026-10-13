#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

class Pane;
class QLabel;
class QSplitter;

// The panes on the right. Overview mode puts them in an automatic grid of
// splitters, focus mode shows one of them big while the others keep running hidden.
class PaneGrid : public QWidget
{
    Q_OBJECT
public:
    explicit PaneGrid(QWidget *parent = nullptr);

    const QList<Pane *> &panes() const { return m_panes; }
    Pane *current() const { return m_current; }

    void addPane(Pane *pane);
    void removePane(Pane *pane);
    void movePane(Pane *pane, int delta);
    void movePaneVertically(Pane *pane, int rows);
    void setCurrent(Pane *pane);

    bool focusMode() const { return m_focusMode; }
    void setFocusMode(bool on);

    int columns() const { return m_columns; }
    void setColumns(int columns); // 0 = automatic
    int effectiveColumns() const;

    void equalizeSizes();

Q_SIGNALS:
    void panesChanged();
    void focusModeChanged(bool on);

private:
    void rebuild();
    void applyFocusMode();

    QList<Pane *> m_panes;
    QPointer<Pane> m_current;
    bool m_focusMode = false;
    int m_columns = 0;

    QSplitter *m_rows = nullptr;
    QList<QSplitter *> m_rowSplitters;
    QLabel *m_placeholder = nullptr;
};

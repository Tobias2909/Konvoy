#pragma once

#include <QList>
#include <QPoint>
#include <QWidget>

class Config;
class Pane;
class UsageModel;
class QToolButton;
class QMenu;
class QTimer;

// The list of pane cards. Clicking a card focuses its pane. Pressing and holding a
// card (or dragging it) lifts it, and dropping it somewhere else moves the pane there.
class CardList : public QWidget
{
    Q_OBJECT
public:
    explicit CardList(QWidget *parent = nullptr);

    void setPanes(const QList<Pane *> &panes);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void paneClicked(Pane *pane);
    void paneCloseRequested(Pane *pane);
    void paneMoved(Pane *pane, int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private:
    QRectF cardRect(int index) const;
    int cardAt(const QPoint &pos) const;
    void lift();
    void paintCard(QPainter &p, const Pane *pane, const QRectF &r, int number, bool hover, bool lifted);

    QList<Pane *> m_panes;
    int m_hover = -1;
    int m_pressed = -1;   // card under a held left button
    QPoint m_pressPos;
    QTimer *m_hold = nullptr;
    int m_dragged = -1;   // lifted card
    int m_target = -1;    // where it would land
    qreal m_dragY = 0;    // top of the lifted card
    qreal m_grab = 0;     // where in the card it was taken
};

// The collapsed sidebar: one row per pane with its state and account colour, and
// two small usage bars per account at the bottom.
class PaneStrip : public QWidget
{
    Q_OBJECT
public:
    PaneStrip(const Config *config, const UsageModel *usage, QWidget *parent = nullptr);
    void setPanes(const QList<Pane *> &panes);

Q_SIGNALS:
    void paneClicked(Pane *pane);
    void paneCloseRequested(Pane *pane);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    QRectF rowRect(int index) const;
    int rowAt(const QPoint &pos) const;
    qreal usageTop() const;

    const Config *m_config;
    const UsageModel *m_usage;
    QList<Pane *> m_panes;
};

class Sidebar : public QWidget
{
    Q_OBJECT
public:
    static constexpr int collapsedWidth = 44;

    Sidebar(const Config *config, const UsageModel *usage, QWidget *parent = nullptr);

    void setPanes(const QList<Pane *> &panes);
    void refresh();
    void setFocusMode(bool on);
    void setColumns(int columns);
    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);

Q_SIGNALS:
    void newPaneRequested();
    void focusModeToggled(bool on);
    void equalSizesRequested();
    void columnsRequested(int columns);
    void collapseRequested(bool collapsed);
    void paneClicked(Pane *pane);
    void paneCloseRequested(Pane *pane);
    void paneMoved(Pane *pane, int index);

private:
    QWidget *m_full;
    QWidget *m_stripBox;
    CardList *m_cards;
    PaneStrip *m_strip;
    QToolButton *m_focusButton;
    QMenu *m_columnsMenu;
    bool m_collapsed = false;
};

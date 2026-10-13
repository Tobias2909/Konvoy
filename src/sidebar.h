#pragma once

#include <QList>
#include <QWidget>

class Config;
class Pane;
class UsageModel;
class QToolButton;
class QMenu;

// The list of pane cards. Clicking a card focuses its pane.
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

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private:
    QRectF cardRect(int index) const;
    int cardAt(const QPoint &pos) const;

    QList<Pane *> m_panes;
    int m_hover = -1;
};

class Sidebar : public QWidget
{
    Q_OBJECT
public:
    Sidebar(const Config *config, const UsageModel *usage, QWidget *parent = nullptr);

    void setPanes(const QList<Pane *> &panes);
    void refresh();
    void setFocusMode(bool on);
    void setColumns(int columns);

Q_SIGNALS:
    void newPaneRequested();
    void focusModeToggled(bool on);
    void equalSizesRequested();
    void columnsRequested(int columns);
    void paneClicked(Pane *pane);
    void paneCloseRequested(Pane *pane);

private:
    CardList *m_cards;
    QToolButton *m_focusButton;
    QMenu *m_columnsMenu;
};

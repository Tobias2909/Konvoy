#pragma once

#include <QWidget>

class Config;
class UsageModel;
struct AccountUsage;

// Bottom of the sidebar: for every account its 5 hour and 7 day usage with the reset
// times, a marker while a usage reset is available, and the time of the numbers when
// no pane of the account is open. It only shows; it has no way to change anything on
// the account. Clicking
// a reset time switches all of them between the clock time and the time left.
class UsageBlock : public QWidget
{
    Q_OBJECT
public:
    UsageBlock(const Config *config, const UsageModel *model, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QString resetLine(const AccountUsage *usage, qint64 now) const; // "1 reset · expires 22 Oct 18:00"
    int accountHeight(const AccountUsage *usage, qint64 now) const;
    int contentHeight() const;
    QString tooltip(int index, qint64 now) const;
    bool onResetTime(const QPointF &pos) const;

    const Config *m_config;
    const UsageModel *m_model;
    int m_height = 0;
    bool m_timeLeft = false;           // reset times as "2h 15m" instead of "04:30"
    mutable QList<QRectF> m_resetRects; // where the reset times were painted
};

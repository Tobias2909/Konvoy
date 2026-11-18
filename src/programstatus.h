#pragma once

#include "config.h"

#include <QJsonObject>
#include <QList>
#include <QWidget>

class QProcess;
class QTimer;
class QToolButton;

// A program's block in the sidebar, above the usage block: its badge and name, a row of
// buttons, and one line about its state. The program says what to show: Konvoy runs its
// Status command every StatusEvery seconds and draws the JSON it prints. A click runs the
// same command with the button's id added, and draws what that prints. Konvoy knows
// nothing else about the program. See docs/design.md, "Program status".
class ProgramStatusBlock : public QWidget
{
    Q_OBJECT
public:
    ProgramStatusBlock(const Account &program, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    // A button with "pane": true asks for a new pane of this program instead.
    void paneRequested(const QString &key);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct Button {
        QString id;
        QString icon;
        QString tip;
        bool enabled = true;
        bool pane = false;
    };

    void poll();
    void run(const QString &button);
    void finished();
    void apply(const QJsonObject &status);
    void fail(const QString &why);
    void placeButtons();

    Account m_program;
    QProcess *m_process = nullptr;
    QTimer *m_timer;
    QTimer *m_deadline;
    QString m_pressing;      // the button whose command runs, empty for a poll
    QString m_queued;        // a click that came while a poll ran
    QString m_state = QStringLiteral("off");
    QString m_text;
    QList<Button> m_buttons;
    QList<QToolButton *> m_tools;
};

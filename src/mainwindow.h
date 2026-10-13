#pragma once

#include <QWidget>

#include "config.h"

class Pane;
class PaneGrid;
class Sidebar;
class QKeyEvent;
class QSplitter;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(Config *config, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens a pane without the dialog (command line --open).
    Pane *openPane(const QString &accountKey, const QString &folder);

public Q_SLOTS:
    void newPane();
    void closeCurrentPane();
    void closePane(Pane *pane);
    void jumpTo(int number);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupShortcuts();
    void adoptPane(Pane *pane);
    void makeCurrent(Pane *pane);
    void onFocusChanged(QWidget *old, QWidget *now);
    void onShortcutCheck(QKeyEvent *event, bool &override);
    void panesChanged();

    Config *m_config;
    QSplitter *m_split;
    Sidebar *m_sidebar;
    PaneGrid *m_grid;
};

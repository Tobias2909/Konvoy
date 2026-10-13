#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

#include "config.h"
#include "theme.h"

class Pane;
class PaneGrid;
class Sidebar;
class UsageModel;
class KNotification;
class QKeyEvent;
class QSplitter;
class QTimer;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(Config *config, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens a pane without the dialog (command line --open, restore). With a session id
    // the pane continues that chat.
    Pane *openPane(const QString &accountKey, const QString &folder, const QString &resumeSession = QString());
    // Reopens the panes of the last run with their layout and continues their chats.
    void restoreSession();

public Q_SLOTS:
    void newPane();
    void closeCurrentPane();
    void closePane(Pane *pane);
    void jumpTo(int number);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupShortcuts();
    void scheduleSave();
    void saveSession();
    void setSidebarCollapsed(bool collapsed);
    Pane *paneById(const QString &id) const;
    void adoptPane(Pane *pane);
    void makeCurrent(Pane *pane);
    void onFocusChanged(QWidget *old, QWidget *now);
    void onShortcutCheck(QKeyEvent *event, bool &override);
    void panesChanged();
    void onPaneState(Pane *pane, PaneState previous);
    void notify(Pane *pane);
    void showPane(Pane *pane);
    void tickSpinner();

    Config *m_config;
    QSplitter *m_split;
    UsageModel *m_usage;
    Sidebar *m_sidebar;
    PaneGrid *m_grid;
    QTimer *m_spinner;
    QHash<Pane *, QPointer<KNotification>> m_notifications;
    QTimer *m_saveTimer;
    bool m_restoring = false;
    bool m_closing = false;
    int m_sidebarWidth = 290;
};

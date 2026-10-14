#include "mainwindow.h"
#include "newpanedialog.h"
#include "pane.h"
#include "panegrid.h"
#include "shortcuts.h"
#include "sidebar.h"
#include "usage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMessageBox>
#include <QSplitter>
#include <QTimer>

#include <KConfigGroup>
#include <KNotification>
#include <KWindowConfig>
#include <KWindowSystem>

#include <QWindow>

namespace
{
constexpr int sidebarWidth = 290;
constexpr int sidebarMinWidth = 230;
constexpr int sidebarMaxWidth = 640;

// Slot sizes as "row heights;widths of row 1;widths of row 2", numbers split by commas.
QString encodeSizes(const QList<QList<int>> &sizes)
{
    QStringList parts;
    for (const QList<int> &list : sizes) {
        QStringList numbers;
        for (int n : list) {
            numbers.append(QString::number(n));
        }
        parts.append(numbers.join(QLatin1Char(',')));
    }
    return parts.join(QLatin1Char(';'));
}

QList<QList<int>> decodeSizes(const QString &text)
{
    QList<QList<int>> sizes;
    for (const QString &part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        QList<int> list;
        for (const QString &n : part.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            list.append(qMax(1, n.toInt()));
        }
        sizes.append(list);
    }
    return sizes;
}

QString describe(const Pane *pane)
{
    const QString title = pane->title().isEmpty() ? QString() : QStringLiteral(" · ") + pane->title();
    return QStringLiteral("%1 · %2%3").arg(pane->account().name, Config::collapseHome(pane->folder()), title);
}
}

MainWindow::MainWindow(Config *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    setWindowTitle(QStringLiteral("Konvoy"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->setHandleWidth(4);
    m_split->setChildrenCollapsible(false);
    m_usage = new UsageModel(this);
    m_usage->startHelper(config);
    m_sidebar = new Sidebar(config, m_usage, m_split);
    m_grid = new PaneGrid(m_split);
    m_split->addWidget(m_sidebar);
    m_split->addWidget(m_grid);
    m_split->setStretchFactor(0, 0);
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({sidebarWidth, 1600});
    m_sidebar->setMinimumWidth(sidebarMinWidth);
    m_sidebar->setMaximumWidth(sidebarMaxWidth);
    layout->addWidget(m_split);

    // The layout is saved a moment after it changes and once more on close.
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1000);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveSession);
    connect(m_split, &QSplitter::splitterMoved, this, [this] {
        if (!m_sidebar->isCollapsed()) {
            m_sidebarWidth = m_split->sizes().value(0, m_sidebarWidth);
        }
        scheduleSave();
    });
    connect(m_grid, &PaneGrid::sizesChanged, this, &MainWindow::scheduleSave);
    connect(m_grid, &PaneGrid::focusModeChanged, this, &MainWindow::scheduleSave);
    connect(m_grid, &PaneGrid::panesChanged, this, &MainWindow::scheduleSave);

    m_grid->setColumns(config->columns());
    m_sidebar->setColumns(config->columns());

    connect(m_sidebar, &Sidebar::newPaneRequested, this, &MainWindow::newPane);
    connect(m_sidebar, &Sidebar::focusModeToggled, m_grid, &PaneGrid::setFocusMode);
    connect(m_sidebar, &Sidebar::equalSizesRequested, m_grid, &PaneGrid::equalizeSizes);
    connect(m_sidebar, &Sidebar::columnsRequested, this, [this](int columns) {
        m_grid->setColumns(columns);
        m_config->setColumns(columns);
        m_sidebar->setColumns(columns);
    });
    connect(m_sidebar, &Sidebar::paneCloseRequested, this, &MainWindow::closePane);
    connect(m_sidebar, &Sidebar::paneMoved, this, [this](Pane *pane, int index) {
        m_grid->movePaneTo(pane, index);
    });
    connect(m_sidebar, &Sidebar::collapseRequested, this, &MainWindow::setSidebarCollapsed);
    connect(m_sidebar, &Sidebar::paneClicked, this, [this](Pane *pane) {
        makeCurrent(pane);
        pane->focusTerminal();
    });
    connect(m_grid, &PaneGrid::focusModeChanged, m_sidebar, &Sidebar::setFocusMode);
    connect(m_grid, &PaneGrid::panesChanged, this, &MainWindow::panesChanged);
    connect(qApp, &QApplication::focusChanged, this, &MainWindow::onFocusChanged);

    // The working icons turn while any pane works.
    m_spinner = new QTimer(this);
    m_spinner->setInterval(90);
    connect(m_spinner, &QTimer::timeout, this, &MainWindow::tickSpinner);

    setupShortcuts();
    resize(1600, 1000);
}

MainWindow::~MainWindow()
{
    // Closing the window moves the focus once more; the slot must not run on a
    // half destroyed window.
    disconnect(qApp, &QApplication::focusChanged, this, nullptr);
}

void MainWindow::setupShortcuts()
{
    auto add = [this](const QKeySequence &key, auto slot) {
        auto *action = new QAction(this);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
    };
    add(Shortcuts::newPane, [this] {
        newPane();
    });
    add(Shortcuts::closePane, [this] {
        closeCurrentPane();
    });
    add(Shortcuts::toggleFocus, [this] {
        m_grid->setFocusMode(!m_grid->focusMode());
    });
    add(Shortcuts::moveLeft, [this] {
        if (m_grid->current()) {
            m_grid->movePane(m_grid->current(), -1);
        }
    });
    add(Shortcuts::moveRight, [this] {
        if (m_grid->current()) {
            m_grid->movePane(m_grid->current(), 1);
        }
    });
    add(Shortcuts::moveUp, [this] {
        if (m_grid->current()) {
            m_grid->movePaneVertically(m_grid->current(), -1);
        }
    });
    add(Shortcuts::moveDown, [this] {
        if (m_grid->current()) {
            m_grid->movePaneVertically(m_grid->current(), 1);
        }
    });
    add(Shortcuts::equalSizes, [this] {
        m_grid->equalizeSizes();
    });
    for (int i = 1; i <= 9; ++i) {
        add(Shortcuts::jumpTo(i), [this, i] {
            jumpTo(i);
        });
    }
}

void MainWindow::newPane()
{
    QStringList open;
    for (const Pane *pane : m_grid->panes()) {
        open.append(pane->sessionId());
    }
    NewPaneDialog dialog(m_config, m_usage, open, this);
    if (dialog.exec() != QDialog::Accepted) {
        if (m_grid->current()) {
            m_grid->current()->focusTerminal();
        }
        return;
    }
    openPane(dialog.accountKey(), dialog.folder(), dialog.sessionId());
}

Pane *MainWindow::openPane(const QString &accountKey, const QString &folder, const QString &resumeSession)
{
    const Account *account = m_config->account(accountKey);
    if (!account) {
        qWarning("konvoy: unknown account %s", qPrintable(accountKey));
        return nullptr;
    }
    const QString dir = QDir::cleanPath(Config::expandHome(folder));
    m_config->addRecentFolder(dir);
    auto *pane = new Pane(m_config, *account, dir, resumeSession, m_grid);
    adoptPane(pane);
    return pane;
}

void MainWindow::adoptPane(Pane *pane)
{
    connect(pane, &Pane::changed, m_sidebar, &Sidebar::refresh);
    connect(pane, &Pane::shortcutCheck, this, &MainWindow::onShortcutCheck);
    connect(pane, &Pane::closeRequested, this, &MainWindow::closePane);
    connect(pane, &Pane::stateChanged, this, &MainWindow::onPaneState);
    connect(pane, &Pane::sessionChanged, this, &MainWindow::scheduleSave);
    connect(pane, &Pane::paneDropped, this, [this, pane](const QString &draggedId) {
        if (Pane *dragged = paneById(draggedId)) {
            m_grid->swapPanes(dragged, pane);
            makeCurrent(dragged);
        }
    });
    connect(pane, &QObject::destroyed, this, [this, pane] {
        if (QPointer<KNotification> n = m_notifications.take(pane)) {
            n->close();
        }
    });
    connect(pane, &Pane::terminalClosed, this, [this](Pane *p) {
        // The shell in the pane ended (exit or Ctrl+D at the prompt): the pane goes.
        m_grid->removePane(p);
        p->deleteLater();
    });
    m_grid->addPane(pane);
    makeCurrent(pane);
    pane->focusTerminal();
}

void MainWindow::closeCurrentPane()
{
    closePane(m_grid->current());
}

void MainWindow::closePane(Pane *pane)
{
    if (!pane) {
        return;
    }
    // Only a pane that works, waits for an answer or runs background tasks asks first.
    if (pane->isBusy()) {
        const PaneState state = pane->state();
        const QString doing = state == PaneState::Working  ? QStringLiteral("Claude is working")
            : state == PaneState::Background               ? QStringLiteral("Background tasks still run")
                                                           : QStringLiteral("Claude is waiting for you");
        const auto answer = QMessageBox::question(this,
                                                  QStringLiteral("Close pane"),
                                                  QStringLiteral("%1 in this pane:\n\n%2\n\nClose it anyway?").arg(doing, describe(pane)),
                                                  QMessageBox::Close | QMessageBox::Cancel,
                                                  QMessageBox::Cancel);
        if (answer != QMessageBox::Close) {
            return;
        }
    }
    m_grid->removePane(pane);
    delete pane;
    if (m_grid->current()) {
        makeCurrent(m_grid->current());
        m_grid->current()->focusTerminal();
    }
}

void MainWindow::jumpTo(int number)
{
    const QList<Pane *> &panes = m_grid->panes();
    if (number < 1 || number > panes.size()) {
        return;
    }
    Pane *pane = panes.at(number - 1);
    makeCurrent(pane);
    pane->focusTerminal();
}

void MainWindow::makeCurrent(Pane *pane)
{
    for (Pane *p : m_grid->panes()) {
        p->setCurrent(p == pane);
    }
    m_grid->setCurrent(pane);
}

void MainWindow::onFocusChanged(QWidget *, QWidget *now)
{
    for (QWidget *w = now; w; w = w->parentWidget()) {
        if (auto *pane = qobject_cast<Pane *>(w)) {
            if (!pane->isCurrent()) {
                makeCurrent(pane);
            }
            pane->markSeen();
            return;
        }
    }
}

void MainWindow::onPaneState(Pane *pane, PaneState)
{
    const PaneState state = pane->state();
    const bool onScreen = isActiveWindow() && pane->isVisible();

    // Finishing in front of the user's eyes needs no "done" marker.
    if (state == PaneState::Done && onScreen && pane->isCurrent()) {
        pane->markSeen();
        return;
    }

    if (QPointer<KNotification> old = m_notifications.take(pane)) {
        old->close();
    }
    if (state == PaneState::Permission || state == PaneState::Question || state == PaneState::Done) {
        // Only a state that holds for a moment is worth a notification.
        const qint64 since = pane->stateSince();
        QPointer<Pane> target(pane);
        QTimer::singleShot(1500, this, [this, target, state, since] {
            if (target && target->state() == state && target->stateSince() == since) {
                notify(target);
            }
        });
    }

    bool anyWorking = false;
    for (const Pane *p : m_grid->panes()) {
        anyWorking = anyWorking || p->state() == PaneState::Working;
    }
    if (anyWorking && !m_spinner->isActive()) {
        m_spinner->start();
    } else if (!anyWorking) {
        m_spinner->stop();
    }
    m_sidebar->refresh();
}

void MainWindow::notify(Pane *pane)
{
    // Only when the pane is out of sight: hidden by focus mode, or the window is not active.
    if (isActiveWindow() && pane->isVisible()) {
        return;
    }
    const PaneState state = pane->state();
    const QString event = state == PaneState::Permission ? QStringLiteral("permission")
        : state == PaneState::Question                   ? QStringLiteral("question")
                                                         : QStringLiteral("done");
    auto *n = new KNotification(event, KNotification::CloseOnTimeout);
    n->setTitle(QStringLiteral("%1 · %2").arg(pane->account().key, QDir(pane->folder()).dirName()));
    QString text = Theme::stateText(state);
    if (!pane->title().isEmpty()) {
        text += QStringLiteral(" · ") + pane->title();
    }
    n->setText(text);
    n->setIconName(QStringLiteral("utilities-terminal"));
    KNotificationAction *show = n->addDefaultAction(QStringLiteral("Show"));
    QPointer<Pane> target(pane);
    connect(show, &KNotificationAction::activated, this, [this, n, target] {
        KWindowSystem::setCurrentXdgActivationToken(n->xdgActivationToken());
        if (target) {
            showPane(target);
        }
    });
    m_notifications.insert(pane, n);
    n->sendEvent();
}

void MainWindow::showPane(Pane *pane)
{
    show();
    raise();
    activateWindow();
    makeCurrent(pane);
    pane->focusTerminal();
}

void MainWindow::tickSpinner()
{
    Theme::advanceSpin();
    m_sidebar->refresh();
    for (Pane *p : m_grid->panes()) {
        if (p->state() == PaneState::Working) {
            p->refreshHeader();
        }
    }
}

void MainWindow::onShortcutCheck(QKeyEvent *event, bool &override)
{
    // Let Konvoy's keys through the terminal, everything else stays with the terminal.
    const QKeySequence pressed(event->modifiers() | Qt::Key(event->key()));
    if (Shortcuts::all().contains(pressed)) {
        override = false;
    }
}

void MainWindow::panesChanged()
{
    m_sidebar->setPanes(m_grid->panes());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QStringList running;
    for (const Pane *pane : m_grid->panes()) {
        if (pane->isBusy()) {
            running.append(QStringLiteral("• ") + describe(pane));
        }
    }
    if (!running.isEmpty()) {
        const auto answer = QMessageBox::warning(this,
                                                 QStringLiteral("Close Konvoy"),
                                                 QStringLiteral("Claude is working, waiting for you or running background tasks in %1:\n\n%2\n\nClose Konvoy anyway?")
                                                     .arg(running.size() == 1 ? QStringLiteral("1 pane") : QStringLiteral("%1 panes").arg(running.size()),
                                                          running.join(QLatin1Char('\n'))),
                                                 QMessageBox::Close | QMessageBox::Cancel,
                                                 QMessageBox::Cancel);
        if (answer != QMessageBox::Close) {
            event->ignore();
            return;
        }
    }
    // Saved before the panes go: their shells end with the window and would leave
    // an empty layout behind.
    saveSession();
    m_closing = true;
    event->accept();
}

Pane *MainWindow::paneById(const QString &id) const
{
    for (Pane *pane : m_grid->panes()) {
        if (pane->id() == id) {
            return pane;
        }
    }
    return nullptr;
}

void MainWindow::setSidebarCollapsed(bool collapsed)
{
    if (collapsed == m_sidebar->isCollapsed()) {
        return;
    }
    const int total = m_split->width();
    if (collapsed) {
        // Before the window is laid out the splitter has no real width yet.
        const int width = m_split->sizes().value(0, 0);
        if (m_sidebar->isVisible() && width >= sidebarMinWidth) {
            m_sidebarWidth = width;
        }
        m_sidebar->setCollapsed(true);
        m_sidebar->setFixedWidth(Sidebar::collapsedWidth);
        // The splitter keeps the old width for the sidebar otherwise, as empty space.
        m_split->setSizes({Sidebar::collapsedWidth, qMax(1, total - Sidebar::collapsedWidth)});
    } else {
        m_sidebar->setCollapsed(false);
        m_sidebar->setMinimumWidth(sidebarMinWidth);
        m_sidebar->setMaximumWidth(sidebarMaxWidth);
        m_split->setSizes({m_sidebarWidth, qMax(1, total - m_sidebarWidth)});
    }
    scheduleSave();
}

void MainWindow::scheduleSave()
{
    if (!m_restoring && !m_closing) {
        m_saveTimer->start();
    }
}

void MainWindow::saveSession()
{
    if (m_restoring || m_closing) {
        return;
    }
    m_saveTimer->stop();
    KSharedConfigPtr state = m_config->state();
    for (const QString &group : state->groupList()) {
        if (group.startsWith(QLatin1String("Pane "))) {
            state->deleteGroup(group);
        }
    }
    const QList<Pane *> &panes = m_grid->panes();
    KConfigGroup layout = state->group(QStringLiteral("Layout"));
    layout.writeEntry("Panes", int(panes.size()));
    layout.writeEntry("Current", int(panes.indexOf(m_grid->current())));
    layout.writeEntry("FocusMode", m_grid->focusMode());
    layout.writeEntry("Slots", encodeSizes(m_grid->slotSizes()));
    layout.writeEntry("SidebarWidth", m_sidebarWidth);
    layout.writeEntry("SidebarCollapsed", m_sidebar->isCollapsed());
    if (windowHandle()) {
        KWindowConfig::saveWindowSize(windowHandle(), layout);
    }
    for (int i = 0; i < panes.size(); ++i) {
        KConfigGroup g = state->group(QStringLiteral("Pane %1").arg(i));
        g.writeEntry("Account", panes.at(i)->account().key);
        g.writeEntry("Folder", Config::collapseHome(panes.at(i)->folder()));
        g.writeEntry("Session", panes.at(i)->sessionId());
    }
    state->sync();
}

void MainWindow::restoreSession()
{
    const KConfigGroup layout = m_config->state()->group(QStringLiteral("Layout"));
    m_restoring = true;
    create();
    KWindowConfig::restoreWindowSize(windowHandle(), layout);
    resize(windowHandle()->size());

    m_sidebarWidth = qBound(sidebarMinWidth, layout.readEntry("SidebarWidth", sidebarWidth), sidebarMaxWidth);
    m_split->setSizes({m_sidebarWidth, qMax(1, width() - m_sidebarWidth)});

    const int count = layout.readEntry("Panes", 0);
    for (int i = 0; i < count; ++i) {
        const KConfigGroup g = m_config->state()->group(QStringLiteral("Pane %1").arg(i));
        const QString folder = g.readEntry("Folder", QString());
        QString key = g.readEntry("Account", QString());
        if (!m_config->account(key)) {
            key = m_config->accounts().first().key; // the account is gone from the config
        }
        if (!folder.isEmpty()) {
            openPane(key, folder, g.readEntry("Session", QString()));
        }
    }
    // Slot sizes only stick once the window has its size; before that the panes'
    // minimum sizes even them out.
    const QList<QList<int>> sizes = decodeSizes(layout.readEntry("Slots", QString()));
    QTimer::singleShot(0, this, [this, sizes] {
        m_grid->setSlotSizes(sizes);
    });
    const QList<Pane *> &panes = m_grid->panes();
    const int current = layout.readEntry("Current", 0);
    if (current >= 0 && current < panes.size()) {
        makeCurrent(panes.at(current));
        panes.at(current)->focusTerminal();
    }
    m_grid->setFocusMode(layout.readEntry("FocusMode", false));
    m_restoring = false;
    setSidebarCollapsed(layout.readEntry("SidebarCollapsed", false));
}

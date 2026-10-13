#include "mainwindow.h"
#include "newpanedialog.h"
#include "pane.h"
#include "panegrid.h"
#include "shortcuts.h"
#include "sidebar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMessageBox>
#include <QSplitter>

namespace
{
constexpr int sidebarWidth = 290;

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
    m_split->setHandleWidth(1);
    m_split->setChildrenCollapsible(false);
    m_sidebar = new Sidebar(m_split);
    m_grid = new PaneGrid(m_split);
    m_split->addWidget(m_sidebar);
    m_split->addWidget(m_grid);
    m_split->setStretchFactor(0, 0);
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({sidebarWidth, 1600});
    layout->addWidget(m_split);

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
    connect(m_sidebar, &Sidebar::paneClicked, this, [this](Pane *pane) {
        makeCurrent(pane);
        pane->focusTerminal();
    });
    connect(m_grid, &PaneGrid::focusModeChanged, m_sidebar, &Sidebar::setFocusMode);
    connect(m_grid, &PaneGrid::panesChanged, this, &MainWindow::panesChanged);
    connect(qApp, &QApplication::focusChanged, this, &MainWindow::onFocusChanged);

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
    NewPaneDialog dialog(m_config, this);
    if (dialog.exec() != QDialog::Accepted) {
        if (m_grid->current()) {
            m_grid->current()->focusTerminal();
        }
        return;
    }
    openPane(dialog.accountKey(), dialog.folder());
}

Pane *MainWindow::openPane(const QString &accountKey, const QString &folder)
{
    const Account *account = m_config->account(accountKey);
    if (!account) {
        qWarning("konvoy: unknown account %s", qPrintable(accountKey));
        return nullptr;
    }
    const QString dir = QDir::cleanPath(Config::expandHome(folder));
    m_config->addRecentFolder(dir);
    auto *pane = new Pane(m_config, *account, dir, m_grid);
    adoptPane(pane);
    return pane;
}

void MainWindow::adoptPane(Pane *pane)
{
    connect(pane, &Pane::changed, m_sidebar, &Sidebar::refresh);
    connect(pane, &Pane::shortcutCheck, this, &MainWindow::onShortcutCheck);
    connect(pane, &Pane::closeRequested, this, &MainWindow::closePane);
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
    if (pane->claudeRunning()) {
        const auto answer = QMessageBox::question(this,
                                                  QStringLiteral("Close pane"),
                                                  QStringLiteral("Claude is still running in this pane:\n\n%1\n\nClose it anyway?").arg(describe(pane)),
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
            return;
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
        if (pane->claudeRunning()) {
            running.append(QStringLiteral("• ") + describe(pane));
        }
    }
    if (!running.isEmpty()) {
        const auto answer = QMessageBox::warning(this,
                                                 QStringLiteral("Close Konvoy"),
                                                 QStringLiteral("Claude is still running in %1:\n\n%2\n\nClose Konvoy anyway?")
                                                     .arg(running.size() == 1 ? QStringLiteral("1 pane") : QStringLiteral("%1 panes").arg(running.size()),
                                                          running.join(QLatin1Char('\n'))),
                                                 QMessageBox::Close | QMessageBox::Cancel,
                                                 QMessageBox::Cancel);
        if (answer != QMessageBox::Close) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

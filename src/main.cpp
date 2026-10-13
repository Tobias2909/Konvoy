#include "config.h"
#include "mainwindow.h"
#include "pane.h"

#include <KAboutData>
#include <KDBusService>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QProcessEnvironment>
#include <QTimer>

namespace
{
// A Konvoy started from inside a Claude Code session (or any shell that ran one)
// inherits CLAUDECODE, CLAUDE_CONFIG_DIR and friends. The panes must start clean.
void clearClaudeEnvironment()
{
    const QStringList env = QProcessEnvironment::systemEnvironment().keys();
    for (const QString &name : env) {
        if (name.startsWith(QLatin1String("CLAUDE"))) {
            qunsetenv(name.toLocal8Bit().constData());
        }
    }
}
}

int main(int argc, char **argv)
{
    clearClaudeEnvironment();

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationDomain(QStringLiteral("tobias2909.github.io"));

    KAboutData about(QStringLiteral("konvoy"),
                     QStringLiteral("Konvoy"),
                     QStringLiteral(PROJECT_VERSION_STRING),
                     QStringLiteral("Several Claude Code sessions side by side in one window"),
                     KAboutLicense::MIT);
    about.setDesktopFileName(QStringLiteral("io.github.tobias2909.konvoy"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("Use this config file instead of konvoyrc."), QStringLiteral("file"));
    const QCommandLineOption openOption(QStringLiteral("open"),
                                        QStringLiteral("Open a pane right away, e.g. --open B:~/project. Can be repeated."),
                                        QStringLiteral("account:folder"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Save a picture of the window after a few seconds and quit (for testing)."),
                                              QStringLiteral("file"));
    const QCommandLineOption delayOption(QStringLiteral("screenshot-delay"), QStringLiteral("Milliseconds before the test picture."), QStringLiteral("ms"), QStringLiteral("4000"));
    parser.addOption(configOption);
    parser.addOption(delayOption);
    parser.addOption(openOption);
    parser.addOption(screenshotOption);
    parser.process(app);
    about.processCommandLine(&parser);

    const bool screenshot = parser.isSet(screenshotOption);
    // One window for everything: a second start raises the first one. Test runs
    // with --screenshot stay separate so they never touch a running Konvoy.
    std::unique_ptr<KDBusService> service;
    if (!screenshot) {
        service = std::make_unique<KDBusService>(KDBusService::Unique);
    }

    Config config(parser.value(configOption));
    MainWindow window(&config);
    if (service) {
        QObject::connect(service.get(), &KDBusService::activateRequested, &window, [&window] {
            window.show();
            window.raise();
            window.activateWindow();
        });
    }

    for (const QString &spec : parser.values(openOption)) {
        const qsizetype colon = spec.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            qWarning("konvoy: --open expects account:folder, got %s", qPrintable(spec));
            continue;
        }
        window.openPane(spec.left(colon), spec.mid(colon + 1));
    }

    window.show();

    if (screenshot) {
        const QString file = parser.value(screenshotOption);
        QTimer::singleShot(parser.value(delayOption).toInt(), &window, [&window, file] {
            window.grab().save(file);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// Claude Code keeps every chat as <config dir>/projects/<folder>/<session id>.jsonl.
struct Chat {
    QString id;
    QString title; // the name given with /rename, else Claude's own title, else the last prompt
    QDateTime modified;
};

namespace Chats
{
// "/home/me/my.app" becomes "-home-me-my-app", as Claude Code names the folder.
QString projectFolderName(const QString &folder);

// The latest chats in a folder, newest first, from the projects folders of the given
// config dirs (shared projects folders count once). Chats without any text are left out.
QList<Chat> recent(const QStringList &configDirs, const QString &folder, int max = 15);

// "5 min ago", "3 h ago", "yesterday", "12 Oct"
QString age(const QDateTime &time);
}

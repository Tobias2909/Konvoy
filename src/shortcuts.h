#pragma once

#include <QKeySequence>
#include <QList>

// Konvoy's own keys. The embedded terminal must let these through, and Konsole
// part actions that use one of them lose it.
namespace Shortcuts
{
inline const QKeySequence newPane{Qt::CTRL | Qt::SHIFT | Qt::Key_T};
inline const QKeySequence closePane{Qt::CTRL | Qt::SHIFT | Qt::Key_W};
inline const QKeySequence minimizePane{Qt::CTRL | Qt::SHIFT | Qt::Key_M};
inline const QKeySequence toggleFocus{Qt::CTRL | Qt::SHIFT | Qt::Key_E};
inline const QKeySequence moveLeft{Qt::CTRL | Qt::SHIFT | Qt::Key_Left};
inline const QKeySequence moveRight{Qt::CTRL | Qt::SHIFT | Qt::Key_Right};
inline const QKeySequence moveUp{Qt::CTRL | Qt::SHIFT | Qt::Key_Up};
inline const QKeySequence moveDown{Qt::CTRL | Qt::SHIFT | Qt::Key_Down};
inline const QKeySequence equalSizes{Qt::CTRL | Qt::SHIFT | Qt::Key_0};

inline QKeySequence jumpTo(int number) // 1 to 9
{
    return QKeySequence(Qt::ALT | Qt::Key(Qt::Key_0 + number));
}

inline QList<QKeySequence> all()
{
    QList<QKeySequence> keys{newPane, closePane, minimizePane, toggleFocus, moveLeft, moveRight, moveUp, moveDown, equalSizes};
    for (int i = 1; i <= 9; ++i) {
        keys.append(jumpTo(i));
    }
    return keys;
}
}

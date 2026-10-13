# Konvoy

Konvoy runs several Claude Code sessions side by side in one KDE window. Every session is a real Konsole terminal, and a slim sidebar shows which session is working and which one waits for you. It also shows how much of the 5 hour and 7 day limits each of your accounts has used and when they reset.

Konvoy is at an early stage. The plan lives in [docs/design.md](docs/design.md).

## Setup

Konvoy needs KDE Frameworks 6 with Konsole, CMake and extra cmake modules to build, and bash and Python 3 for its helpers.

```
cmake -B build -DCMAKE_INSTALL_PREFIX=~/.local
cmake --build build
cmake --install build
cp konvoyrc.example ~/.config/konvoyrc
konvoy --install-hooks
```

The last step adds Konvoy's hooks to the Claude Code settings of every account in your config and shows them before it writes. Your other hooks stay as they are. `konvoy --remove-hooks` takes Konvoy's hooks out again.

## License

MIT

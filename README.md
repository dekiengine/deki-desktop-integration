# Deki Desktop Integration

Docs: https://dekiengine.github.io/deki-desktop-integration/ (components and properties, generated from the code)

The desktop and web targets for the Deki Engine: the `native_simulator` platform (shown as Desktop), which builds a game as a program for Windows, Linux or macOS, the `web` platform, which builds it for a web browser, and the builders that make them. The window, input and time are `deki-sdl3-integration`'s.

A game built as a program starts at the engine's own `main()`, which sets up the memory and the file system (`F:/` is `./flash/`, `S:/` is `./storage/`); this package has none of its own. Other targets that run as a program, such as the Miyoo Mini, use the same entry without this package.

Part of [Deki Engine](https://github.com/dekiengine/deki-engine).

## Web

Choose **Web** in the Build panel and press **Build & Open in Browser**: the
game is built with Emscripten into a web page and opens in your browser. The Build panel
installs Emscripten the first time (about 2 GB, in
`~/.deki-editor/toolchains/emsdk`). It is the same project as on a board or
the desktop.

- The page is `index.html` with `index.js`, `index.wasm` and `index.data` (the
  game's assets), in `generated/build/web/build/bin/Release`. Put the four
  files on any web server to share the game.
- WebGL 2, no threads, and the game shows at its own resolution (960x540 in
  the platform; change `screenWidth` and `screenHeight` in the project's copy).
- Keys and the mouse reach the game as on the desktop.

## Namespace

Types live in `DekiDesktop`. Scene files store the qualified name, and so does code:

```cpp
using namespace DekiDesktop;
obj->AddComponent<SomeComponent>();
```

## Install

Package Manager in the Deki Editor, or `DekiEditor --packages-add deki-desktop-integration <project>`.

## Dependencies

None.

## Community

Questions, ideas and what you made with Deki: [the Deki Discord](https://dekiengine.com/discord/).

## License

Apache 2.0. See [LICENSE](LICENSE).

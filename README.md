# Deki Desktop Integration

Docs: https://dekiengine.github.io/deki-desktop-integration/ (components and properties, generated from the code)

The desktop simulator for the Deki Engine: the `native_simulator` platform, which runs a game as a program on Windows, Linux or macOS, and the builder that makes it. The window, input and time are `deki-sdl3-integration`'s.

A game built as a program starts at the engine's own `main()`, which sets up the memory and the file system (`F:/` is `./flash/`, `S:/` is `./storage/`); this package has none of its own. Other targets that run as a program, such as the Miyoo Mini, use the same entry without this package.

Part of [Deki Engine](https://github.com/dekiengine/deki-engine).

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

# Deki Desktop Integration

Docs: https://dekiengine.github.io/deki-desktop-integration/ (components and properties, generated from the code)

Desktop platform HAL (Hardware Abstraction Layer) for the Deki Engine: memory management and filesystem support.

Part of [Deki Engine](https://github.com/dekiengine/deki-engine).

## Namespace

Types live in `DekiDesktop`. Scene files store the qualified name, and so does code:

```cpp
using namespace DekiDesktop;
obj->AddComponent<SomeComponent>();
```

Scenes saved before 0.16.0 used bare names and still load; saving writes the current one.

## Install

Package Manager in the Deki Editor, or `DekiEditor --packages-add deki-desktop-integration <project>`.

## Dependencies

None.

## License

Apache 2.0. See [LICENSE](LICENSE).

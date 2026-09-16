# Deki Desktop Integration

Documentation: https://dekiengine.github.io/deki-desktop-integration/ (components and properties, generated from the code)

Desktop platform HAL (Hardware Abstraction Layer) for the Deki Engine: memory management and filesystem support.

Part of the [Deki Engine](https://github.com/dekiengine/deki-engine) package ecosystem.

## Namespace

This package's types live in `DekiDesktop`. Scene files store the qualified
name, so a component is `DekiDesktop::SomeComponent` there, and code naming one
needs the namespace:

```cpp
using namespace DekiDesktop;
obj->AddComponent<SomeComponent>();
```

Scenes saved before 0.16.0 used bare names and still load: every component
records what it used to be called, and a save writes the current name.

## Installation

Install via the Package Manager inside the Deki Editor.

## Dependencies

None.

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

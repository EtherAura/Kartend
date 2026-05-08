# Contributing to Kartend

Thank you for considering contributing to Kartend! This document outlines the process for contributing.

## Development Setup

1. Install dependencies (see [docs/building.md](docs/building.md)):
   - Qt6 (Core, Gui, Widgets, Sql, Concurrent, Multimedia, MultimediaWidgets)
   - CMake 3.20+
   - C++23 compiler (Clang or GCC)

2. Build:
   ```bash
   .scripts/build.sh --debug
   ```

3. Run tests:
   ```bash
   .scripts/build.sh --tests --run-tests
   ```

## Submitting Changes

1. Fork the repository and create a feature branch from `main`.
2. Make your changes. Follow the existing code style (enforced by `.clang-format` and `.clang-tidy`).
3. Add tests for new functionality where applicable.
4. Ensure `build.sh --maintenance` passes (format, lint, and build checks).
5. Open a pull request with a clear description of the changes.

## Code Style

- 2-space indentation, 100 column limit
- Implicit boolean null checks (`if (!ptr)` not `if (ptr == nullptr)`)
- All `QTimer::singleShot` calls must have a comment explaining the delay
- UI constants go in `src/ui/uiconstants.h` — no magic numbers
- Use `[[nodiscard]]` on const getters and factory functions
- See [.github/copilot-instructions.md](.github/copilot-instructions.md) for full conventions

## Architecture

See [docs/architecture.md](docs/architecture.md) for module hierarchy, signal flow, and ownership model.

## Reporting Issues

Use the GitHub issue templates for bugs and feature requests.

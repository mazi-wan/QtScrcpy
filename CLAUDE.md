# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Build with CMake

```bash
# Linux build
./ci/linux/build_for_linux.sh "Debug"

# Using CMake directly
cmake -DCMAKE_PREFIX_PATH=$qt_cmake_path -DCMAKE_BUILD_TYPE=Debug -S . -B build
cmake --build build --config Debug -j8

# Output location
./output/x64/Debug/
```

### Required Dependencies

- Qt 5.12+ (Qt 6 supported)
- CMake 3.19+
- MSVC 2019+ (Windows)
- C++11 compatible compiler

## High-Level Architecture

QtScrcpy follows a client-server architecture where:

- **QtScrcpy client** runs on desktop (Linux/Windows/macOS)
- **scrcpy-server** runs on Android device

### Core Components

1. **QtScrcpyCore** (`QtScrcpy/QtScrcpyCore/`) - Core library handling:
   - **ADB Management** (`src/adb/`) - Device communication via ADB
   - **Device Layer** (`src/device/`) - Server communication, video decoding, input control
   - **Device Management** (`src/devicemanage/`) - Multi-device support

2. **Main Application** (`QtScrcpy/`) - Qt-based UI including:
   - **UI Forms** (`ui/`) - Main dialog, video display, toolbar
   - **OpenGL Rendering** (`render/`) - Hardware-accelerated video display
   - **Audio Output** (`audio/`) - Device audio streaming
   - **Input Mapping** (`keymap/`) - Custom keyboard/mouse mappings for games

### Key Design Patterns

- **Asynchronous Communication**: Qt signal-slot mechanism throughout
- **Singleton Pattern**: Config management
- **Strategy Pattern**: Input converters (normal vs game mode)
- **Factory Pattern**: Device creation based on connection type

### Threading Model

- **Main Thread**: UI event loop and rendering
- **ADB Thread**: Device communication
- **Decoder Thread**: Video stream decoding
- **Controller Thread**: Input event handling

## Code Style

- Format with clang-format: `./clang-format-all.sh`
- 160 character line limit
- 4-space indentation
- Allman brace style for classes/functions

## Development Workflow

1. Make changes to `dev` branch (not master)
2. Format code with clang-format
3. Test on target platforms
4. Create small, focused PRs

## Testing

Currently relies on manual testing and CI builds. Key areas to test:

- USB and WiFi connections
- Multi-device scenarios
- Input mapping functionality
- Video performance

## Key Files to Understand

- `QtScrcpy/ui/dialog.cpp` - Main application logic
- `QtScrcpyCore/src/device/device.cpp` - Device abstraction
- `QtScrcpyCore/src/adb/adbprocess.cpp` - ADB command execution
- `QtScrcpyCore/src/device/server/server.cpp` - Server communication protocol

## System prompt

You can break complex problems into steps (Sequential Thinking) and remember facts over time (Memory). For each question:

- Use step-by-step reasoning.
- Store new facts or conclusions in memory.
- Recall relevant information if asked about previous steps.

Format:
Step X/Y: [reasoning]
Memory: [store or recall fact, if relevant]

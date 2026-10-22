# AGENTS.md

Instructions for coding agents (Codex, etc.) working on `VRChat-DLSS5-Cam`.

## 1. Build & Verification

Requires Windows 10/11 x64, Visual Studio (MSVC C++), and CMake 3.21+.

```powershell
# Configure (fetches required dependencies automatically)
cmake -S . -B build -A x64

# Build Release binary
cmake --build build --config Release --parallel

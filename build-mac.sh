#!/bin/bash
# Build CREEPER on your Mac and install it (VST3 + AU + Standalone).
# Usage: open Terminal in this folder and run:   bash build-mac.sh
set -e
cd "$(dirname "$0")"

echo "== CREEPER: build for macOS =="

# 1. Apple compiler (Xcode Command Line Tools)
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Installing the Xcode Command Line Tools (a window will open)."
  echo "Click Install, wait for it to finish, then run this script again."
  xcode-select --install
  exit 1
fi

# 2. CMake (downloaded locally into this folder if it is not installed)
if command -v cmake >/dev/null 2>&1; then
  CMAKE=cmake
else
  CMAKE_DIR="$PWD/.tools/cmake"
  if [ ! -x "$CMAKE_DIR/CMake.app/Contents/bin/cmake" ]; then
    echo "Downloading CMake..."
    mkdir -p "$CMAKE_DIR"
    curl -L -o "$CMAKE_DIR/cmake.tar.gz" \
      https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-macos-universal.tar.gz
    tar -xzf "$CMAKE_DIR/cmake.tar.gz" -C "$CMAKE_DIR" --strip-components=1
  fi
  CMAKE="$CMAKE_DIR/CMake.app/Contents/bin/cmake"
fi

# 3. Build (universal: Apple Silicon + Intel). JUCE is downloaded automatically the first time.
"$CMAKE" -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
"$CMAKE" --build build --config Release --parallel "$(sysctl -n hw.ncpu)"

# 4. Install into your user plugin folders
OUT=build/Creeper_artefacts/Release
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/CREEPER.vst3" "$AU_DIR/CREEPER.component"
cp -R "$OUT/VST3/CREEPER.vst3" "$VST3_DIR/"
cp -R "$OUT/AU/CREEPER.component" "$AU_DIR/"
for p in "$VST3_DIR/CREEPER.vst3" "$AU_DIR/CREEPER.component" "$OUT/Standalone/CREEPER.app"; do
  xattr -cr "$p" 2>/dev/null || true
  codesign --force --deep --sign - "$p" >/dev/null 2>&1 || true
done

echo
echo "Done. CREEPER is installed:"
echo "  VST3: $VST3_DIR/CREEPER.vst3"
echo "  AU:   $AU_DIR/CREEPER.component"
echo "  App:  $PWD/$OUT/Standalone/CREEPER.app"
echo "In FL Studio: Options > Manage plugins > Find plugins."

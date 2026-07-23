#!/usr/bin/env bash
set -euo pipefail

ProjectDir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
Packages=(
    cmake
    ninja-build
    gcc-c++
    git
    libevdev-devel
    libX11-devel
    systemd-devel
    glfw-devel
    nlohmann-json-devel
    mesa-libGL-devel
)

echo "Installing FastClicker build dependencies..."
sudo dnf install -y "${Packages[@]}"

echo "Enabling access to the Linux virtual-input device..."
sudo modprobe uinput
sudo install -Dm644 \
    "${ProjectDir}/packaging/70-autoclicker-uinput.rules" \
    /etc/udev/rules.d/70-autoclicker-uinput.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --name-match=uinput

echo "Configuring and building FastClicker..."
cmake -S "${ProjectDir}" -B "${ProjectDir}/.build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAUTOCLICKER_USE_SYSTEM_DEPS=ON
cmake --build "${ProjectDir}/.build" --parallel

echo
echo "Setup complete. Start FastClicker with:"
echo "  ${ProjectDir}/run.sh"
echo
echo "If /dev/uinput is still inaccessible, log out and back in once."

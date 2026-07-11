# Trees of Insanity

Clean Electrobun rebuild of Trees of Insanity.

## Direction

- Electrobun desktop shell
- Solid/Tailwind left UI
- native right viewport through `<electrobun-wgpu>` native handle
- system webview renderer first: WebKitGTK on Linux, WebView2 on Windows
- current C++ growth/ovrtx/Vulkan hot path first
- Wayland workstation with Xwayland-backed viewport first
- Windows native + NVIDIA RTX second

Out of scope for now: true Wayland-native viewport, macOS, AMD/Intel GPU support, canvas/WebGPU pixel upload viewport.

## Setup (Arch Linux)

Requires an NVIDIA RTX GPU with a working NVIDIA driver and a graphical session with Xwayland.

Install the system dependencies:

```bash
sudo pacman -S --needed \
  git gcc cmake ninja nlohmann-json cuda \
  vulkan-headers shaderc \
  webkit2gtk-4.1 libayatana-appindicator xorg-xwayland unzip
```

Install [Bun](https://bun.com/docs/installation):

```bash
curl -fsSL https://bun.com/install | bash
```

Restart the shell, then clone and enter the repository:

```bash
git clone https://github.com/Dillpickleschmidt/trees-of-insanity.git
cd trees-of-insanity
```

From the repository root, download the pinned ovrtx SDK and install the project:

```bash
mkdir -p .cache/downloads .cache/ovrtx
curl -fL --retry 3 \
  https://github.com/NVIDIA-Omniverse/ovrtx/releases/download/v0.3.0/ovrtx%400.3.0.312915.cec773e1.manylinux_2_35_x86_64.zip \
  -o .cache/downloads/ovrtx-0.3.0-linux-x86_64.zip
echo "5569e44b18d2d39f23f374c9352dac9c87b8115892209c243c98732085f1d5f9  .cache/downloads/ovrtx-0.3.0-linux-x86_64.zip" | sha256sum -c -
rm -rf .cache/ovrtx/current
mkdir -p .cache/ovrtx/current
unzip -q .cache/downloads/ovrtx-0.3.0-linux-x86_64.zip -d .cache/ovrtx/current
bun install --frozen-lockfile
bun run dev
```

After the initial native build, use HMR with `bun run dev:watch`.

## Native core

```bash
cmake --preset core
cmake --build --preset core
ctest --preset core
```

## Automation

```bash
bun run verify:shell
```

This launches the shell, writes JSONL events and logs to `artifacts/automation/`, and exits non-zero unless the native viewport presents a correctly sized frame.

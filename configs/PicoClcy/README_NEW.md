# PicoClcy — Custom GP2040-CE Build (格斗游戏宏 / Reverse 一键出招)

This is Chang's customized GP2040-CE board config for a Raspberry Pi Pico (RP2040),
tuned for fighting games. 本文档是给**当前这台机器** (`/Users/chang/Explore/GP2040-CE`)
重新 build 固件 + 刷机用的步骤。

> 💡 **zsh paste tip (做一次):** your shell doesn't treat `#` as a comment by default, so
> pasting command blocks that contain `# ...` notes can misfire (it even *executes* backticks
> inside a "comment"). Turn comments on once and the blocks below paste cleanly:
> ```
> echo 'setopt interactive_comments' >> ~/.zshrc
> setopt interactive_comments
> ```

## What's customized (改了什么)

- **Macros (`src/addons/input_macro.cpp`)** — expanded from 6 → **10** macro slots
  (`MACRO_1 … MACRO_10`). New `interruptible` semantics: while a macro runs the dpad is
  ignored, and any 拳/脚 you press are **deferred** and fired together on the macro's
  **last frame** (蓄力 + 最后一帧同时出招). With `interruptible = false` the macro is fully
  exclusive (ignores all input).
- **Reverse (`src/addons/reverse.cpp`)** — a family of one-button move buttons: **2 Drive Reversals**
  (`Drive Reversal` + `Drive Reversal G`, the latter gated on a held ←/→), a **21346 super**
  (`Reverse 23626 LP/LK`), a **hardcoded-motion engine** (236/214 QCR, 623*, 21346*, 22, 28*, 2HP…),
  and **directional moves** (`46 LP`, `46 HK`, `Air Throw`, `JMP`) that mirror off your held direction.
  All assigned via the web **Pin Mapping** dropdown (Add-ons → Input Reverse must be enabled).

---

## 0. One-time setup per machine (一台机器只需做一次)

**On THIS machine everything below is already done ✅** — so you can skip straight to
**section 1 (Build)**. It's listed here only so you can reproduce it on a new Mac.

```bash
brew install cmake
brew install --cask gcc-arm-embedded

cd /Users/chang/Explore/GP2040-CE
echo '/pico-sdk/' >> .git/info/exclude
git clone --branch 2.1.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git pico-sdk
git -C pico-sdk submodule update --init --depth 1

git submodule update --init --recursive

echo 'export PICO_SDK_PATH=/Users/chang/Explore/GP2040-CE/pico-sdk' >> ~/.zshrc
source ~/.zshrc
```

What each step does:
- `cmake` + `gcc-arm-embedded` → the build tools (`gcc-arm-embedded` provides `arm-none-eabi-gcc`).
- The `pico-sdk` clone → **Pico SDK 2.1.1, self-contained inside the repo** at `GP2040-CE/pico-sdk/`.
  The `.git/info/exclude` line hides it from this repo's git (local-only — never tracked, never
  committed, never shows in `git diff`). Its submodules give us tinyusb / mbedtls / lwip / etc.
- `git submodule update --init --recursive` → this repo's own submodules (`lib/tinyusb`, `lib/pico_pio_usb`).
- The `~/.zshrc` line → tells every terminal where the SDK is, so the build stays a plain
  `cmake .. && make` (this is what made it "just work" on your other Mac).

> **Notes**
> - If you ever re-clone GP2040-CE fresh, redo the `.git/info/exclude` line — it lives in
>   `.git/`, so it does **not** travel with the repo.
> - `node`/`npm` (web config UI) and `python3` (proto codegen) are already on this machine.
> - **VS Code alternative:** install the "Raspberry Pi Pico" extension and hit **Compile** —
>   it manages SDK 2.1.1 + toolchain for you, no `PICO_SDK_PATH` needed.

---

## 1. Build (编译) — simple flow, same as your other Mac

```
cd /Users/chang/Explore/GP2040-CE
export PICO_SDK_PATH=/Users/chang/Explore/GP2040-CE/pico-sdk
export GP2040_BOARDCONFIG=PicoClcy
rm -rf build
mkdir build
cd build
cmake ..
make -j12

```

(`make -j12` uses all 12 cores — much faster. Plain `make` works too, just slower.)

➡️ **Output firmware:** `build/GP2040-CE_<version>_PicoClcy.uf2`
(`<version>` is `0.0.0` unless the repo has a git version tag — purely cosmetic, fine to flash.)

> ⚠️ **First build only:** the proto-codegen step creates a Python venv and pip-installs
> `protobuf` + `grpcio-tools` — so the **very first** `make` needs internet. It's cached after
> that (`build/venv/`), so later rebuilds work offline. (`PICO_SDK_PATH` comes from `~/.zshrc`
> via step 0; if you skipped that, run `export PICO_SDK_PATH=/Users/chang/Explore/GP2040-CE/pico-sdk`
> before `cmake ..`.)

Clean rebuild (改完代码想干净重来):

```bash
cd /Users/chang/Explore/GP2040-CE
rm -rf ./build
```

Then repeat the Build steps above.

> Tip: 只改了 `.cpp` 代码时不用删 `build`，直接在 `build/` 里跑 `make -j12` 就行，快很多。
> 改了 `proto/*.proto` 或加了新文件，才需要重新跑 `cmake ..`（或干脆删 build 重来）。

---

## 2. Flash the firmware (刷固件)

The controller is a Raspberry Pi Pico, so flashing = drag a `.uf2` onto a USB drive.

### Step A — Enter BOOTSEL mode (进入刷机模式)

- **Unplug** the controller's USB cable.
- **Hold down** the **BOOTSEL** button (the small white button on the Pico board).
- While still holding it, **plug the USB cable** back into the Mac.
- Release BOOTSEL.
- A USB drive named **`RPI-RP2`** appears in Finder. ✅ 你现在在刷机模式了。

> Already running GP2040-CE? You can also reboot into BOOTSEL from the **web config**
> (open `http://192.168.7.1` while in web-config mode → **Reboot → Bootloader Mode**),
> instead of physically holding the button.

### Step B — NUKE the flash (清空闪存，强烈建议在升级 / 出怪现象时做)

A "nuke" wipes the RP2040's entire flash so you start from a clean slate. **建议在以下情况 nuke：**
跨大版本升级、配置看起来乱了/启动异常、或想彻底重置。

1. Download **`flash_nuke.uf2`** from the official install page:
   <https://gp2040-ce.info/installation/>
   (upstream origin is Raspberry Pi's `flash_nuke.uf2`:
   <https://datasheets.raspberrypi.com/soft/flash_nuke.uf2>)
2. With `RPI-RP2` mounted (Step A), **drag `flash_nuke.uf2` onto the `RPI-RP2` drive**.
3. The board erases its flash, reboots, and **re-mounts as `RPI-RP2` again** automatically.

> ⚠️ **Nuke 会清掉你在网页 UI 里存的所有设置**（按键映射、宏、reverse 配置等），
> 之后固件会回到 `BoardConfig.h` 里的默认值，需要重新在网页里配。
> 如果只是想测试新代码、不想丢配置，**可以跳过 Step B**，直接做 Step C。

### Step C — Flash GP2040-CE (刷入你 build 出来的固件)

1. Make sure `RPI-RP2` is mounted (after a nuke it already is).
2. **Drag `build/GP2040-CE_<version>_PicoClcy.uf2` onto the `RPI-RP2` drive.**
   (or from a terminal: `cp build/GP2040-CE_*_PicoClcy.uf2 /Volumes/RPI-RP2/`)
3. The board flashes, **reboots automatically**, the drive disappears, and your custom
   firmware is now running. 🎮

---

## 3. Quick reference (改代码 → 测试 的日常循环)

After editing `src/addons/input_macro.cpp` or `src/addons/reverse.cpp`:

```bash
cd /Users/chang/Explore/GP2040-CE/build
make -j12
```

Then put the controller into BOOTSEL (Step A) and copy the firmware over:

```bash
cp /Users/chang/Explore/GP2040-CE/build/GP2040-CE_*_PicoClcy.uf2 /Volumes/RPI-RP2/
```

It reboots into the new firmware — go test in game. Most code-only iterations **don't need a
nuke** — just Step A + this copy.

---

## Pin mapping (this board)

| RP2040 GPIO | Action |
|-------------|--------|
| GPIO_02 | BUTTON_PRESS_UP |
| GPIO_03 | BUTTON_PRESS_DOWN |
| GPIO_04 | BUTTON_PRESS_RIGHT |
| GPIO_05 | BUTTON_PRESS_LEFT |
| GPIO_06 | BUTTON_PRESS_B4 |
| GPIO_07 | BUTTON_PRESS_L1 |
| GPIO_10 | BUTTON_PRESS_B2 |
| GPIO_11 | BUTTON_PRESS_R1 |
| GPIO_12 | BUTTON_PRESS_R2 |
| GPIO_15 | BUTTON_PRESS_L2 |
| GPIO_16 | BUTTON_PRESS_S1 |
| GPIO_17 | BUTTON_PRESS_S2 |
| GPIO_19 | BUTTON_PRESS_B3 |
| GPIO_20 | BUTTON_PRESS_L3 |
| GPIO_21 | BUTTON_PRESS_R3 |
| GPIO_28 | BUTTON_PRESS_B1 |

Macro / Reverse trigger pins are assigned in the **web config UI** (Add-ons → Input Macro /
Input Reverse), not in `BoardConfig.h`.

---

## Links

- GP2040-CE install / flashing guide: <https://gp2040-ce.info/installation/>
- Build environment guide: <https://gp2040-ce.info/development/build-environment>

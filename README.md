<div align="center">
  <img src="assets/images/alir-io-banner.png" alt="alir.io banner" width="1080"/>
</div>

----

<div align="center">

[![License](https://img.shields.io/github/license/aprixlabs/alir-io)](https://github.com/aprixlabs/alir-io/blob/main/LICENSE)
[![Latest Release](https://img.shields.io/github/v/release/aprixlabs/alir-io)](https://github.com/aprixlabs/alir-io/releases)
[![Downloads](https://img.shields.io/github/downloads/aprixlabs/alir-io/total)](https://github.com/aprixlabs/alir-io/releases)

</div>

**alir.io** is an OBS Studio plugin that routes audio directly between your ASIO hardware and OBS.
It bypasses the need for virtual audio cables or third-party workarounds, allowing you to capture multi-channel ASIO inputs and route OBS audio outputs with native, low-latency performance.

----

### Features
* **ASIO Input Source:** Capture direct audio from your ASIO interface into OBS.
* **ASIO Output Filter:** Route specific audio sources from OBS straight to your ASIO hardware.
* **Multi-channel Support:** Works with Mono, Stereo, and complex surround setups (up to 7.1).
* **Low Latency:** Relies on native ASIO drivers to keep audio delays to an absolute minimum.

----

### Installation
**Windows**
* **Installer (Recommended):** Just grab the latest `.exe` installer from the **[Releases](../../releases/latest)** page. Run it, restart OBS Studio, and you're done.
* **Manual (.zip):** Download the `.zip` version, extract it, and drop the `data` and `obs-plugins` folders directly into your OBS Studio installation directory (usually `C:\Program Files\obs-studio`). Restart OBS.

Once installed, you'll find the new ASIO options waiting for you in your Sources and Audio Filters menus.

----

### Development

Building the project is fully automated using **CMake Presets**. Dependencies (Qt6, OBS Studio SDK, ASIO SDK) are fetched automatically.

**Prerequisites:**
- Visual Studio 2022 (with *Desktop development with C++* workload)
- CMake 3.28+
- [Inno Setup 6](https://jrsoftware.org/isinfo.php) (Only required for building the `.exe` installer)

**Build Instructions (Windows):**
```powershell
# 1. Configure the project and download all dependencies
cmake --preset windows-x64

# 2. Compile the plugin
cmake --build --preset windows-x64

# 3. Create local installation (output goes to /release folder)
cmake --install build_x64 --prefix release --config RelWithDebInfo

# 4. Generate the installer (.exe)
cmake --build --preset windows-x64 --target package
```
----

### Acknowledgments
A special thanks to the [obs-asio](https://github.com/Andersama/obs-asio) project by Andersama. Their foundational work in bringing ASIO support to OBS Studio served as a major inspiration for the development of alir.io.

----

### License & Trademarks
This project is licensed under the [GPL-3.0 License](LICENSE).

*ASIO is a registered trademark of Steinberg Media Technologies GmbH.*

### Support alir.io
Keep my throat from getting scratchy:D

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/aprixlabs)

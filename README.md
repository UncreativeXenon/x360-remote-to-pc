# A lightweight Xbox 360 plugin that forwards controller input over the network to a PC.

## 📜 Features
- Send controller input to a remote PC over TCP.
- Enable/Disable console-side input when streaming.
- Support for multiple controllers (2nd-4th controller may be buggy/slow).

## ⚙️ Usage

1. Download both the Windows App & the Xbox Plugin from the latest [Release](https://github.com/UncreativeXenon/x360-remote-to-pc/releases).
2. For the Windows app, edit `port.txt` to the port where the server will listen to (default is 4000, you can leave it at that if it works).
3. For the Xbox Plugin, edit the IP `360ControllerToPC.ini` to your PC's Local IPv4 and the port to whatever port you set on Windows (default is 4000).
4. Edit other configurable options such as enabling/disabling console-side input & Maximum amount of controllers allowed.
5. make sure `360ControllerToPC.ini` is placed right next to `360ControllerToPC.xex` and add the `.xex` to Plugin list in DashLaunch configuration.

## 🧱 Credits

Inspired by localcc's [360AsController](https://github.com/localcc/360AsController) repo, but their implementation was a bit buggy hence why I made my own.

## 📄 License

MIT License.  
See [LICENSE](LICENSE) for full details.

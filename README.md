# wayfire-word-switcher

A lightweight native [Wayfire](https://wayfire.org) plugin for converting the last typed word between keyboard layouts on the fly (similar to Punto Switcher / XNeur, using manual hotkey triggering).

Typed `Ghbdtn` instead of `Привет` or `руддщ` instead of `hello`? Press a single hotkey, and the plugin will erase the mistyped word, switch the keyboard layout, and re-type the characters with their exact original case and key repeat state.

## Features

* **Per-character layout & case memory:** Accurately restores uppercase/lowercase letters (`Ghbdtn` → `Привет`, `GHBDTN` → `ПРИВЕТ`) and even handles words where the layout was accidentally switched mid-word.
* **Key repeat synchronization:** Uses Wayfire's `input/kb_repeat_delay` and `input/kb_repeat_rate` timers so holding `Backspace` or letter keys stays completely in sync with the internal word buffer.
* **Broad application compatibility:** Works reliably across native Wayland terminals, GTK applications, and browsers (including Firefox and Chromium-based apps) by properly managing `wl_keyboard.modifiers` masks and releasing held binding modifiers before replaying keys.
* **Smart buffer invalidation:** Automatically clears the tracked word buffer on word delimiters (`Space`, `Enter`, `Tab`, `Esc`), cursor navigation, mouse clicks, window focus changes, and `Ctrl` editing shortcuts (`Ctrl+Backspace`, `Ctrl+V`, `Ctrl+X`, `Ctrl+Z`).
* **Optional layout preservation:** Includes a `dont_change_layout` option to convert the text while keeping your active keyboard layout unchanged.

## Dependencies

* **Wayfire** (tested with Wayfire 0.11+)
* **wlroots**
* **xkbcommon**
* **Meson** & **Ninja** (or your preferred C++20 compiler)

## Building & Installation

```bash
git clone https://github.com<your-username>/wayfire-word-switcher.git
cd wayfire-word-switcher

meson setup build --prefix=/usr
ninja -C build
sudo ninja -C build install
```

*(Note: Adjust `--prefix` to match your Wayfire installation path, e.g., `/usr/local` if built from source.)*

## Configuration

1. Enable `word-switcher` in the `plugins` list in `~/.config/wayfire.ini`:

```ini
[core]
plugins = \
    ... \
    word-switcher
```

2. Configure the hotkey and options in `~/.config/wayfire.ini`:

```ini
[word-switcher]
# Hotkey to convert the last typed word (default: <super> KEY_SPACE)
convert_key = KEY_PAUSE

# Keep the current keyboard layout active after converting the word (default: false)
dont_change_layout = false
```

### Example Bindings

* Single key: `convert_key = KEY_PAUSE` or `convert_key = KEY_SYSRQ`
* Key combination: `convert_key = <super> KEY_SPACE` or `convert_key = <alt> KEY_Q`

## License

MIT License

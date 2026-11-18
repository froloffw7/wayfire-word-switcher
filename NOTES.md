# Architectural Notes & Wayland/wlroots Specifics / Архитектурные заметки и особенности Wayland / wlroots

*[English](#english) | [Русский](#русский)*

---

## English

This document summarizes the key architectural decisions and low-level Wayland / `wlroots` / `libxkbcommon` subtleties behind `wayfire-word-switcher`.

### 1. Releasing Held Binding Modifiers Before Replaying Keys

* **The Problem:** When the user triggers conversion via a key combination (e.g., `<super> KEY_SPACE` or `<control> KEY_...`), the physical modifier key is still held down when the callback executes. Applications (especially Firefox/Gecko and GTK apps) interpret incoming `KEY_BACKSPACE` events with an active `Ctrl` or `Super` modifier as shortcuts (or ignore them completely).
* **The Solution:** The plugin maintains a bitmask (`state_mods`) tracking the exact physical left/right modifier keys (`KEY_LEFTCTRL`, `KEY_RIGHTCTRL`, `KEY_LEFTMETA`, etc.) in bits `16..23` (above standard `wlr_keyboard_modifier` bits `0..7`). Before sending `Backspace` or replaying characters, the plugin sends explicit `WL_KEYBOARD_KEY_STATE_RELEASED` events for any held binding modifiers and resets `depressed` modifiers via `wlr_keyboard_notify_modifiers()`.

### 2. Per-Character Layout & Shift State Tracking

* **The Problem:** Storing only raw keycodes fails when a word contains mixed case (`Ghbdtn`) or when the user accidentally switches the layout mid-word.
* **The Solution:** Each entry in `word_buffer` (`KeyStroke`) stores three fields: `{keycode, layout, shift}`.
  * During replay, if the current `layout` matches `stroke.layout`, the plugin advances to `(layout + 1) % num_layouts` and unconditionally updates `stroke.layout = layout`.
  * Unconditional updating ensures that pressing the conversion hotkey a second time reliably reverts the word back to its original layout, even if the user manually toggled `CapsLock` beforehand.
  * When `dont_change_layout` is enabled, the plugin restores `orig_layout` after the last character is replayed.

### 3. Client-Side Key Repeat Emulation (`wf::wl_timer`)

* **The Problem:** In Wayland, key repeat is handled client-side rather than generating repeated `wl_keyboard.key` events on the compositor. Holding `Backspace` or a letter key only fires a single `PRESSED` event in `on_key`, causing `word_buffer` to desynchronize from what is actually on screen.
* **The Solution:**
  * The plugin reads `input/kb_repeat_delay` and `input/kb_repeat_rate` from Wayfire's configuration (and checks `xkb_keymap_key_repeats(kbd->keymap, key + 8)`, where `+ 8` converts Linux `evdev` keycodes to XKB keycodes).
  * Two separate timers are used: a one-shot `wf::wl_timer<false>` for the initial delay and a repeatable `wf::wl_timer<true>` (whose callback returns `bool`) for subsequent ticks.
  * Any new key press, release of the repeating key, mouse click, or window focus change immediately disconnects both timers (`stop_repeat()`).

### 4. Dynamic Keymap Caching

* **The Problem:** Keyboard devices can be hot-plugged, or `xkb_layout` / `xkb_options` can be reloaded at runtime in `wayfire.ini`.
* **The Solution:** `shift_mask` and `num_layouts` are cached against the active `xkb_keymap*` pointer (`last_keymap`). Whenever `kbd->keymap != last_keymap`, the plugin automatically recalculates `XKB_MOD_NAME_SHIFT` and the layout count.

### 5. Replay Guard (`is_replaying`)

* Synthetic key events injected via `wlr_seat_keyboard_notify_key()` pass back through Wayfire's input signals. An `is_replaying` flag prevents `on_key` from re-capturing synthetic `Backspace` and character keystrokes into `word_buffer`.

---

## Русский

В этом документе собраны ключевые архитектурные решения и неочевидные особенности работы с Wayland, `wlroots` и `libxkbcommon`, учтенные в `wayfire-word-switcher`.

### 1. Принудительное отпускание модификаторов горячей клавиши

* **Проблема:** Когда конвертация запускается сочетанием клавиш (например, `<super> KEY_SPACE` или `<control> ...`), физическая клавиша-модификатор в момент срабатывания еще удерживается пользователем. Клиентские приложения (особенно Firefox/Gecko и GTK-программы) воспринимают прилетающий `KEY_BACKSPACE` при зажатом `Ctrl` или `Super` как шорткат или игнорируют его.
* **Решение:** Плагин отслеживает физическое состояние левых и правых клавиш-модификаторов (`KEY_LEFTCTRL`, `KEY_RIGHTCTRL`, `KEY_LEFTMETA` и др.) в старших битах `16..23` маски `state_mods` (не пересекаясь с младшими битами `0..7` стандарта `wlr_keyboard_modifier`). Перед отправкой `Backspace` и повторным вводом слова плагин отправляет события отжатия (`WL_KEYBOARD_KEY_STATE_RELEASED`) для зажатых модификаторов и сбрасывает `depressed` через `wlr_keyboard_notify_modifiers()`.

### 2. Посимвольное хранение раскладки и регистра

* **Проблема:** Запоминания одних только скан-кодов недостаточно для слов со смешанным регистром (`Ghbdtn`) или слов, в которых раскладка была случайно переключена посередине.
* **Решение:** Каждый элемент буфера `word_buffer` (`KeyStroke`) хранит три поля: `{keycode, layout, shift}`.
  * При воспроизведении, если текущая раскладка `layout` совпадает с `stroke.layout`, плагин переключает её на `(layout + 1) % num_layouts` и **безусловно** обновляет `stroke.layout = layout`.
  * Безусловное обновление гарантирует, что повторное нажатие горячей клавиши корректно вернет слово в исходную раскладку, даже если перед первым нажатием пользователь вручную переключил раскладку через `CapsLock`.
  * При включенной опции `dont_change_layout` после ввода последнего символа раскладка возвращается в исходную (`orig_layout`).

### 3. Эмуляция автоповтора клавиш (`wf::wl_timer`)

* **Проблема:** В протоколе Wayland автоповтор нажатой клавиши реализуется на стороне клиента, а не сервера. При удержании `Backspace` или буквы обработчик `on_key` получает только одно событие `PRESSED`, из-за чего внутренний `word_buffer` рассинхронизируется с текстом на экране.
* **Решение:**
  * Плагин использует настройки Wayfire `input/kb_repeat_delay` и `input/kb_repeat_rate`, предварительно проверяя поддержку автоповтора для клавиши через `xkb_keymap_key_repeats(kbd->keymap, key + 8)` (сдвиг `+ 8` переводит скан-код ядра Linux `evdev` в скан-код XKB).
  * Используются два отдельных таймера: одноразовый `wf::wl_timer<false>` для начальной задержки и циклический `wf::wl_timer<true>` (колбэк которого возвращает `bool`) для последующих повторов.
  * Любое новое нажатие клавиши, отпускание повторяемой клавиши, клик мыши или смена фокуса окна мгновенно останавливают оба таймера (`stop_repeat()`).

### 4. Динамическое кэширование `xkb_keymap`

* **Проблема:** Пользователь может подключить внешнюю клавиатуру на лету или изменить `xkb_layout` / `xkb_options` в `wayfire.ini` во время сессии.
* **Решение:** Индекс `shift_mask` и количество раскладок `num_layouts` кэшируются с привязкой к указателю `xkb_keymap*` (`last_keymap`). При изменении `kbd->keymap != last_keymap` параметры автоматически пересчитываются.

### 5. Защита от повторного захвата (`is_replaying`)

* Синтетические события нажатий, отправляемые через `wlr_seat_keyboard_notify_key()`, снова проходят через сигналы ввода Wayfire. Флаг `is_replaying` блокирует их повторную обработку в `on_key`.

#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/unstable/wlr-view-keyboard-interaction.hpp>
#include <wayfire/util.hpp>
#include <linux/input-event-codes.h>
#include <vector>

// #define __DEBUG_SWITCHER__

namespace wf::switcher
{

struct KeyStroke {
    uint32_t           keycode;
    xkb_layout_index_t layout;
    bool               shift;
};

enum {
	MODIFIER_SHIFT_L    = 1U << 16, /* Left shift */
	MODIFIER_CTRL_L     = 1U << 17, /* Left control */
	MODIFIER_ALT_L      = 1U << 18,
	MODIFIER_META_L     = 1U << 19,
	MODIFIER_SHIFT_R    = 1U << 20, /* Right shift */
	MODIFIER_CTRL_R     = 1U << 21, /* Right control */
	MODIFIER_ALT_R      = 1U << 22,
	MODIFIER_META_R     = 1U << 23,
};

class word_switcher : public wf::plugin_interface_t {

    wf::option_wrapper_t<wf::keybinding_t> convert_binding   {"word-switcher/convert_key"};
    wf::option_wrapper_t<bool>             dont_change_layout{"word-switcher/dont_change_layout"};
    wf::option_wrapper_t<int>              repeat_delay      {"input/kb_repeat_delay"};
    wf::option_wrapper_t<int>              repeat_rate       {"input/kb_repeat_rate"};

    xkb_keymap *           last_keymap   = nullptr;
    xkb_layout_index_t     num_layouts   = 0;
    uint32_t               shift_mask    = 0;
    uint32_t               binding_mods  = 0;

    std::vector<KeyStroke> word_buffer;
    bool                   is_replaying  = false;
    uint32_t               state_mods    = 0;
    uint32_t               repeating_key = 0;

    wf::wl_timer<false>    delay_timer;
    wf::wl_timer<true>     repeat_timer;

    wf::key_callback on_convert = [this] (const wf::keybinding_t&) {
        convert_last_word();
        return true; // Поглощаем нажатие, чтобы окно его не получило
    };

    bool key_to_modifier(uint32_t key, bool pressed) {
        uint32_t mask;
        switch (key) {
        case KEY_CAPSLOCK:   mask = WLR_MODIFIER_CAPS; break;
        case KEY_NUMLOCK:    mask = WLR_MODIFIER_MOD2; break;
        case KEY_SCROLLLOCK: mask = WLR_MODIFIER_MOD3; break;
        // case KEY_ISO:        mask = WLR_MODIFIER_MOD5;    break;

        case KEY_LEFTSHIFT:  mask = MODIFIER_SHIFT_L;  break;
        case KEY_LEFTCTRL:   mask = MODIFIER_CTRL_L;   break;
        case KEY_LEFTALT:    mask = MODIFIER_ALT_L;    break;
        case KEY_LEFTMETA:   mask = MODIFIER_META_L;   break;
        case KEY_RIGHTSHIFT: mask = MODIFIER_SHIFT_R;  break;
        case KEY_RIGHTCTRL:  mask = MODIFIER_CTRL_R;   break;
        case KEY_RIGHTALT:   mask = MODIFIER_ALT_R;    break;
        case KEY_RIGHTMETA:  mask = MODIFIER_META_R;   break;
        default:             mask = 0;
        }
        if (mask) {
            if (pressed)
                state_mods |= mask;
            else
                state_mods &= ~mask;
        }
        return mask != 0;
    }

    uint32_t get_key(wlr_keyboard_modifier mod) {
        uint32_t key = 0;
        switch (mod) {
        case WLR_MODIFIER_SHIFT:
            if      (state_mods & MODIFIER_SHIFT_L) key = KEY_LEFTSHIFT;
            else if (state_mods & MODIFIER_SHIFT_R) key = KEY_RIGHTSHIFT;
            break;
        case WLR_MODIFIER_CAPS:
            key = KEY_CAPSLOCK;
            break;
        case WLR_MODIFIER_CTRL:
            if      (state_mods & MODIFIER_CTRL_L) key = KEY_LEFTCTRL;
            // else if (state_mods & MODIFIER_CTRL_R) key = KEY_RIGHTCTRL;
            break;
        case WLR_MODIFIER_ALT:
            if      (state_mods & MODIFIER_ALT_L) key = KEY_LEFTALT;
            else if (state_mods & MODIFIER_ALT_R) key = KEY_RIGHTALT;
            break;
        case WLR_MODIFIER_MOD2:
            key = KEY_NUMLOCK;
            break;
        case WLR_MODIFIER_MOD3:
            key = KEY_SCROLLLOCK;
            break;
        case WLR_MODIFIER_LOGO:
            if      (state_mods & MODIFIER_META_L) key = KEY_LEFTMETA;
            else if (state_mods & MODIFIER_META_R) key = KEY_RIGHTMETA;
            break;
        case WLR_MODIFIER_MOD5:
            if      (state_mods & MODIFIER_ALT_R) key = KEY_RIGHTALT;
            // else if (state_mods & MODIFIER_ALT_L) key = KEY_LEFTALT;
            break;
        default: key = 0;
        }
        return key;
    }

    // Вызывается при нажатии клавиши, способной к автоповтору:
    void start_repeat(wlr_keyboard* kbd, uint32_t key, std::function<void()> action) {

        action();

        stop_repeat();
        // + 8 для перевода скан-кода ядра Linux (evdev) в скан-код стандарта XKB
        if (!kbd || !kbd->keymap || !xkb_keymap_key_repeats(kbd->keymap, key + 8)) {
            return; // Клавиша не поддерживает автоповтор (например, Shift или CapsLock)
        }

        int32_t delay = kbd->repeat_info.delay; // Обычно ~400-600 мс
        int32_t rate  = kbd->repeat_info.rate;  // Обычно ~25-35 повторов в сек
        if (delay <= 0 || rate <= 0) return;

        uint32_t interval_ms = 1000 / rate;
        repeating_key = key;

        // Первый тик после delay
        delay_timer.set_timeout(delay, [this, action, interval_ms] () {
            action();
            // Последующие тики с частотой rate
            repeat_timer.set_timeout(interval_ms, [action] () -> bool {
                action();
                return true; // Возврат true заставляет wf::wl_timer работать циклически
            });
            return false; // Останавливаем одноразовый таймер задержки
        });
    }

    void stop_repeat() {
        delay_timer.disconnect();
        repeat_timer.disconnect();
        repeating_key = 0;
    }

    xkb_layout_index_t get_current_layout(wlr_keyboard *kbd) {
        if (kbd) return xkb_state_serialize_layout(
            kbd->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
        else     return 0;
    }

    wf::signal::connection_t<wf::input_event_signal<wlr_keyboard_key_event>> on_key =
        [this] (wf::input_event_signal<wlr_keyboard_key_event> *ev) {
        if (is_replaying) return;

        auto key = ev->event->keycode;
        bool pressed = (ev->event->state == WL_KEYBOARD_KEY_STATE_PRESSED);

        if (pressed) {
            // Любое новое нажатие клавиши отменяет автоповтор предыдущей
            stop_repeat();
        } else if (key == repeating_key) {
            // Отпускание останавливает таймер, только если отпустили именно повторяемую клавишу
            stop_repeat();
        }

        if (key_to_modifier(key, pressed)) {
#ifdef __DEBUG_SWITCHER__
            LOGW("key=", key, " state=", mod2string(state_mods));
#endif
            return;
        }

        if (!pressed) {
            return;
        }

        // Tab: очищаем буфер при любом сочетании.
        if (key == KEY_TAB) {
            word_buffer.clear();
            return;
        }

        auto mods = wf::get_core().seat->get_keyboard_modifiers();
#ifdef __DEBUG_SWITCHER__
        LOGW("key=", key, " state=", mod2string(state_mods), " MODS=", mod2string(mods));
#endif
        // При Ctrl+Backspace / Ctrl+V / Ctrl+Z буфер слова теряет актуальность
        if ((mods & WLR_MODIFIER_CTRL) && 
            (key == KEY_BACKSPACE || key == KEY_V || key == KEY_X || key == KEY_Z)) {
            word_buffer.clear();
            return;
        }

        // Если зажат Ctrl, Alt или Super — это шорткат, а не ввод букв
        constexpr uint32_t CMD_MODS = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO;
        if (mods & CMD_MODS) {
            return;
        }
        
        // Сброс буфера при навигации и разделителях
        if (key == KEY_SPACE || key == KEY_ENTER || key == KEY_ESC ||
            key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN ||
            key == KEY_PAGEDOWN || key == KEY_PAGEUP) {
            word_buffer.clear();
            return;
        }

        wlr_keyboard * kbd = nullptr;
        auto *seat = wf::get_core().get_current_seat();
        if (seat) {
            kbd = wlr_seat_get_keyboard(seat);
        }

        if (key == KEY_BACKSPACE) {
            auto action = [this] () {
                if (!word_buffer.empty()) {
                    word_buffer.pop_back();
                }
            };
            start_repeat(kbd, key, std::move(action));
            return;
        }
        // Сохраняем печатные клавиши (основной блок клавиатуры)
        if ((key >= KEY_1 && key <= KEY_EQUAL) ||
            (key >= KEY_Q && key <= KEY_RIGHTBRACE) ||
            (key >= KEY_A && key <= KEY_GRAVE) ||
            (key >= KEY_BACKSLASH && key <= KEY_SLASH)) {

            auto layout = get_current_layout(kbd);
            bool shift = (state_mods & (MODIFIER_SHIFT_L | MODIFIER_SHIFT_R)) != 0;
            KeyStroke stroke{key, layout, shift};

            auto action = [this, stroke] () {
                word_buffer.push_back(stroke);
            };
            start_repeat(kbd, key, std::move(action));
        }
    };

    // Сброс буфера при клике мыши (пользователь мог переставить курсор)
    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_button =
        [this] (wf::input_event_signal<wlr_pointer_button_event> *ev) {
        if (ev->event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
            word_buffer.clear();
        }
    };

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_focus_changed =
        [this] (wf::keyboard_focus_changed_signal*) {
        if (!is_replaying) {
            word_buffer.clear();
        }
    };

    void send_key(wlr_seat *seat, uint32_t keycode, uint32_t state) {
        wlr_seat_keyboard_notify_key(seat, wf::get_current_time(), keycode, state);
    }

    void convert_last_word() {
        if (word_buffer.empty()) return;

        auto *seat = wf::get_core().get_current_seat();
        if (!seat) return;
        auto *kbd = wlr_seat_get_keyboard(seat);
        if (!kbd || !kbd->keymap) return;

        // Лениво вычисляем параметры
        if (kbd->keymap != last_keymap) {
            last_keymap = kbd->keymap;
            auto shift_idx = xkb_keymap_mod_get_index(kbd->keymap, XKB_MOD_NAME_SHIFT);
            shift_mask = (shift_idx != XKB_MOD_INVALID) ? (1u << shift_idx) : 0u;
            num_layouts = xkb_keymap_num_layouts(kbd->keymap);
#ifdef __DEBUG_SWITCHER__
            LOGW("Layouts number=", num_layouts, " shift_idx=", shift_idx);

            LOGW("kbd->repeat_info.delay=", kbd->repeat_info.delay, "(", (int)repeat_delay,
                ") kbd->repeat_info.rate=", kbd->repeat_info.rate, "(", (int)repeat_rate, ")");
#endif
        }

        is_replaying = true;

        auto depressed = kbd->modifiers.depressed;
#ifdef __DEBUG_SWITCHER__
        LOGW("depressed=", mod2string(depressed));
#endif
        uint32_t active_mods;
        uint32_t key;

        if (binding_mods) {
            // 1. Отпускаем физически зажатые командные модификаторы (Super/Alt/Ctrl),
            // чтобы Backspace и буквы не сработали как шорткаты
            active_mods = wf::get_core().seat->get_keyboard_modifiers();
#ifdef __DEBUG_SWITCHER__
            LOGW("state_mod=", mod2string(state_mods), " active_mods=", mod2string(active_mods));
#endif            
            if (active_mods & WLR_MODIFIER_LOGO) {
                if ((key = get_key(WLR_MODIFIER_LOGO))) {
                    send_key(seat, key, WL_KEYBOARD_KEY_STATE_RELEASED);
                }
            }
            if (active_mods & WLR_MODIFIER_ALT) {
                if ((key = get_key(WLR_MODIFIER_ALT))) {
                    send_key(seat, key, WL_KEYBOARD_KEY_STATE_RELEASED);
                }
            }
            if (active_mods & WLR_MODIFIER_CTRL) {
                if ((key = get_key(WLR_MODIFIER_CTRL))) {
                    send_key(seat, key, WL_KEYBOARD_KEY_STATE_RELEASED);
                }
            }
            // Сбрасываем depressed-модификаторы перед стиранием слова
            wlr_keyboard_notify_modifiers(kbd, 0, kbd->modifiers.latched,
                kbd->modifiers.locked, kbd->modifiers.group);
        }

        // 2. Стираем слово
        for (size_t i = 0; i < word_buffer.size(); ++i) {
            send_key(seat, KEY_BACKSPACE, WL_KEYBOARD_KEY_STATE_PRESSED);
            send_key(seat, KEY_BACKSPACE, WL_KEYBOARD_KEY_STATE_RELEASED);
        }

        // Вспомогательная λ вычисления следующей раскладки (без отправки в wlroots)
        auto next_layout = [this] (xkb_layout_index_t current) {
            return (num_layouts > 1) ? ((current + 1) % num_layouts) : current;
        };

        auto set_modifiers = [this, kbd] (bool active, xkb_layout_index_t layout) {
            wlr_keyboard_notify_modifiers(kbd,
                active ? shift_mask : 0u,
                kbd->modifiers.latched,
                kbd->modifiers.locked,
                layout);
        };

        auto current_layout = get_current_layout(kbd);
#ifdef __DEBUG_SWITCHER__
        LOGW("Current layout=", current_layout);
#endif
        auto layout = current_layout;
        // 3. Повторно вводим символы с учетом регистра и раскладки
        bool shifted = false;
        for (auto& stroke : word_buffer) {
            bool modifiers_changed = false;
            // Переключаем раскладку
            if (layout == stroke.layout) {
                layout = next_layout(layout);
                modifiers_changed = true;
            }
            stroke.layout = layout;
            if (shifted != stroke.shift) {
                shifted = stroke.shift;
                send_key(seat, KEY_LEFTSHIFT,
                    shifted ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
                modifiers_changed = true;
            }
            if (modifiers_changed) {
                set_modifiers(shifted, layout);
            }
            send_key(seat, stroke.keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
            send_key(seat, stroke.keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
        }

        if (shifted) {
            send_key(seat, KEY_LEFTSHIFT, WL_KEYBOARD_KEY_STATE_RELEASED);
        }

        if (binding_mods) {
            // Возвращаем исходное состояние модификаторов (на случай, если Super еще зажат)
            active_mods ^= binding_mods;
            if (active_mods & WLR_MODIFIER_CTRL) {
                if ((key = get_key(WLR_MODIFIER_CTRL))) {
                    send_key(seat, key, WL_KEYBOARD_KEY_STATE_PRESSED);
                }
            }
            if (active_mods & WLR_MODIFIER_ALT) {
                if ((key = get_key(WLR_MODIFIER_ALT))) {
                    send_key(seat, key, WL_KEYBOARD_KEY_STATE_PRESSED);
                }
            }
            if (active_mods & WLR_MODIFIER_LOGO) {
                if ((key = get_key(WLR_MODIFIER_LOGO))) {
                    send_key(seat, key, WL_KEYBOARD_KEY_STATE_PRESSED);
                }
            }
        }
        if (dont_change_layout) layout = current_layout;
#ifdef __DEBUG_SWITCHER__
        LOGW("Restored layout=", layout);
#endif
        wlr_keyboard_notify_modifiers(kbd, depressed, kbd->modifiers.latched,
            kbd->modifiers.locked, layout);

        is_replaying = false;
    }

public:
    void init() override {
        wf::get_core().connect(&on_key);
        wf::get_core().connect(&on_button);
        wf::get_core().connect(&on_focus_changed);
        wf::get_core().bindings->add_key(convert_binding, &on_convert);

        binding_mods = convert_binding.value().get_modifiers();
#ifdef __DEBUG_SWITCHER__
        LOGW("Binding: key=", convert_binding.value().get_key(), " mods=", mod2string(binding_mods));
#endif
    }

    void fini() override {
        stop_repeat();
        wf::get_core().bindings->rem_binding(&on_convert);
        on_focus_changed.disconnect();
        on_button.disconnect();
        on_key.disconnect();
    }
#ifdef __DEBUG_SWITCHER__
private:

    std::string mod2string(uint32_t mod) {
        if (mod == 0) {
            return "|NONE|";
        }

        static constexpr std::pair<uint32_t, std::string_view> flags[] = {
            {WLR_MODIFIER_SHIFT, "WLR_SHIFT"},
            {WLR_MODIFIER_CAPS,  "WLR_CAPS"},
            {WLR_MODIFIER_CTRL,  "WLR_CTRL"},
            {WLR_MODIFIER_ALT,   "WLR_ALT"},
            {WLR_MODIFIER_MOD2,  "WLR_MOD2"},
            {WLR_MODIFIER_MOD3,  "WLR_MOD3"},
            {WLR_MODIFIER_LOGO,  "WLR_LOGO"},
            {WLR_MODIFIER_MOD5,  "WLR_MOD5"},

            {MODIFIER_SHIFT_L,   "SHIFT_L"},
            {MODIFIER_CTRL_L,    "CTRL_L"},
            {MODIFIER_ALT_L,     "ALT_L"},
            {MODIFIER_META_L,    "META_L"},
            {MODIFIER_SHIFT_R,   "SHIFT_R"},
            {MODIFIER_CTRL_R,    "CTRL_R"},
            {MODIFIER_ALT_R,     "ALT_R"},
            {MODIFIER_META_R,    "META_R"},
        };
        std::string text;
        for (const auto& [mask, name] : flags) {
            if (mod & mask) {
                text += name;
                text += '|';
            }
        }
        return text.empty() ? "|UNKNOWN|" : '|' + text;
    }
#endif
};

}

DECLARE_WAYFIRE_PLUGIN(wf::switcher::word_switcher);


/**
 * vimb - a webkit based vim like browser.
 *
 * Copyright (C) 2012-2013 Daniel Carl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 */

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkkeysyms-compat.h>
#include "hints.h"
#include "dom.h"
#include "command.h"
#include "hints.js.h"

#define HINT_VAR "VpHint"
#define HINT_FILE "hints.js"

extern VbCore vb;
extern const unsigned int MAXIMUM_HINTS;

static void run_script(char *js);
static void fire();
static void observe_input(gboolean observe);
static gboolean changed_cb(GtkEditable *entry);
static gboolean keypress_cb(WebKitWebView *webview, GdkEventKey *event);

void hints_init(WebKitWebFrame *frame)
{
    char *value = NULL;
    vb_eval_script(frame, HINTS_JS, HINT_FILE, &value);
    g_free(value);
}

void hints_clear()
{
    char *js, *value = NULL;

    observe_input(false);
    if (CLEAN_MODE(vb.state.mode) == VB_MODE_HINTING) {
        js = g_strdup_printf("%s.clear();", HINT_VAR);
        vb_eval_script(webkit_web_view_get_main_frame(vb.gui.webview), js, HINT_FILE, &value);
        g_free(value);
        g_free(js);

        g_signal_emit_by_name(vb.gui.webview, "hovering-over-link", NULL, NULL);
    }
}

void hints_create(const char *input, guint mode, const guint prefixLength)
{
    char *js = NULL;
    if (CLEAN_MODE(vb.state.mode) != VB_MODE_HINTING) {
        vb_set_mode(VB_MODE_HINTING, false);

        Style *style = &vb.style;
        vb.hints.prefixLength = prefixLength;
        vb.hints.mode         = mode;
        vb.hints.num          = 0;

        char type, usage;
        /* convert the mode into the type chare used in the hint script */
        type = mode & HINTS_TYPE_LINK ? 'l' : 'i';

        if (mode & HINTS_PROCESS_OPEN) {
            usage = mode & HINTS_OPEN_NEW ? 'T' : 'O';
        } else {
            usage = 'U';
        }

        js = g_strdup_printf(
            "%s = new VimbHints('%c', '%c', '%s', '%s', '%s', '%s', %d);",
            HINT_VAR, type, usage,
            style->hint_bg,
            style->hint_bg_focus,
            style->hint_fg,
            style->hint_style,
            MAXIMUM_HINTS
        );

        observe_input(true);

        run_script(js);
        g_free(js);
    }


    js = g_strdup_printf("%s.create('%s');", HINT_VAR, input ? input : "");
    run_script(js);
    g_free(js);
}

void hints_update(const gulong num)
{
    char *js = g_strdup_printf("%s.update(%lu);", HINT_VAR, num);
    run_script(js);
    g_free(js);
}

void hints_focus_next(const gboolean back)
{
    char *js = g_strdup_printf(back ? "%s.focusPrev()" : "%s.focusNext();", HINT_VAR);
    run_script(js);
    g_free(js);
}

static void run_script(char *js)
{
    char *value = NULL;
    int mode = vb.hints.mode;

    gboolean success = vb_eval_script(
        webkit_web_view_get_main_frame(vb.gui.webview), js, HINT_FILE, &value
    );
    if (!success) {
        fprintf(stderr, "%s\n", value);
        g_free(value);

        vb_set_mode(VB_MODE_NORMAL, false);

        return;
    }

    if (!strncmp(value, "OVER:", 5)) {
        g_signal_emit_by_name(
            vb.gui.webview, "hovering-over-link", NULL, *(value + 5) == '\0' ? NULL : (value + 5)
        );
    } else if (!strncmp(value, "DONE:", 5)) {
        vb_set_mode(VB_MODE_NORMAL, true);
    } else if (!strncmp(value, "INSERT:", 7)) {
        vb_set_mode(VB_MODE_INSERT, false);
    } else if (!strncmp(value, "DATA:", 5)) {
        Arg a = {0};
        char *v = (value + 5);
        if (mode & HINTS_PROCESS_INPUT) {
            a.s = g_strconcat((mode & HINTS_OPEN_NEW) ? ":tabopen " : ":open ", v, NULL);
            command_input(&a);
            g_free(a.s);
        } else {
            a.i = COMMAND_YANK_PRIMARY | COMMAND_YANK_SECONDARY;
            a.s = v;
            command_yank(&a);
        }
    }
    g_free(value);
}

static void fire()
{
    char *js = g_strdup_printf("%s.fire();", HINT_VAR);
    run_script(js);
    g_free(js);
}

static void observe_input(gboolean observe)
{
    if (observe) {
        vb.hints.change_handler = g_signal_connect(
            G_OBJECT(vb.gui.inputbox), "changed", G_CALLBACK(changed_cb), NULL
        );
        vb.hints.keypress_handler = g_signal_connect(
            G_OBJECT(vb.gui.inputbox), "key-press-event", G_CALLBACK(keypress_cb), NULL
        );
    } else if (vb.hints.change_handler && vb.hints.keypress_handler) {
        g_signal_handler_disconnect(G_OBJECT(vb.gui.inputbox), vb.hints.change_handler);
        g_signal_handler_disconnect(G_OBJECT(vb.gui.inputbox), vb.hints.keypress_handler);

        vb.hints.change_handler = vb.hints.keypress_handler = 0;
    }
}

static gboolean changed_cb(GtkEditable *entry)
{
    const char *text = GET_TEXT();

    /* skip hinting prefixes like '.', ',', ';y' ... */
    hints_create(text + vb.hints.prefixLength, vb.hints.mode, vb.hints.prefixLength);

    return true;
}

static gboolean keypress_cb(WebKitWebView *webview, GdkEventKey *event)
{
    int numval;
    guint keyval = event->keyval;
    guint state  = CLEAN_STATE_WITH_SHIFT(event);

    if (keyval == GDK_Return) {
        fire();
        return true;
    }
    if (keyval == GDK_BackSpace && (state & GDK_SHIFT_MASK) && (state & GDK_CONTROL_MASK)) {
        vb.hints.num /= 10;
        hints_update(vb.hints.num);

        return true;
    }
    numval = g_unichar_digit_value((gunichar)gdk_keyval_to_unicode(keyval));
    if ((numval >= 1 && numval <= 9) || (numval == 0 && vb.hints.num)) {
        /* allow a zero as non-first number */
        vb.hints.num = (vb.hints.num ? vb.hints.num * 10 : 0) + numval;
        hints_update(vb.hints.num);

        return true;
    }

    return false;
}

/* ncdc - NCurses Direct Connect client

  Copyright (c) 2011 Yoran Heling

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/


#include "ncdc.h"
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <signal.h>
#include <wchar.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <glib/gstdio.h>


// input handling declarations

#if INTERFACE

#define KEY_ESCAPE (KEY_MAX+1)
#define INPT_KEY  0
#define INPT_CHAR 1
#define INPT_CTRL 2
#define INPT_ALT  4

struct input_key {
  gunichar code;   // character code (as given by get_wch() - but re-encoded)
  char type;       // INPT_ type
  char encoded[7]; // UTF-8 encoded character string (if type != key)
};

#endif


// global variables
const char *conf_dir;
GKeyFile *conf_file;
GMainLoop *main_loop;



// utility function (uses statically allocated memory for convenience, obviously not thread-safe in any way)
char *get_hub_group(const char *hubname) {
  static char hubgroup[152]; // "#" + name of hub tab (name = max 25 char = 150 bytes...)
  strcpy(hubgroup, "#");
  strcat(hubgroup, hubname);
  return hubgroup;
}


static void handle_input() {
  /* Mapping from get_wch() to struct input_key:
   *  KEY_CODE_YES -> KEY
   *  KEY_CODE_NO:
   *    char == 127           -> KEY = KEY_BACKSPACE
   *    char <= 31            -> CTRL
   *    !'^['                 -> CHAR
   *    ('^[', !)             -> KEY = KEY_ESCAPE
   *    ('^[', !CHAR)         -> ignore both characters (1)
   *    ('^[', CHAR && '[')   -> ignore both characters and the character after that (2)
   *    ('^[', CHAR && !'[')  -> ALT = second char
   *
   * 1. this is something like ctrl+alt+X, which we won't use
   * 2. these codes indicate a 'Key' that somehow wasn't captured with
   *    KEY_CODE_YES. We won't attempt to interpret these ourselves.
   *
   * There are still several unhandled issues:
   * - Ncurses does not catch all key codes, and there is no way of knowing how
   *   many bytes a key code spans. Things like ^[[1;3C won't be handled correctly. :-(
   * - Ncurses can actually return key codes > KEY_MAX, but does not provide
   *   any mechanism for figuring out which key it actually was.
   * - It may be useful to use define_key() for some special (and common) codes
   * - Modifier keys will always be a problem. Most alt+key things work, except
   *   for those that may start a control code. alt+[ is a famous one, but
   *   there are others (like alt+O on my system). This is system-dependend,
   *   and again we have no way of knowing these things. (except perhaps by
   *   reading termcap entries on our own?)
   */

  struct input_key key;
  int r;
  wint_t code;
  int lastesc = 0, curignore = 0;
  while((r = get_wch(&code)) != ERR) {
    if(curignore) {
      curignore = 0;
      continue;
    }
    // we use SIGWINCH, so KEY_RESIZE can be ignored
    if(r == KEY_CODE_YES && code == KEY_RESIZE)
      continue;
    // backspace is often sent as DEL control character, correct this
    if(r != KEY_CODE_YES && code == 127) {
      r = KEY_CODE_YES;
      code = KEY_BACKSPACE;
    }
    key.type = r == KEY_CODE_YES ? INPT_KEY : code == 27 ? INPT_ALT : code <= 31 ? INPT_CTRL : INPT_CHAR;
    key.code = code;
    // do something with key.code and key.encoded
    if(key.type == INPT_CHAR) {
      if((r = wctomb(key.encoded, code)) < 0)
        g_warning("Cannot encode character 0x%X", code);
      key.encoded[r] = 0;
      key.code = g_utf8_get_char_validated(key.encoded, -1);
      if(key.code == (gunichar)-1 || key.code == (gunichar)-2) {
        g_warning("Invalid UTF-8 sequence in keyboard input. Are you sure you are running a UTF-8 locale?");
        continue;
      }
    } else if(key.type != INPT_KEY) {
      // if it's not a "character" nor a key, then it very likely fits in a single byte
      key.encoded[0] = key.code;
      key.encoded[1] = 0;
    }
    // check for escape sequence
    if(lastesc) {
      lastesc = 0;
      if(key.type != INPT_CHAR)
        continue;
      if(key.code == '[') {
        curignore = 0;
        continue;
      }
      key.type = INPT_ALT;
      ui_input(&key);
      continue;
    }
    if(key.type == INPT_ALT) {
      lastesc = 1;
      continue;
    }
    // no escape sequence? just insert it into the array
    if(key.type == INPT_CHAR || key.type == INPT_KEY)
      ui_input(&key);
    if(key.type == INPT_CTRL)
      ui_input(&key); // TODO: control code to printable char?
  }
  if(lastesc) {
    key.type = INPT_KEY;
    key.code = KEY_ESCAPE;
    ui_input(&key);
  }

  ui_draw();
}


static gboolean stdin_read(GIOChannel *src, GIOCondition cond, gpointer dat) {
  handle_input();
  return TRUE;
}


static gboolean screen_update_timer(gpointer dat) {
  ui_draw();
  return TRUE;
}


// Fired when the screen is resized.  Normally I would check for KEY_RESIZE,
// but that doesn't work very nicely together with select(). See
// http://www.webservertalk.com/archive107-2005-1-896232.html
static void catch_sigwinch(int sig) {
  endwin();
  doupdate();
  ui_draw();
}


void save_config() {
  char *dat = g_key_file_to_data(conf_file, NULL, NULL);
  char *cf = g_build_filename(conf_dir, "config.ini", NULL);
  FILE *f = fopen(cf, "w");
  if(!f || fputs(dat, f) < 0 || fclose(f))
    g_critical("Cannot save config file '%s': %s", cf, g_strerror(errno));
  g_free(dat);
  g_free(cf);
}


// TODO: make sure that there is no other ncdc instance working with the same config directory
static void init_config() {
  // get location of the configuration directory
  conf_dir = g_getenv("NCDC_DIR");
  if(!conf_dir)
    conf_dir = g_build_filename(g_get_home_dir(), ".ncdc", NULL);

  // try to create it (ignoring errors if it already exists)
  g_mkdir(conf_dir, 0700);
  if(g_access(conf_dir, F_OK | R_OK | X_OK | W_OK) < 0)
    g_error("Directory '%s' does not exist or is not writable.", conf_dir);

  // we should also have a logs/ subdirectory
  char *logs = g_build_filename(conf_dir, "logs", NULL);
  g_mkdir(logs, 0777);
  if(g_access(conf_dir, F_OK | R_OK | X_OK | W_OK) < 0)
    g_error("Directory '%s' does not exist or is not writable.", logs);
  g_free(logs);

  // load config file (or create it)
  conf_file = g_key_file_new();
  char *cf = g_build_filename(conf_dir, "config.ini", NULL);
  GError *err = NULL;
  if(g_file_test(cf, G_FILE_TEST_EXISTS)) {
    if(!g_key_file_load_from_file(conf_file, cf, G_KEY_FILE_KEEP_COMMENTS, &err))
      g_error("Could not load '%s': %s", cf, err->message);
  }
  // always set the initial comment
  g_key_file_set_comment(conf_file, NULL, NULL,
    "This file is automatically managed by ncdc.\n"
    "While you could edit it yourself, doing so is highly discouraged.\n"
    "It is better to use the respective commands to change something.\n"
    "Warning: Editing this file while ncdc is running may result in your changes getting lost!", NULL);
  // make sure a nick is set
  if(!g_key_file_has_key(conf_file, "global", "nick", NULL)) {
    char *nick = g_strdup_printf("ncdc_%d", g_random_int_range(1, 9999));
    g_key_file_set_string(conf_file, "global", "nick", nick);
    g_free(nick);
  }
  save_config();
  g_free(cf);
}


#define log_to_str(level) (\
  (level) & G_LOG_LEVEL_ERROR    ? "ERROR"    :\
  (level) & G_LOG_LEVEL_CRITICAL ? "CRITICAL" :\
  (level) & G_LOG_LEVEL_WARNING  ? "WARNING"  :\
  (level) & G_LOG_LEVEL_MESSAGE  ? "message"  :\
  (level) & G_LOG_LEVEL_INFO     ? "info"     : "debug")

// clean-up our ncurses window before throwing a fatal error
static void log_fatal(const gchar *dom, GLogLevelFlags level, const gchar *msg, gpointer dat) {
  endwin();
  printf("*%s* %s\n", log_to_str(level), msg);
}


// redirect all non-fatal errors to the main window
static void log_redirect(const gchar *dom, GLogLevelFlags level, const gchar *msg, gpointer dat) {
  ui_logwindow_printf(ui_main->log, "*%s* %s", log_to_str(level), msg);
}


int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  // TODO: check that the current locale is UTF-8. Things aren't going to work otherwise

  // init stuff
  g_thread_init(NULL);
  g_type_init();
  init_config();
  ui_cmdhist_init("history");
  ui_init();

  // setup logging
  g_log_set_handler(NULL, G_LOG_FATAL_MASK | G_LOG_FLAG_FATAL | G_LOG_LEVEL_ERROR, log_fatal, NULL);
  g_log_set_default_handler(log_redirect, NULL);

  // setup SIGWINCH
  struct sigaction act;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_RESTART;
  act.sa_handler = catch_sigwinch;
  if(sigaction(SIGWINCH, &act, NULL) < 0)
    g_error("Can't setup SIGWINCH: %s", g_strerror(errno));

  // init and start main loop
  main_loop = g_main_loop_new(NULL, FALSE);

  GIOChannel *in = g_io_channel_unix_new(STDIN_FILENO);
  g_io_add_watch(in, G_IO_IN, stdin_read, NULL);

  g_timeout_add_seconds(1, screen_update_timer, NULL);

  g_main_loop_run(main_loop);

  // cleanup
  ui_cmdhist_close();
  erase();
  refresh();
  endwin();

  return 0;
}

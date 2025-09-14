#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define DITTO_VERSION "v0.0.0"
#define ENABLE_LOG

#define UNUSED(x) (void)(x);

// Ctrl-() bitwises AND with 00011111 (0x1f, 31)
// e.g.
//      b = 98
//      ctrl-b = 2
//      98 & 31 = 2
#define CTRL_KEY(k) ((k) & 0x1f)

// Clear screen escape sequence:
//
// |++++++++|++++++++|++++++++|++++++++|
//    \x1b      [         2        J
//   Escape  StartEsc  Screen   Erase
//
// VT100 Escape Sequences (widely supported by terminals):
// - 0J Clear from cursor position to end screen
// - 1J Clear up to cursor position
// - 2J Clear full screen
#define CLEAR_SCREEN "\x1b[2J"
#define CLEAR_SCREEN_SZ 4
// Clear screen takes 2 arguments [RowNo;ColNo] e.g. <esc>[12;40H
// Default is 1;1 (rows and cols start at 1, not 0)
#define REPOS_CURSOR "\x1b[H"
#define REPOS_CURSOR_SZ 3
// Get cursor position (n command = Device Status Report, 6 is for Cursor Pos)
#define GET_CURSOR "\x1b[6n"
#define GET_CURSOR_SZ 4
// SetMode (h) and ResetMode (l) set on/off term features or modes like cursor
// visibility
#define HIDE_CURSOR "\x1b[?25h"
#define HIDE_CURSOR_SZ 6
// Erase in line, it also takes param (0 [default] = right to cursor, 1 = left
// to cursor, 2 = all line)
#define ERASE_LINE_RIGHT "\x1b[K"
#define ERASE_LINE_RIGHT_SZ 3

#define ABUF_INIT {NULL, 0}

/*** data ***/

typedef struct {
  // Cursor position
  uint16_t cx, cy;
  // Screen size
  uint16_t screenrows, screencols;
  // Log file
  FILE *logfile;
  int logfd;
  // Terminal status
  struct termios orig_termios;
} EditorConfig;

EditorConfig E;

typedef struct abuf {
  char *b;
  uint32_t len;
} AppendBuffer;

/*** util ***/

void die(const char *s) {
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SZ);
  write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);

  perror(s);
  exit(1);
}

void dlog(const char *format, ...) {
  va_list args;
  va_start(args, format);

  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char timestr[64];
  strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm);
  fprintf(E.logfile, "%s - ", timestr);
  vfprintf(E.logfile, format, args);
  fprintf(E.logfile, "\n");

  va_end(args);
}

/*** terminal ***/

void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;

  // Disabling some Input flags:
  // - Break condition causing sigint
  // - Signal ctrl-m (fix specific)
  // - Parity check (old)
  // - 8th bit each input being stripped out
  // - Signals flow control (ctrl-s, ctrl-q)
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

  // Disabling some Output flags:
  // - Output processing (\n, \r\n) -> must add \r ourself now
  raw.c_oflag &= ~(OPOST);

  // Disabling some Local flags:
  // - Echoing
  // - Canonical (byte by byte read) mode
  // - Signals Ctrl-v (wait input), ctrl-o fix in macos
  // - Signals int and tstp (ctrl-c, ctrl-z)
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // Disabling some Control flags:
  // - Old stuff
  raw.c_cflag &= ~(CS8);

  // Setting some Control Characters:
  // read() witll return as soon as any byte is read
  raw.c_cc[VMIN] = 0;
  // max amount of time to wait before read() returns (tenths of a second)
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

char editorReadKey(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  return c;
}

int getCursorPosition(uint16_t *rows, uint16_t *cols) {
  // Avoid unused rows, cols
  UNUSED(rows);
  UNUSED(cols);

  char buf[32];
  uint16_t i = 0;

  if (write(STDOUT_FILENO, GET_CURSOR, GET_CURSOR_SZ) != GET_CURSOR_SZ)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  // Debug print to show the cursor position
  // printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%hd;%hd", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(uint16_t *rows, uint16_t *cols) {
  struct winsize ws;

  // Get the size of the terminals on most systems
  // (Terminal Input Output Control Get WINdow SiZe)
  // There is a fallback in case it would fail
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // Position the cursor to the bottom right and get rows,cols
    // Cursor forward (C), cursor down (B), 999 just to make sure to get to the
    // end
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;

    return getCursorPosition(rows, cols);
  }

  *cols = ws.ws_col;
  *rows = ws.ws_row;

  return 0;
}

/*** append buffer ***/

void abAppend(AppendBuffer *ab, const char *s, uint32_t len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    die("realloc");

  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(AppendBuffer *ab) { free(ab->b); }

/*** output ***/

void editorDrawRows(AppendBuffer *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    abAppend(ab, "~", 1);

    if (y == E.screenrows / 2) {
      char wlc[20];
      int l = snprintf(wlc, sizeof(wlc), "Ditto -- %s", DITTO_VERSION);
      int pad = (E.screencols - l) / 2;
      char line[E.screencols + 1];
      memset(line, ' ', pad);
      memcpy(line + pad, wlc, l);
      abAppend(ab, line, pad + l);
    }

    abAppend(ab, ERASE_LINE_RIGHT, ERASE_LINE_RIGHT_SZ);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen(void) {
  AppendBuffer ab = ABUF_INIT;

  // To avoid cursor flickering, hide the cursor before clearing the screen and
  // showing it later again
  abAppend(&ab, HIDE_CURSOR, HIDE_CURSOR_SZ);
  abAppend(&ab, REPOS_CURSOR, REPOS_CURSOR_SZ);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%hu;%huH", E.cy + 1, E.cx + 1);

#ifdef ENABLE_LOG
  dlog("%hu;%hu", E.cy + 1, E.cx + 1);
#endif

  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, REPOS_CURSOR, REPOS_CURSOR_SZ);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/

void destroyEditor(void) {
#ifdef ENABLE_LOG
  fclose(E.logfile);
#endif
}

void initEditor(void) {
  E.cx = 10;
  E.cy = 20;

#ifdef ENABLE_LOG
  E.logfile = fopen("/tmp/dittolog.txt", "a");
  if (E.logfile == NULL)
    die("fopen");
  dlog("Welcome to Ditto Editor!");
#endif

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");

  atexit(destroyEditor);
}

void editorProcessKeypress(void) {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SZ);
    write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);
    exit(0);
    break;
  }
}

/*** init ***/

int main(void) {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}

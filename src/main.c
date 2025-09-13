#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

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

/*** data ***/

typedef struct {
  uint16_t screenrows;
  uint16_t screencols;
  struct termios orig_termios;
} EditorConfig;

EditorConfig E;

void die(const char *s) {
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SZ);
  write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);

  perror(s);
  exit(1);
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

/*** output ***/

void editorDrawRows(void) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    write(STDOUT_FILENO, "~", 1);

    if (y < E.screenrows - 1) {
      write(STDOUT_FILENO, "\r\n", 2);
    }
  }
}

void editorRefreshScreen(void) {
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SZ);
  write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);

  editorDrawRows();

  write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);
}

/*** input ***/

void initEditor(void) {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
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

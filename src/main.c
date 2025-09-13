#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

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

/*** data ***/

struct termios orig_termios;

void die(const char *s) {
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SZ);
  write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);

  perror(s);
  exit(1);
}

/*** terminal ***/

void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_termios;

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

char editReadKey(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  return c;
}

/*** output ***/

void editorRefreshScreen(void) {
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SZ);
  write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);
}

/*** input ***/

void editorProcessKeypress(void) {
  char c = editReadKey();

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

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}

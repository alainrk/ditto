#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode(void) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void enableRawMode(void) {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disableRawMode);

  struct termios raw = orig_termios;

  // Disable:
  // - Signal ctrl-m (fix specific)
  // - Signals flow control (ctrl-s, ctrl-q)
  raw.c_iflag &= ~(ICRNL | IXON);
  // - Output processing (\n, \r\n) -> must add \r ourself now
  raw.c_oflag &= ~(OPOST);
  // - Echoing
  // - Canonical (byte by byte read) mode
  // - Signals Ctrl-v (wait input), ctrl-o fix in macos
  // - Signals int and tstp (ctrl-c, ctrl-z)
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(void) {
  enableRawMode();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
  }

  return 0;
}

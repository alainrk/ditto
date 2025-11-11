#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "dlogger.h"
#include "dmalloc.h"
#include "fss.h"

/*** defines ***/

#define DITTO_VERSION "v0.0.0"
#define DITTO_TAB_STOP 2
#define DITTO_LINENO_ENABLED 1
#define DITTO_QUIT_TIMES 2
#define DITTO_STATUSMSG_SEC 5

#define UNUSED(x) (void)(x);

// Ctrl-() bitwises AND with 00011111 (0x1f, 31)
// e.g.
//      b = 98
//      ctrl-b = 2
//      98 & 31 = 2
#define CTRL_KEY(k) ((k) & 0x1f)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

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
// visibility, the number is the feature (25 = show/hine)
#define HIDE_CURSOR "\x1b[?25l"
#define HIDE_CURSOR_SZ 6
#define SHOW_CURSOR "\x1b[?25h"
#define SHOW_CURSOR_SZ 6
// Cursor styles: 1=blinking block, 2=steady block, 3=blinking underline,
// 4=steady underline, 5=blinking bar, 6=steady bar
#define CURSOR_BLOCK "\x1b[2 q"
#define CURSOR_BLOCK_SZ 5
#define CURSOR_BAR_BLINK "\x1b[5 q"
#define CURSOR_BAR_BLINK_SZ 5
// Erase in line, it also takes param (0 [default] = right to cursor, 1 = left
// to cursor, 2 = all line)
#define ERASE_LINE_RIGHT "\x1b[K"
#define ERASE_LINE_RIGHT_SZ 3
// Terminal color management
#define COLORS_INVERT_ON "\x1b[7m"
#define COLORS_INVERT_ON_SZ 4
#define COLORS_BOLD_ON "\x1b[1m"
#define COLORS_BOLD_ON_SZ 4
#define COLORS_BOLD_OFF "\x1b[22m"
#define COLORS_BOLD_OFF_SZ 5
#define COLORS_ALL_OFF "\x1b[m"
#define COLORS_ALL_OFF_SZ 3

// Position the cursor (forward: C, down: B)
#define POS_CURSOR_AT(x, y) "\x1b[" #y "C\x1b[" #x "B"

// Amount of microseconds to wait when waiting for key sequences
#define SEQUENCES_TIMEOUT_MICROSEC 100000 // 100ms

#define ABUF_INIT {NULL, 0}

#define CHAR_FAMILY_WORDS 0
#define CHAR_FAMILY_SPACES 1
#define CHAR_FAMILY_OTHERS 2

/*** prototypes ***/

char *editorPrompt(char *prompt);

/*** enum ***/

enum editorMode { NORMAL_MODE = 0, INSERT_MODE, VISUAL_MODE, COMMAND_MODE };

static const char *const mode_str[] = {
    [NORMAL_MODE] = "NORMAL",
    [INSERT_MODE] = "INSERT",
    [VISUAL_MODE] = "VISUAL",
    [COMMAND_MODE] = "COMMAND",
};

enum editorCommands {
  CMD_GO_TOP_DOC = 2000,
  CMD_GO_BOTTOM_DOC,
};

enum keys {
  KEY_ESC = 27,
  KEY_COLON = 58,
  KEY_0 = '0',
  KEY_A = 'A',
  KEY_D = 'D',
  KEY_G = 'G',
  KEY_H = 'H',
  KEY_J = 'J',
  KEY_K = 'K',
  KEY_I = 'I',
  KEY_L = 'L',
  KEY_O = 'O',
  KEY_P = 'P',
  KEY_X = 'X',
  KEY_Y = 'Y',
  KEY_a = 'a',
  KEY_d = 'd',
  KEY_e = 'e',
  KEY_g = 'g',
  KEY_h = 'h',
  KEY_i = 'i',
  KEY_j = 'j',
  KEY_k = 'k',
  KEY_l = 'l',
  KEY_o = 'o',
  KEY_p = 'p',
  KEY_v = 'v',
  KEY_w = 'w',
  KEY_x = 'x',
  KEY_y = 'y',
  KEY_TAB = '\t',
  KEY_BACKSPACE = 127,
  ARROW_UP = 1000,
  ARROW_DOWN,
  ARROW_LEFT,
  ARROW_RIGHT,
  HOME_KEY,
  INSERT_KEY,
  DELETE_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
};

/*** data ***/

typedef struct {
  // Real length of the row
  int size;
  // Rendered length of the row
  int rsize;
  // Real Characters in the row
  char *chars;
  // Rendered characters in the row
  char *render;
} Row;

typedef struct {
  DLogger *logger;
  // Current cursor X-position relative to the actual chars in the file
  int cx;
  // Current cursor Y-position relative to the actual chars in the file
  int cy;
  // Current cursor X-position relative to the rendered chars in the file
  int rx;
  // Current cursor Y-position relative to the rendered chars in the file
  int ry;
  // Row offset in current file the cursor is on
  int rowoff;
  // Column offset in current file the cursor is on
  int coloff;
  // Screen size
  int screenrows, screencols;
  // Number of rows in the file
  int numrows;
  // Editor rows
  Row *row;
  // Dirty flag indicates if buffer has changes not yet saved
  int dirty;
  // Current mode
  enum editorMode mode;
  // Currently open filename
  char *filename;
  // Status messages stack
  FixedSizeStack *messages;
  // Status message
  char statusmsg[80];
  // Status message time
  time_t statusmsg_time;
  // Terminal status
  struct termios orig_termios;
  // Screen resize flag
  volatile sig_atomic_t screen_resized;
  // NOTE: For now it's just a single register
  char *reg;
  // Message bar input state
  int input_mode;     // 0 = normal editing, 1 = message bar input active
  char *input_prompt; // Prompt text (e.g., ":" or "Filename to save to: %s")
  char *input_buffer; // Input buffer for message bar
  size_t input_buffer_size; // Allocated size of input buffer
  size_t input_buffer_len;  // Current length of input in buffer
} EditorConfig;

EditorConfig E;

typedef struct {
  char *b;
  int len;
} AppendBuffer;

/*** utils ***/

void die(const char *s) {
  write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SZ);
  write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);

  perror(s);
  exit(1);
}

void handleResize(int sig) {
  UNUSED(sig);
  E.screen_resized = 1;
}

int getCharFamily(char c) {
  return (isalnum((c)) || (c) == '_') ? CHAR_FAMILY_WORDS
         : (isspace((c)))             ? CHAR_FAMILY_SPACES
                                      : CHAR_FAMILY_OTHERS;
}

/*** terminal ***/

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  int len = vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);

  fss_push(E.messages, E.statusmsg, len);
}

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
  raw.c_cflag |= (CS8);

  // Setting some Control Characters:
  // read() witll return as soon as any byte is read
  raw.c_cc[VMIN] = 0;
  // max amount of time to wait before read() returns (tenths of a second)
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey(void) {
  int nread;
  char c = '\0';

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  // Escape-starting keys (e.g. arrows), need to read multiple bytes starting
  // from that
  if (c == '\x1b') {
    char seq[3];

    // By default just return the escape char, if nothing else avaible to be
    // read
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    // If Start Escape char '['
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '2':
            return INSERT_KEY;
          case '3':
            return DELETE_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        // Up Arrow
        case 'A':
          return ARROW_UP;
        // Down Arrow
        case 'B':
          return ARROW_DOWN;
        // Right Arrow
        case 'C':
          return ARROW_RIGHT;
        // Left Arrow
        case 'D':
          return ARROW_LEFT;

          // Home Key
        case 'H':
          return HOME_KEY;
          // End Key
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }

    // By default just return the escape char
    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  // Avoid unused rows, cols
  UNUSED(rows);
  UNUSED(cols);

  char buf[32];
  unsigned long i = 0;

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
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  // Get the size of the terminals on most systems
  // (Terminal Input Output Control Get WINdow SiZe)
  // There is a fallback in case it would fail
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // Position the cursor to the bottom right and get rows,cols (use 999 just
    // to be sure)
    if (write(STDOUT_FILENO, POS_CURSOR_AT(999, 999), 12) != 12)
      return -1;

    return getCursorPosition(rows, cols);
  }

  *cols = ws.ws_col;
  *rows = ws.ws_row;

  return 0;
}

/*** line number operations ***/

int editorGetLineNumberWidth(void) {
  if (!DITTO_LINENO_ENABLED)
    return 0;
  return 5; // "9999 " format (4 digits + space)
}

void updateScreenSize(void) {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");

  dlog_debug(E.logger, "Screen resized to: %d x %d", E.screenrows,
             E.screencols);

  // Make space for the line numbers
  E.screencols -= editorGetLineNumberWidth();

  // Make space for status bar and status message
  E.screenrows -= 2;

  // Validate cursor position after resize
  if (E.cy >= E.screenrows + E.rowoff) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

  if (E.rx >= E.screencols + E.coloff) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

/*** row operations ***/

int editorRowCxToRx(Row *row, int cx) {
  int rx = 0;

  // Rendered cursor x-position needs to advance according to chosen rendered
  // spaces for tab, for each tab in the line so far
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t') {
      rx += (DITTO_TAB_STOP - 1) - (rx % DITTO_TAB_STOP);
    }
    rx++;
  }

  return rx;
}

void editorUpdateRow(Row *row) {
  int tabs = 0;

  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t')
      tabs++;
  }

  dfree(row->render);
  row->render = dmalloc(row->size + tabs * (DITTO_TAB_STOP - 1) + 1);

  int idx = 0;
  for (int j = 0; j < row->size; j++) {
    // Tabs rendering
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % DITTO_TAB_STOP != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }

  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows)
    return;

  E.row = drealloc(E.row, sizeof(Row) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(Row) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = dmalloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorFreeRow(Row *row) {
  dfree(row->render);
  dfree(row->chars);
}

void editorDeleteRow(int at) {
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(Row) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(Row *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  // Make space for 1 char + NULL terminator
  row->chars = drealloc(row->chars, row->size + 2);
  // Like realloc but safe when src/dest can overlap
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  // Update render and rsize with the new row content
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(Row *row, char *s, size_t len) {
  row->chars = drealloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
  // EOF, add new row
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline(void) {
  if (E.cx == 0) {
    // At the beginning of the line, just insert a new line above
    editorInsertRow(E.cy, "", 0);
  } else {
    // Otherwise split the current line and insert with the second part of the
    // content of the current one below
    Row *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }

  // Bring the cursor to the newline
  E.cy++;
  E.cx = 0;
}

void editorRowDeleteChar(Row *row, int at) {
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

void editorDeleteChar(void) {
  if (E.cy == E.numrows)
    return;

  // At the beginning of the first line, there's nothing to do
  if (E.cx == 0 && E.cy == 0)
    return;

  Row *row = &E.row[E.cy];
  if (E.cx > 0) {
    // If there's a character at the left of the cursor, we delete it and move
    // the cursor to the left
    editorRowDeleteChar(row, E.cx - 1);
    E.cx--;
  } else {
    // Backspacing at the beginning of the line means we need to merge current
    // line and previous one, so we append the current line to that and delete
    // it
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDeleteRow(E.cy);
    E.cy--;
  }
}

/*** file i/o ***/

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  for (int j = 0; j < E.numrows; j++) {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;

  char *buf = dmalloc(totlen);
  char *p = buf;
  for (int j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(const char *filename) {
  dfree(E.filename);
  E.filename = dstrdup(filename);

  FILE *f = fopen(filename, "a+");
  if (!f)
    die("fopen");
  fseek(f, 0, SEEK_SET);

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, f)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;

    editorInsertRow(E.numrows, line, linelen);
  }

  free(line); // Not used dmalloc here
  fclose(f);
  E.dirty = 0;
}

int editorSave(void) {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Filename to save to: %s");
    if (E.filename == NULL)
      return 1;
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd == -1) {
    dlog_debug(E.logger, "Could not save file %s", E.filename);
    editorSetStatusMessage("Could not save file %s", E.filename);
    dfree(buf);
    return 1;
  }

  int err = ftruncate(fd, len);
  if (!err)
    err = (write(fd, buf, len) != len);

  if (!err)
    editorSetStatusMessage("%d bytes written to %s", len, E.filename);

  close(fd);
  dfree(buf);
  E.dirty = 0;
  return err;
}

/*** append buffer ***/

void abAppend(AppendBuffer *ab, const char *s, int len) {
  char *new = drealloc(ab->b, ab->len + len);

  if (new == NULL)
    die("drealloc");

  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(AppendBuffer *ab) { dfree(ab->b); }

/*** output ***/

void editorScroll(void) {
  E.rx = 0;

  // Horizontal scroll based on rendered chars
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  // Cursor is above visible window
  if (E.cy < E.rowoff) {
    // Align file offset to the cursor
    E.rowoff = E.cy;
  }

  // Cursor is below visible window
  if (E.cy >= E.rowoff + E.screenrows) {
    // Align file offset to the cursor + the y-size of the screen
    E.rowoff = E.cy - E.screenrows + 1;
  }

  // Handle rendered difference of x position of the cursor
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }

  // Cursor is to the left of visible window
  if (E.cx < E.coloff) {
    E.coloff = E.cx;
  }
  // Cursor is to the right of visible window
  if (E.cx >= E.coloff + E.screencols) {
    E.coloff = E.cx - E.screencols + 1;
  }
}

void editorDrawRows(AppendBuffer *ab) {
  for (int y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;

    // Print the line number
    if (DITTO_LINENO_ENABLED && filerow < E.numrows) {
      char line[16];
      snprintf(line, sizeof(line), "%4d ", filerow + 1);
      abAppend(ab, line, strlen(line));
    }

    // If we are at the end of the file
    if (filerow >= (int)E.numrows) {
      abAppend(ab, "~", 1);

      // Welcome message if no content or no file loaded
      if (E.numrows == 0 && y == E.screenrows / 2) {
        char wlc[20];
        int l = snprintf(wlc, sizeof(wlc), "Ditto -- %s", DITTO_VERSION);
        int pad = (E.screencols - l) / 2;
        char line[E.screencols + 1];
        memset(line, ' ', pad);
        memcpy(line + pad, wlc, l);
        abAppend(ab, line, pad + l);
      }
    } else {
      // Print the row otherwise, considering the column offset
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    // Clear the rest of the line and go newline in the terminal
    abAppend(ab, ERASE_LINE_RIGHT, ERASE_LINE_RIGHT_SZ);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(AppendBuffer *ab) {
  abAppend(ab, COLORS_INVERT_ON, COLORS_INVERT_ON_SZ);
  char status[80];
  char rstatus[80];

  int len = snprintf(status, sizeof(status), " %s%s%s %.20s %s", COLORS_BOLD_ON,
                     mode_str[E.mode], COLORS_BOLD_OFF,
                     E.filename ? E.filename : "[No Name]",
                     E.dirty ? "(edited)" : "");

  int rlen = snprintf(rstatus, sizeof(rstatus), "%d:%d ", E.cy + 1, E.rx + 1);

  // Remove not visible chars from count
  int nonprintable = 0;
  nonprintable += COLORS_BOLD_ON_SZ + COLORS_BOLD_OFF_SZ;

  // Visible chars counting
  int vizlen = len - nonprintable;

  // Use full terminal width for statusbar (add back line number width)
  int fullwidth = E.screencols + editorGetLineNumberWidth();

  // Handle overflow of the statusbar content
  if (vizlen > fullwidth)
    vizlen = fullwidth;

  // Assumption: I append the statusbar, but if too long, truncate it,
  // remembering to add the nonprintable chars of the mode, which we assume
  // are always there
  abAppend(ab, status, vizlen + nonprintable);

  // Fill the rest of the statusbar with spaces
  while (vizlen + rlen < fullwidth) {
    abAppend(ab, " ", 1);
    vizlen++;
  }

  abAppend(ab, rstatus, rlen);

  abAppend(ab, COLORS_ALL_OFF, COLORS_ALL_OFF_SZ);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(AppendBuffer *ab) {
  abAppend(ab, ERASE_LINE_RIGHT, ERASE_LINE_RIGHT_SZ);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < DITTO_STATUSMSG_SEC) {
    abAppend(ab, E.statusmsg, msglen);
  }
}

void editorRefreshScreen(void) {
  AppendBuffer ab = ABUF_INIT;

  // Handle screen resize
  if (E.screen_resized) {
    updateScreenSize();
    E.screen_resized = 0;
  }

  editorScroll();

  // To avoid cursor flickering, hide the cursor before clearing the screen
  // and showing it later again
  abAppend(&ab, HIDE_CURSOR, HIDE_CURSOR_SZ);
  abAppend(&ab, REPOS_CURSOR, REPOS_CURSOR_SZ);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];

  // Position cursor based on input mode
  if (E.input_mode) {
    // Cursor in message bar (last line of terminal)
    int row = E.screenrows + 2; // +1 for status bar, +1 for message bar
    // Calculate column: prompt length + buffer length + 1
    // For prompt format like "Filename to save to: %s", we need to find where
    // %s is
    int col = 1; // Start at column 1

    if (E.input_prompt) {
      // Calculate visible prompt length
      char prompt_formatted[256];
      snprintf(prompt_formatted, sizeof(prompt_formatted), E.input_prompt, "");
      col = strlen(prompt_formatted) + 1;
    }

    // Add buffer length for cursor position
    col += E.input_buffer_len;

    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
  } else {
    // Cursor in editor at normal position
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
             (E.rx - E.coloff) + editorGetLineNumberWidth() + 1);
  }

  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, SHOW_CURSOR, SHOW_CURSOR_SZ);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/

char *editorPrompt(char *prompt) {
  // Enter input mode and use the global input buffer
  E.input_mode = 1;
  E.input_prompt = prompt;
  E.input_buffer_len = 0;
  E.input_buffer[0] = '\0';
  write(STDOUT_FILENO, CURSOR_BAR_BLINK, CURSOR_BAR_BLINK_SZ);

  while (1) {
    editorSetStatusMessage(prompt, E.input_buffer);
    editorRefreshScreen();

    int c = editorReadKey();

    if (c == CTRL_KEY('c') || c == KEY_ESC) {
      editorSetStatusMessage("");
      E.input_mode = 0;
      E.input_prompt = NULL;
      E.input_buffer_len = 0;
      return NULL;
    }

    if (c == KEY_BACKSPACE) {
      if (E.input_buffer_len > 0) {
        E.input_buffer[--E.input_buffer_len] = '\0';
      }
      continue;
    }

    if (c == '\r') {
      if (E.input_buffer_len > 0) {
        editorSetStatusMessage("");
        E.input_mode = 0;
        E.input_prompt = NULL;
        E.input_buffer_len = 0;
        // Return a copy of the buffer
        return strdup(E.input_buffer);
      }
    }

    if (!iscntrl(c) && c < 128) { // Is printable character
      // Expand buffer if needed
      if (E.input_buffer_len >= E.input_buffer_size - 1) {
        E.input_buffer_size *= 2;
        E.input_buffer = drealloc(E.input_buffer, E.input_buffer_size);
      }
      E.input_buffer[E.input_buffer_len++] = c;
      E.input_buffer[E.input_buffer_len] = '\0';
    }
  }
}

void editorChangeMode(enum editorMode mode) {
  enum editorMode old_mode = E.mode;
  E.mode = mode;

  // Clean up message bar if exiting command mode
  if (old_mode == COMMAND_MODE && mode != COMMAND_MODE) {
    E.input_mode = 0;
    E.input_prompt = NULL;
    E.input_buffer_len = 0;
  }

  // Change cursor style based on mode
  if (mode == INSERT_MODE) {
    write(STDOUT_FILENO, CURSOR_BAR_BLINK, CURSOR_BAR_BLINK_SZ);
  } else if (mode == COMMAND_MODE) {
    // Entering command mode - set up input state
    E.input_mode = 1;
    E.input_prompt = ":";
    E.input_buffer_len = 0;
    E.input_buffer[0] = '\0';
    write(STDOUT_FILENO, CURSOR_BAR_BLINK, CURSOR_BAR_BLINK_SZ);
    editorSetStatusMessage(":%s", E.input_buffer);
  } else {
    write(STDOUT_FILENO, CURSOR_BLOCK, CURSOR_BLOCK_SZ);
  }
}

void editorMoveCursor(int key) {
  // Current row can be a valid one or the first "empty" line at the end
  Row *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
  case KEY_0:
    E.cx = 0;
    break;

  case ARROW_DOWN:
  case KEY_j:
    if (E.cy < E.numrows - 1) {
      E.cy++;
    }
    break;

  case ARROW_UP:
  case KEY_k:
    if (E.cy > 0) {
      E.cy--;
    }
    break;

  case ARROW_LEFT:
  case KEY_h:
    if (E.cx > 0) {
      E.cx--;
    }
    break;

  case ARROW_RIGHT:
  case KEY_l:
    // Limit right scrolling
    if (row && E.cx < row->size) {
      E.cx++;
    }
    break;

  // Go to first printable character in the current line
  case KEY_I: {
    int p;
    for (p = 0; p < row->size && isspace(row->chars[p]); p++)
      ;
    E.cx = p;
    break;
  }

  // Move to the end of next word
  case KEY_e: {

    // 0. EOL -> exit
    if (E.cx >= row->size - 1)
      break;

    // 1. On a space -> go to the end of it (we could be already there and next
    // char is start of a word) and then to the end of word
    while (E.cx < row->size - 1 &&
           getCharFamily(row->chars[E.cx]) == CHAR_FAMILY_SPACES) {
      E.cx++;
    }

    if (E.cx >= row->size - 2) {
      break;
    }

    // 2. At the end of a word already (current char != next char) -> go to next
    // char (check boundary) and then skip spaces and go to the end of next word
    int family = getCharFamily(row->chars[E.cx]);
    int nextfamily = getCharFamily(row->chars[E.cx + 1]);

    if (nextfamily != family) {
      // Go to next word, whatever it is
      E.cx++;
      // Go to the end of it, or EOL
      while (E.cx + 1 < row->size &&
             getCharFamily(row->chars[E.cx + 1]) == nextfamily) {
        E.cx++;
      }
      // If it was spaces, continue
      if (nextfamily == CHAR_FAMILY_SPACES) {
        if (E.cx + 1 >= row->size)
          break;
        nextfamily = getCharFamily(row->chars[E.cx + 1]);
        while (E.cx + 1 < row->size &&
               getCharFamily(row->chars[E.cx + 1]) == CHAR_FAMILY_SPACES) {

          E.cx++;
        }
      }
      break;
    }

    // 3. Inside a word -> go to the end of it
    while (E.cx < row->size - 2 &&
           getCharFamily(row->chars[E.cx + 1]) == family) {
      E.cx++;
    }

    // Fix inconsistencies
    if (E.cx >= row->size)
      E.cx = row->size - 1;

    break;
  }

  // Move to the start of next word
  case KEY_w: {
    int p;
    int old = getCharFamily(row->chars[E.cx]);

    for (p = E.cx + 1; p < row->size; p++) {
      char c = (row->chars[p]);
      int new = getCharFamily(c);
      if (old != new && new != CHAR_FAMILY_SPACES) {
        E.cx = p;
        break;
      }
      old = new;
    }
    break;
  }

  // Full right
  case KEY_L:
    // TODO: Will need to move to the file line end, not the editor line end
    E.cx = MAX(0, E.row[E.cy].size - 1);
    break;
  // Full left
  case KEY_H:
    E.cx = 0;
    break;
  // Fast down
  case KEY_J:
    E.cy = MIN(E.cy + 5, E.numrows - 1);
    break;
  // Fast up
  case KEY_K:
    E.cy = MAX(E.cy - 5, 0);
    break;

  // TODO: Need to move to top document, not just first editor row
  // Go top of doc
  case CMD_GO_TOP_DOC:
    E.cy = 0;
    break;
  // Go bottom of doc
  case CMD_GO_BOTTOM_DOC:
    E.cy = E.numrows - 1;
    break;
  }

  // New row after the movement
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  // Avoid ending up in an invalid x-position through vertical movements
  // across lines with different size
  if (E.cx > rowlen)
    E.cx = rowlen;
}

void destroyEditor(void) { dlog_close(E.logger); }

void editorProcessKeypressNormalMode(int c) {
  static int quit_times = DITTO_QUIT_TIMES;

  int cc = 0;

  switch (c) {
  case CTRL_KEY('c'):
    if (E.dirty && quit_times > 1) {
      editorSetStatusMessage("Unsaved changes. Press Ctrl-C again to quit.");
      quit_times--;
      return;
    }
    write(STDOUT_FILENO, CLEAR_SCREEN, CLEAR_SCREEN_SZ);
    write(STDOUT_FILENO, REPOS_CURSOR, REPOS_CURSOR_SZ);
    exit(0);
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case KEY_ESC:
    editorChangeMode(NORMAL_MODE);
    break;
  case KEY_i:
    editorChangeMode(INSERT_MODE);
    break;
  case KEY_v:
    editorChangeMode(VISUAL_MODE);
    break;
  case KEY_COLON:
    editorChangeMode(COMMAND_MODE);
    break;
  case ARROW_UP:
  case ARROW_LEFT:
  case ARROW_RIGHT:
  case ARROW_DOWN:
  case KEY_j:
  case KEY_k:
  case KEY_h:
  case KEY_l:
  case KEY_J:
  case KEY_K:
  case KEY_H:
  case KEY_L:
    editorMoveCursor(c);
    break;

  case KEY_0:
    editorMoveCursor(KEY_0);
    break;

  case KEY_a:
    editorMoveCursor(ARROW_RIGHT);
    editorChangeMode(INSERT_MODE);
    break;

  case KEY_A:
    editorMoveCursor(KEY_L);
    editorMoveCursor(ARROW_RIGHT);
    editorChangeMode(INSERT_MODE);
    break;

  case KEY_e:
    editorMoveCursor(KEY_e);
    break;

  case KEY_w:
    editorMoveCursor(KEY_w);
    break;

  case KEY_o:
    editorInsertRow(E.cy + 1, "", 0);
    editorMoveCursor(ARROW_DOWN);
    editorChangeMode(INSERT_MODE);
    break;

  case KEY_I:
    editorMoveCursor(KEY_I);
    editorChangeMode(INSERT_MODE);
    break;

  case KEY_O:
    editorInsertRow(E.cy, "", 0);
    editorMoveCursor(KEY_0);
    editorChangeMode(INSERT_MODE);
    break;

  case KEY_x:
  case KEY_X:
    if (c == KEY_x)
      editorMoveCursor(ARROW_RIGHT);
    editorDeleteChar();
    break;

  case KEY_y:
    // Sleep a bit to allow the possible sequence to be read
    usleep(SEQUENCES_TIMEOUT_MICROSEC);
    cc = editorReadKey();
    switch (cc) {
    case KEY_y:
      dfree(E.reg);
      E.reg = E.row[E.cy].chars;
      editorSetStatusMessage("Yanked %d lines", 1);
      break;
    default:
      dlog_debug(E.logger, "no sequence for '%c%c'", c, cc);
      break;
    }
    break;

  case KEY_p:
    editorInsertRow(E.cy + 1, E.reg, strlen(E.reg));
    break;
  case KEY_P:
    editorInsertRow(E.cy, E.reg, strlen(E.reg));
    editorMoveCursor(E.cy - 1);
    break;

  case KEY_d:
    // Sleep a bit to allow the possible sequence to be read
    usleep(SEQUENCES_TIMEOUT_MICROSEC);
    cc = editorReadKey();
    switch (cc) {
    case KEY_d:
      editorDeleteRow(E.cy);
      break;
    default:
      dlog_debug(E.logger, "no sequence for '%c%c'", c, cc);
      break;
    }
    break;

  case KEY_G:
    editorMoveCursor(CMD_GO_BOTTOM_DOC);
    break;

  case KEY_g:
    // Sleep a bit to allow the possible sequence to be read
    usleep(SEQUENCES_TIMEOUT_MICROSEC);
    cc = editorReadKey();
    switch (cc) {
    case KEY_g:
      editorMoveCursor(CMD_GO_TOP_DOC);
      break;
    default:
      dlog_debug(E.logger, "no sequence for '%c%c'", c, cc);
      break;
    }
    break;
  }

  quit_times = DITTO_QUIT_TIMES;
}

void editorProcessKeypressInsertMode(int c) {
  switch (c) {
  case KEY_ESC:
    editorChangeMode(NORMAL_MODE);
    break;
  case '\r':
    editorInsertNewline();
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case KEY_BACKSPACE:
    editorDeleteChar();
    break;
  case CTRL_KEY('l'):
    // case '\x1b':
    break;
  case ARROW_UP:
  case ARROW_LEFT:
  case ARROW_RIGHT:
  case ARROW_DOWN:
    editorMoveCursor(c);
    break;
  default:
    // Only insert printable characters (ASCII 32-126)
    // 9 is tab
    // 32 is space
    if (c == KEY_TAB || (c >= 32 && c <= 126)) {
      editorInsertChar(c);
    }
    break;
  }
}

void editorProcessKeypressVisualMode(int c) {
  switch (c) {
  case KEY_ESC:
    editorChangeMode(NORMAL_MODE);
    break;
  default:
    editorInsertChar(c);
    break;
  }
}

void editorProcessKeypressCommandMode(int c) {
  switch (c) {
  case KEY_ESC:
    editorSetStatusMessage("");
    editorChangeMode(NORMAL_MODE);
    break;

  case KEY_BACKSPACE:
    if (E.input_buffer_len > 0) {
      E.input_buffer[--E.input_buffer_len] = '\0';
      editorSetStatusMessage(":%s", E.input_buffer);
    }
    break;

  case '\r':
    // TODO: Execute the command
    editorSetStatusMessage("Command not implemented: %s", E.input_buffer);
    editorChangeMode(NORMAL_MODE);
    break;

  default:
    // Only insert printable characters
    if (!iscntrl(c) && c < 128) {
      // Expand buffer if needed
      if (E.input_buffer_len >= E.input_buffer_size - 1) {
        E.input_buffer_size *= 2;
        E.input_buffer = drealloc(E.input_buffer, E.input_buffer_size);
      }
      E.input_buffer[E.input_buffer_len++] = c;
      E.input_buffer[E.input_buffer_len] = '\0';
      editorSetStatusMessage(":%s", E.input_buffer);
    }
    break;
  }
}

void editorProcessKeypress(void) {
  int c = editorReadKey();
  // dlog_debug(E.logger, "Pressed '%c' (%d)", c, c);

  switch (E.mode) {
  case NORMAL_MODE:
    editorProcessKeypressNormalMode(c);
    break;
  case INSERT_MODE:
    editorProcessKeypressInsertMode(c);
    break;
  case VISUAL_MODE:
    editorProcessKeypressVisualMode(c);
    break;
  case COMMAND_MODE:
    editorProcessKeypressCommandMode(c);
    break;
  default:
    editorSetStatusMessage("%s mode not handled yet", mode_str[E.mode]);
    break;
  }
}

/*** init ***/

void initEditor(DLogger *l) {
  E.logger = l;
  E.cx = 0;
  E.rx = 0;
  E.cy = 0;
  E.ry = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.mode = NORMAL_MODE;
  E.screen_resized = 0;
  E.input_mode = 0;
  E.input_prompt = NULL;
  E.input_buffer = dmalloc(128);
  E.input_buffer_size = 128;
  E.input_buffer_len = 0;
  E.input_buffer[0] = '\0';

  E.messages = fss_create(10);

  enableRawMode();

  // Set up signal handler for window resize
  signal(SIGWINCH, handleResize);

  dlog_info(E.logger, "Welcome to Ditto Editor %s!", DITTO_VERSION);

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");

  dlog_debug(E.logger, "Screen size: %d x %d", E.screenrows, E.screencols);

  // Make space for the line numbers
  E.screencols -= editorGetLineNumberWidth();

  // Make space for status bar and status message
  E.screenrows -= 2;

  atexit(destroyEditor);
}

/*** main ***/

int main(int argc, char *argv[]) {
  FILE *f = fopen("/tmp/dittolog.txt", "a+");
  if (f == NULL)
    die("fopen");
  DLogger *l = dlog_initf(f, DLOG_LEVEL_DEBUG);

  initEditor(l);

  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("Ctrl-C to quit. Ctrl-S to save.");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}

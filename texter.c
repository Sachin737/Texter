/*----- includes -----*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/*----- global data -----*/

struct editorConfig {
    struct termios orig_termios; // terminal attributes (basically terminal settings' attr)can be read in termios struct
    int screenRows;
    int screenCols;
    int cx, cy; // curX, curY positions
};

struct editorConfig Ed;

/*----- defines -----*/

#define CTRL_KEY(k) ((k) & 0x1f) // Ctrl key actually do this only! 
#define AP_BUF_INIT {NULL, 0}
#define TEXTER_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 100001,
    ARROW_RIGHT = 100002,
    ARROW_UP = 100003,
    ARROW_DOWN = 100004
};


/*----- terminal functions -----*/

void die(const char* s){
    // to adjust cursor position and screen clear when program stops
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    
    // every C lib func has errno, perror looks for that and print message given at that no. index.
    // can also print string s.
    perror(s);
    exit(1); // return 1 means failure
}

void disableRawMode() {
    // to apply terminal setting for standard input (STDIN_FILENO) to original state.
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &Ed.orig_termios) == -1){
    die("tcsetattr");
  } 
}

void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &Ed.orig_termios) == -1){
        die("tcgetattr");
    } // read current terminal attr 
    
    atexit(disableRawMode);

    struct termios raw = Ed.orig_termios;

    // ECHO: Disables echoing of typed characters, meaning the 
    // characters won't be displayed on the terminal.
    
    // ICANON: Disables canonical mode, allowing input to be 
    // processed character-by-character instead of line-by-line.

    // ISIG: It is responsible for storing ctrl+C / ctrl+Z signals which terminates the
    // processes. So now, our terminal attr. is changed so that it wont get terminate
    // on this key presses.

    // IXON: XOFF/XON data tranmission. It keep track of whether to send or not data between two 
    // devices. for our case, its terminal. It is done by pressin ctrl+S / ctrl+Q

    // IEXTEN: Disabling ctrl+V, which previously, On some systems, when you type Ctrl-V, the 
    // terminal waits for you to type another character and then sends that character literally.

    // ICRNL: It fix Ctrl+M, actually by default terminal transform its ascii code (13,'\r') to (10,'\n') 
    // which perform same function as Enter button, so we need to fix it. 

    // OPOST: Turn off all output processing -> By default '\n' is translated to '\r\n' but we will
    // remove this. ['\r': it move cursor to beginning of current line]
    // ''''''from now on, we’ll have to write out the full "\r\n" whenever we want to start a new line.''''''''

    raw.c_iflag &= ~ (ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); 
    raw.c_cflag |= (CS8);

    raw.c_cc[VMIN] = 0; // minimum #bits needed before read() can return
    raw.c_cc[VTIME] = 50; // total input time window before read() return 0 (in 1/10 th of seconds).

    // to apply terminal setting for standard input (STDIN_FILENO) to content of raw
    // TCSAFLUSH: this option flushes any ip/op present and immediately apply new terminal attr!
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1){
        die("tcsetattr");
    } 
}

int editorReadKey(){
    int readn;
    char c;
    while((readn = read(STDERR_FILENO, &c, 1)) != 1){
        if (readn == -1 && errno != EAGAIN){
            die("read");
        }
    }

    /*
        reading escape seq: '\x1b[?'
        this type of escape seq is send to our program 
        when we press arrow keys. so we are just mapping
        it to w,s,a,d
    */

    if(c == '\x1b'){
        char s[3];

        // if we have input size < 3: return '\x1b'
        if (read(STDIN_FILENO, &s[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &s[1], 1) != 1) return '\x1b';

        if (s[0] == '[') {
            switch (s[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    }else{
        return c;

    }

}

int getCursorPosition(int *rows, int *cols){
    // The n command can be used to query the terminal for status information (arg 6 used to return window size (status)).
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    

    char buf[32];
    unsigned int i = 0;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    buf[i] = '\0'; // null terminator : end of string
    
    // parsing row, cols
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    
    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws; // Declare a variable of type struct winsize to hold window size information.
    
    /*
        Use ioctl to get the window size
        The ioctl() system call manipulates the underlying device
        parameters of special files.  In particular, many operating
        characteristics of character special files (e.g., terminals) may
        be controlled with ioctl() operations.

        TIOCGWINSZ: tells ioctl system call to get window size.
    */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /*
            ioctl may not work in some systems! so we are implementing this fallback method of getting W and H.
            Here, we use two escape sequences..
            C is used to move cursor to the right
            B is used to move cursor to the bottom
            we give 999 as args, but these functions insure that cursor dont move out of the window.
        */
        
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;   
        return getCursorPosition(rows, cols);
    } else {
        *rows = ws.ws_row; 
        *cols = ws.ws_col; 

        return 0; // success.
    }
}

/*-----  append buffer  -----*/

struct AP_buf {
    char *buf;
    int len;
};

void AP_append(struct AP_buf *b, char *s, int len){
    char *temp = realloc(b->buf, b->len+len); // temp has larger mem block with initial data same as b->buf.
    if (temp==NULL) return;

    memcpy(&temp[b->len],s,len); // now taking s to back of temp string.

    b->buf = temp;
    b->len = b->len + len; 
}

void AP_free(struct AP_buf *b) {
    free(b->buf);
}

/*----- input processing -----*/

void editorMoveCursor(int key) {
  switch (key) {
    case ARROW_UP:
      Ed.cy--;
      break;
    case ARROW_LEFT:
      Ed.cx--;
      break;
    case ARROW_DOWN:
      Ed.cy++;
      break;
    case ARROW_RIGHT:
      Ed.cx++;
      break;
  }
}


void editorProcessKey(){
    int c = editorReadKey();

    // Processing key presses
    switch (c){
        case CTRL_KEY('q'):
            // to adjust cursor position and screen clear when program stops
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);    

            exit(0);
            break;
        
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*----- output processing ----- */

void editorDrawRows(struct AP_buf *b) {
  int y;
  for (y = 0; y < Ed.screenRows; y++) {
    if (y == Ed.screenRows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
        "TEXTER -- version %s", TEXTER_VERSION);
      if (welcomelen > Ed.screenCols) welcomelen = Ed.screenCols;

      int padding = (Ed.screenCols - welcomelen) / 2;
      if (padding) {
        AP_append(b, "~", 1);
        padding--;
      }
      while (padding--) AP_append(b, " ", 1);


      AP_append(b, welcome, welcomelen);
    } else {
      AP_append(b, "~", 1);
    }
    AP_append(b, "\x1b[K", 3);
    if (y < Ed.screenRows - 1) {
      AP_append(b, "\r\n", 2);
    }
  }
}

void editorRefreshScreen(){
    /* 
        Escape sequences are used to put nonprintable characters in character and string literals
        such as backspace, tab, color, cursor locations etc.
    */

    struct AP_buf b = AP_BUF_INIT;
    AP_append(&b, "\x1b[?25l", 6); // disable pointer

    // this will adjust the cursor position to top-left
    // read here about '[H': https://vt100.net/docs/vt100-ug/chapter3.html#CUP
    AP_append(&b, "\x1b[H", 3);

    editorDrawRows(&b);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", Ed.cy + 1, Ed.cx + 1);
    AP_append(&b, buf, strlen(buf));

    AP_append(&b, "\x1b[?25h", 6); // enable pointer

    write(STDOUT_FILENO, b.buf, b.len); //finally writing buffer to stdout
    
    AP_free(&b);
}


/*----- main -----*/

void initEditor() {
    Ed.cx = 0;
    Ed.cy = 0;
  if (getWindowSize(&Ed.screenRows, &Ed.screenCols) == -1) {
    die("getWindowSize");
  }
}

int main(){
    /*
        In raw mode, each character is processed immediately as it's typed, 
        allowing for real-time interaction without waiting for Enter. 
        In contrast, canonical mode allows users to edit their input 
        (e.g., using backspace) until they press Enter to submit the line. 
    */
    enableRawMode();
    initEditor();
    
    while (1){
        editorRefreshScreen();
        editorProcessKey();
        // Ed.cx++;
    }

    return 0;
}

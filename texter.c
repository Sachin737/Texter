/*----- includes -----*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>


/*----- global data -----*/

struct editorConfig {
    struct termios orig_termios; // terminal attributes (basically terminal settings' attr)can be read in termios struct
    int screenRows;
    int screenCols;
};

struct editorConfig Ed;

/*----- re-definition-----*/

#define CTRL_KEY(k) ((k) & 0x1f) // Ctrl key actually do this only! 

/*----- terminal functions -----*/

void die(const char* s){
    // to adjust cursor position and screen clear when program stops
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[1;1H", 3);
    
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

char editorReadKey(){
    int readn;
    char c;
    while((readn = read(STDERR_FILENO, &c, 1)) != 1){
        if (readn == -1 && errno != EAGAIN){
            die("read");
        }
    }
    return c;
}

int getCursorPosition(int *rows, int *cols){
    // The n command can be used to query the terminal for status information (arg 6 return window size).
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

/*----- input processing -----*/

void editorProcessKey(){
    char c = editorReadKey();

    // Processing key presses
    switch (c){
        case CTRL_KEY('q'):
            // to adjust cursor position and screen clear when program stops
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[1;1H", 3);    

            exit(0);
            break;
    }

}

/*----- output processing ----- */

void editorDrawRows(){
    for(int i=0;i<Ed.screenRows;i++){
        if(i < Ed.screenRows - 1){
            write(STDOUT_FILENO, "~\r\n", 3);
        }else{
            write(STDOUT_FILENO, "~", 1);
        }
    }
}

void editorRefreshScreen(){
    // Escape sequences are used to put nonprintable characters in character and string literals
    // such as backspace, tab, color, cursor locations etc.

    // '\x1b is escape character, and read about '[2J' here:
    // https://vt100.net/docs/vt100-ug/chapter3.html#ED
    write(STDOUT_FILENO, "\x1b[2J", 4);  

    // this will adjust the cursor position to top-left
    // read here about '[H': https://vt100.net/docs/vt100-ug/chapter3.html#CUP
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}


/*----- main -----*/

void initEditor() {
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
    }

    return 0;
}

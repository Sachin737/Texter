/*----- includes -----*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>

/*----- global data -----*/
struct termios orig_termios; // terminal attributes (basically terminal settings' attr)can be read in termios struct


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
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1){
    die("tcsetattr");
  } 
}

void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1){
        die("tcgetattr");
    } // read current terminal attr 
    
    atexit(disableRawMode);

    struct termios raw = orig_termios;

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


/*----- input processing -----*/

void editorProcessKey(){
    char c = editorReadKey();

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

void editorRefreshScreen(){
    // Escape sequences are used to put nonprintable characters in character and string literals
    // such as backspace, tab, color, cursor locations etc.

    // '\x1b is escape character, and read about '[2J' here:
    // https://vt100.net/docs/vt100-ug/chapter3.html#ED
    write(STDOUT_FILENO, "\x1b[2J", 4);

    // this will adjust the cursor position to top-left
    // read here about '[H': https://vt100.net/docs/vt100-ug/chapter3.html#CUP
    write(STDOUT_FILENO, "\x1b[1;1H", 3);
}

/*----- main -----*/

int main(){
    // In raw mode, each character is processed immediately as it's typed, 
    // allowing for real-time interaction without waiting for Enter. 
    // In contrast, canonical mode allows users to edit their input 
    // (e.g., using backspace) until they press Enter to submit the line. 
    enableRawMode();
    
    while (1){
        editorRefreshScreen();
        editorProcessKey();
    }

    return 0;
}

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>


struct termios orig_termios; // terminal attributes (basically terminal settings' attr)can be read in termios struct

void disableRawMode() {
    // to apply terminal setting for standard input (STDIN_FILENO) to original state.
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); 
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios); // read current terminal attr 
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

    // to apply terminal setting for standard input (STDIN_FILENO) to content of raw
    // TCSAFLUSH: this option flushes any ip/op present and immediately apply new terminal attr!
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); 
}


int main(){
    // In raw mode, each character is processed immediately as it's typed, 
    // allowing for real-time interaction without waiting for Enter. 
    // In contrast, canonical mode allows users to edit their input 
    // (e.g., using backspace) until they press Enter to submit the line. 
    enableRawMode();
    
    char c;
    
    while (1){
        char c = '\0';
        read(STDIN_FILENO, &c, 1);

        // it checks for control chars.
        // Control characters are nonprintable characters.
        if(iscntrl(c)){ 
            printf("%d\r\n", c);
        }else{
            printf("%d (%c)\r\n", c, c);
        }
        
        if(c != 'q'){
            break;
        }
    }

    return 0;
}

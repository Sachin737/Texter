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
    raw.c_lflag &= ~(ECHO | ICANON); 

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
    
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q'){
        // it checks for control chars.
        // Control characters are nonprintable characters.
        if(iscntrl(c)){ 
            printf("%d\n", c);
        }else{
            printf("%d (%c)\n", c, c);
        }
    }

    return 0;
}

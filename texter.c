/* ----- includes ----- */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/* ----- prototypes ----- */
char *editorPrompt(char* prompt,void (*callback)(char*,int));
void editorSetStatusMessage(char *s, ...);

/* ----- global data ----- */

typedef struct erow
{
    int size;
    char *data;
    int rSize;
    char *renderData;

    int lineNo;
} erow;
 

struct editorConfig {
    struct termios orig_termios; // terminal attributes (basically terminal settings' attr)can be read in termios struct
    int screenRows;
    int screenCols;
    int cx, cy; // curX, curY positions
    int rx; // curX position in renderData string

    // stores file data
    int numRows;
    erow *row;
    char *filename; 

    int scrollYOffset; // vertical scroll offset
    int scrollXOffset; // horizontal scroll offset

    // status msg
    char statusmsg[80];
    time_t statusmsg_time;

    // to keep track of unsaved changes
    int dirty;

    // to keep track of selected text
    int selected;
    int sx,sy,ex,ey; // cords of selection
    char* selectedData;
    int selectedDataLen;
    
    int copied;
    char* copiedData;
    int copiedDataLen;
};

struct editorConfig Ed;

/* ----- defines ----- */

#define CTRL_KEY(k) ((k) & 0x1f)  // 11111
#define ab_BUF_INIT {NULL, 0}
#define TEXTER_VERSION "0.0.1"
#define TAB_SIZE 7
#define TEXTER_QUIT_CONFIRM 2
#define LINENO_BAR_WIDTH 4
#define STATUS_DISPLAY_TIME 1

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 10001,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY,
    SHIFT_ARROW_UP,
    SHIFT_ARROW_DOWN,
    SHIFT_ARROW_RIGHT,
    SHIFT_ARROW_LEFT,
};

/* ----- DEBUG functions ----- */

void debugLog(const char *format, ...) {
    FILE *logFile = fopen("debug_log.txt", "a"); // Append mode to keep previous logs
    if (logFile == NULL) {
        return; // If the file can't be opened, exit the function
    }

    va_list args;
    va_start(args, format);
    vfprintf(logFile, format, args);
    va_end(args);

    fprintf(logFile, "\n"); // Add a new line after each log
    fclose(logFile);
}


/* ----- terminal functions ----- */

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

int editorReadKey() {
    int readn;
    char c;

    // Read a character from stdin
    while ((readn = read(STDIN_FILENO, &c, 1)) != 1) {
        if (readn == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    // Handle escape sequences
    if (c == '\x1b') {
        char seq[3];

        // Try reading the next two characters to detect escape sequences
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        // Handling escape sequences for special keys

        if (seq[0] == '[') {
    
            if (seq[1] >= '0' && seq[1] <= '9') {
                // Read the next part of the escape sequence
                char seq2[3];
                if (read(STDIN_FILENO, &seq2[0], 1) != 1) return '\x1b';
                    
                
                // Check if it's a Shift+Arrow key
                if (seq2[0] == ';') {
                    if (read(STDIN_FILENO, &seq2[1], 1) != 1) return '\x1b';
                

                    if (seq2[1] == '2') {  // "2" indicates Shift key
                        if (read(STDIN_FILENO, &seq2[2], 1) != 1) return '\x1b';

                            
                        // editorSetStatusMessage("%3s",seq2);

                        // Map SHIFT+Arrow keys
                        switch (seq2[2]) {
                            case 'A': return SHIFT_ARROW_UP;
                            case 'B': return SHIFT_ARROW_DOWN;
                            case 'C': return SHIFT_ARROW_RIGHT;
                            case 'D': return SHIFT_ARROW_LEFT;
                        }
                    }
                } else if (seq2[0] == '~') {
                    // Page up/down, home, end, and delete
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '4': return END_KEY;
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                // Handle simple arrow keys
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b'; // Return escape character if it's not a recognized sequence
    }

    return c;  // Return the character read if it's not an escape sequence
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

/* -----  append buffer  ----- */

struct ab_buf {
    char *buf;
    int len;
};

void ab_append(struct ab_buf *b, char *s, int len){
    char *temp = realloc(b->buf, b->len+len); // temp has larger mem block with initial data same as b->buf.
    if (temp==NULL) return;

    memcpy(&temp[b->len],s,len); // now taking s to back of temp string.

    b->buf = temp;
    b->len = b->len + len; 
}

void ab_free(struct ab_buf *b) {
    free(b->buf);
}

/* ----- row operations ----- */

int editorCxToRx(erow *line, int cx){
    int rx = 0;
    for(int i=0;i<cx;i++){
        if(line->data[i]=='\t'){
            // since all tabs dont take full TAB_SIZE,
            // we decrease that part.
            rx += TAB_SIZE - rx%(TAB_SIZE+1); 
        }
        rx++;
    }
    return rx+LINENO_BAR_WIDTH;
}

int editorRxToCx(erow *line, int rx){
    int cur_rx = LINENO_BAR_WIDTH;
    int i;
    for(i=0;i<line->size;i++){
        if(line->data[i]=='\t'){
            cur_rx += TAB_SIZE - rx%(TAB_SIZE+1); 
        }
        cur_rx++;
        if(cur_rx > rx) return i;
    }
    return i;
}


void editorUpdateRenderData(erow *line){
    free(line->renderData);

    int tabs = 0;
    for(int i=0;i<line->size;i++){
        if(line->data[i]=='\t')tabs++;
    }

    int len = line->size + tabs * TAB_SIZE + 1;
    line->renderData = malloc(len);
    int j = 0;
    
    for(int i=0;i<line->size;i++){
        if(line->data[i]=='\t'){
            int next_tab_stop = TAB_SIZE + 1;
            
            line->renderData[j++] = ' ';
            while(j % next_tab_stop != 0)  {
                line->renderData[j++] = ' ';
            }
        }else{
            line->renderData[j++] = line->data[i];
        }
    }
    line->renderData[j]='\0';
    line->rSize = j;
}

void editorInsertRow(char *line, size_t len, int idx){
    if(idx < 0 || idx>Ed.numRows) return;


    Ed.row = realloc(Ed.row, sizeof(erow) * (Ed.numRows + 1)); // allocating space for new line
    memmove(&Ed.row[idx + 1], &Ed.row[idx], sizeof(erow)*(Ed.numRows-idx));


    Ed.numRows+=1;
    Ed.row[idx].size = len;    
    
    Ed.row[idx].data = malloc(len + 1);
    memcpy(Ed.row[idx].data, line, len);
    Ed.row[idx].data[len]='\0';

    Ed.row[idx].rSize = 0;
    Ed.row[idx].renderData = NULL;

    editorUpdateRenderData(&Ed.row[idx]);
    Ed.dirty=1;

    // updating line number 
    for(int j=0;j<Ed.numRows;j++) Ed.row[j].lineNo=j+1;
}

void editorInsertCharToRow(erow *line, int idx, int c){
    int curLen = line->size;
    if(idx < 0 || idx > curLen) idx = curLen;

    line->data = realloc(line->data, curLen + 2);
    memmove(&line->data[idx+1], &line->data[idx], curLen-idx + 1);
    
    line->data[idx] = c;
    line->size++;
    editorUpdateRenderData(line); 
    Ed.dirty=1;
}

void editorAppendStringToRow(erow *row, char *s, size_t len) {
    row->data = realloc(row->data, row->size + len + 1);

    memcpy(&row->data[row->size], s, len);
    
    row->size += len;
    row->data[row->size] = '\0';
    editorUpdateRenderData(row);

    Ed.dirty=1;
}


void editorDeleteCharFromRow(erow* line,int idx){
    int curLen = line->size;
    if(idx < 0 || idx > curLen) return;
    memmove(&line->data[idx],&line->data[idx+1],line->size-idx);
    line->size--;
    Ed.dirty=1;

    editorUpdateRenderData(line);
}


void editorDeleteRow(int idx){
    if(idx < 0 || idx >= Ed.numRows) return;
    
    free(Ed.row[idx].data);

    // shift all rows after row[idx] by one line.
    memmove(&Ed.row[idx],&Ed.row[idx+1], sizeof(erow)*(Ed.numRows-idx-1));
    

    Ed.numRows--;
    Ed.dirty=1;

    // updating line number 
    for(int j=0;j<Ed.numRows;j++) Ed.row[j].lineNo=j+1;

    editorUpdateRenderData(&Ed.row[idx]);
}

/* ----- editing operations ----- */

void editorInsertChar(int c){
    if(Ed.cy==Ed.numRows){ // appending new empty line
        editorInsertRow("", 0, Ed.numRows);
    }
    editorInsertCharToRow(&Ed.row[Ed.cy], Ed.cx, c);
    Ed.cx++;
}

void editorDeleteChar(){
    if(Ed.cy==Ed.numRows) return;
    if(Ed.cx==0 && Ed.cy==0) return;

    if(Ed.cx>0){ // erase char just before cursor
        editorDeleteCharFromRow(&Ed.row[Ed.cy], Ed.cx - 1);
        Ed.cx--;
    }else{ // moving cursor to prev line's end and performing required action.
        Ed.cx = Ed.row[Ed.cy - 1].size;
        editorAppendStringToRow(&Ed.row[Ed.cy-1],Ed.row[Ed.cy].data,Ed.row[Ed.cy].size);
        editorDeleteRow(Ed.cy);
        Ed.cy--;
    }
}

void editorInsertNewLine(){
    if (Ed.cx == 0) { // pressing enter at line start
        editorInsertRow("", 0, Ed.cy);
    } else {
        erow *row = &Ed.row[Ed.cy];

        // split cur row and push right string to next row
        editorInsertRow(&row->data[Ed.cx], row->size - Ed.cx, Ed.cy + 1);

        row = &Ed.row[Ed.cy];
        row->size = Ed.cx;
        row->data[row->size] = '\0';
        editorUpdateRenderData(row);
    }

    Ed.cy++;
    Ed.cx = 0;
}

/* ----- output processing ----- */

void editorSetStatusMessage(char *s, ...){ // variadic func.
    /*
        vsnprintf : its used to store data in buffer, but when input
                    string have variable #args.
    */

    va_list args; // stores args list
    va_start(args, s); // starts iterating over arg list starting from s

    // internally it calls va_arg() to get current arg, and move
    // iterator to next argument.
    vsnprintf(Ed.statusmsg, sizeof(Ed.statusmsg), s, args);

    va_end(args); // end of interation

    Ed.statusmsg_time = time(NULL); // current time
}

void editorDrawStatusMessage(struct ab_buf *b){
    ab_append(b, "\x1b[K", 3);
    
    int len = strlen(Ed.statusmsg);

    if (len > Ed.screenCols) len = Ed.screenCols;
    
    if (len && time(NULL) - Ed.statusmsg_time < STATUS_DISPLAY_TIME){
        ab_append(b, Ed.statusmsg, len);
    }
}

void editorDrawStatusBar(struct ab_buf *b){
    ab_append(b, "\x1b[7m", 4); // negative image (here white bg)

    // printing file name
    char status[80], curStatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", Ed.filename, Ed.numRows, Ed.dirty ? "(modified)" : "");
    int rlen = snprintf(curStatus, sizeof(curStatus), "%d/%d - Cx:%d Rx:%d [sy:%d,sx:%d]->[ey:%d,ex:%d]", Ed.cy+1, Ed.numRows,Ed.cx, Ed.rx,  Ed.sy+1, Ed.sx+1, Ed.ey+1, Ed.ex+1);
     

    ab_append(b, status, len);

    while (len < Ed.screenCols) {
        if (Ed.screenCols - len == rlen) {
            ab_append(b, curStatus, rlen);
            break;
        } else {
            ab_append(b, " ", 1);
            len++;
        }
    }
    ab_append(b, "\x1b[m", 3);
    ab_append(b, "\r\n", 2);

}

void editorDrawLineNos(struct ab_buf *b, int id){
    ab_append(b, "\x1b[1m",  4); // bold
    ab_append(b, "\x1b[48;5;15m", 10); // bg
    ab_append(b, "\x1b[38;5;16m", 10); // text
    // ab_append(b, "\x1b[4m",  4); // underline

    char s[20]; 
    snprintf(s, sizeof(s), "%*d", LINENO_BAR_WIDTH, Ed.row[id].lineNo); 
    ab_append(b, s, LINENO_BAR_WIDTH); 
    
    ab_append(b, "\x1b[m", 3);
}

void editorDrawRows(struct ab_buf *b) {
    int y;

    for (y = 0; y < Ed.screenRows; y++) {
        int realY = y + Ed.scrollYOffset;

        if(realY >= Ed.numRows){
            
            // set colors for line no side bar.
            ab_append(b, "\x1b[1m",  4); // bold
            ab_append(b, "\x1b[48;5;15m", 10); // bg
            ab_append(b, "\x1b[38;5;16m", 10); // text

            // we only show wlcm msg when user open empty editor
            if (y == Ed.screenRows / 3 && Ed.numRows == 0) {

                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "TEXTER -- version %s", TEXTER_VERSION);
                if (welcomelen > Ed.screenCols) welcomelen = Ed.screenCols;

                
                int padding = (Ed.screenCols - welcomelen) / 2;
                if (padding) {
                    ab_append(b, "   ~", 4);
                    padding--;
                }

                ab_append(b, "\x1b[m", 3); // reset color setting
                while (padding--) ab_append(b, " ", 1);

                
                ab_append(b, welcome, welcomelen);
            } else { // printing tildes
                ab_append(b, "   ~", 4);
            }

            ab_append(b, "\x1b[m", 3);

            
        }else{
            editorDrawLineNos(b, realY);

            // setting screen bg color
            ab_append(b, "\x1b[48;5;17m", 10);

            int len = Ed.row[realY].rSize - Ed.scrollXOffset;

            // truncate data lines to screenCols
            if(len < 0) len = 0;
            if (len > Ed.screenCols) len = Ed.screenCols;

            char *c = &Ed.row[realY].renderData[Ed.scrollXOffset];

            // iterating char by char to detect digits
            for(int i=0;i<len;i++){
                if(isdigit(c[i])){  // coloring digits
                    ab_append(b, "\x1b[36m", 5);
                    ab_append(b, &c[i], 1);
                    ab_append(b, "\x1b[39m", 5);
                }else{
                    ab_append(b, &c[i], 1);
                }
            }
        }

        ab_append(b, "\x1b[K", 3);
        ab_append(b, "\r\n", 2);
    }

}

void editorScroll() {
    // calculating rx using cx
    Ed.rx = Ed.cx;
    if(Ed.cy < Ed.numRows) Ed.rx = editorCxToRx(&Ed.row[Ed.cy], Ed.cx);

    // when we try to move up, it decreases the offsetY, 
    // so that we can view code from that desired cursor pos.
    if (Ed.cy < Ed.scrollYOffset) {
        Ed.scrollYOffset = Ed.cy;
    }

    // same thing for scroll down, [to view content below when scrolling down]
    if (Ed.cy >= Ed.scrollYOffset + Ed.screenRows) {
        Ed.scrollYOffset = Ed.cy - Ed.screenRows + 1;
    }


    if (Ed.rx < Ed.scrollXOffset + LINENO_BAR_WIDTH) {
        Ed.scrollXOffset = Ed.rx-LINENO_BAR_WIDTH;
    }

    // same thing for scroll down, [to view content below when scrolling down]
    if (Ed.rx >= Ed.scrollXOffset + Ed.screenCols + LINENO_BAR_WIDTH) {
        Ed.scrollXOffset = Ed.rx - Ed.screenCols + 1 - + LINENO_BAR_WIDTH;
    }
}

/*
    WHY?? 
    If you try to scroll back up, you may notice the cursor isn’t being positioned properly.
    That is because E.cy no longer refers to the position of the cursor on the screen. 
    It refers to the position of the cursor within the text file. 
    To position the cursor on the screen, we now have to subtract E.rowoff from the value of E.cy.
*/

void editorRefreshScreen(){
    editorScroll();

    /* 
        Escape sequences are used to put nonprintable characters in character and string literals
        such as backspace, tab, color, cursor locations etc.
    */

    struct ab_buf b = ab_BUF_INIT;
    
    ab_append(&b, "\x1b[?25l", 6); // disable pointer

    // this will adjust the cursor position to top-left
    // read here about '[H': https://vt100.net/docs/vt100-ug/chapter3.html#CUP
    ab_append(&b, "\x1b[H", 3);


    // draw
    editorDrawRows(&b);
    editorDrawStatusBar(&b);
    editorDrawStatusMessage(&b);

    // to position the cursor according to our cursor pos variables.
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", Ed.cy - Ed.scrollYOffset + 1, Ed.rx - Ed.scrollXOffset + 1);
    ab_append(&b, buf, strlen(buf));

    ab_append(&b, "\x1b[?25h", 6); // enable pointer
    
    write(STDOUT_FILENO, b.buf, b.len); //finally writing buffer to stdout
    
    ab_free(&b);
}


/* ----- file-io ----- */

void editorOpenFile(char *filename){
    FILE *file_ptr;

    Ed.filename = strdup(filename);

    file_ptr = fopen(filename, "r");

    if(file_ptr==NULL)die("fopen");

    char *lineData = NULL;
    ssize_t linelen = 0;
    size_t linecap = 0; //stores current length of data stored in lineData.

    // linelen = ;
    

    while((linelen = getline(&lineData, &linecap, file_ptr)) != -1){
        // strip off the newline or carriage return at the end
        while(linelen > 0 && (lineData[linelen-1]=='\n' || lineData[linelen-1]=='\r')){
            linelen--;
        }

        // saving line to buffer
        editorInsertRow(lineData,linelen,Ed.numRows);
    }

    free(lineData);
    fclose(file_ptr);
    Ed.dirty=0; // because this fn calls editorAppend which 
                // make file status as modified but its not when we
                // newly open file.
}

char* editorFileDataToString(int *buflen){
    int totLen = 0;
    for(int i=0;i<Ed.numRows;i++){
        totLen += Ed.row[i].size+1; // 1 for newline char
    }
    
    *buflen = totLen;
    char *buf = malloc(totLen);
    char *ptr = buf;
    for(int i=0;i<Ed.numRows;i++){ 
        memcpy(ptr,Ed.row[i].data, Ed.row[i].size);
        ptr += Ed.row[i].size;

        *ptr = '\n';
        ptr++;
    }
    return buf;
}


/*  For Bash on Windows, you will have to press Escape 3 times 
    to get one Escape keypress to register in our program  
*/
void editorSaveFile(){
    if(Ed.filename==NULL){
        Ed.filename = editorPrompt("Save as: %s (ESC to cancel | Enter to save)",NULL);
        if (Ed.filename == NULL) {
            editorRefreshScreen();
            editorSetStatusMessage("Save aborted");
            return;
        }
    }
    
    int len;
    char* buf = editorFileDataToString(&len);

    /*  O_CREAT: create new file if doesn;t exist.
        0644: this give write permission to only owner.
        O_RDWR: to read and write both 
    */
    int fd = open(Ed.filename, O_CREAT | O_RDWR, 0644);

    if(fd!=-1){
        if(ftruncate(fd, len)!=-1){
            if(write(fd, buf, len)==len){
                close(fd);
                free(buf);
                editorSetStatusMessage("%d bytes written to disk", len);
                Ed.dirty = 0;

                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}   

/* ----- search ----- */

void editorFallBackSearch(char* query, int keyPress){
    static int last_match_y = -1;
    static int direction = 1; 
    
    if(keyPress=='\r'||keyPress=='\x1b'){
        last_match_y = -1;
        direction = 1;
        return;
    }else if(keyPress == ARROW_DOWN || keyPress == ARROW_RIGHT){
        direction = 1;
    }else if(keyPress == ARROW_UP || keyPress == ARROW_LEFT){
        direction = -1;
    }else{
        last_match_y = -1;
        direction = 1;
    }

    int current_y = last_match_y;

    for(int i=0;i<Ed.numRows;i++){
        current_y+=direction;
        if(current_y == -1) current_y = Ed.numRows - 1;
        else if(current_y == Ed.numRows) current_y = 0;

        erow* row = &Ed.row[current_y];
        char* ptr = strstr(row->renderData, query);
        // ptr to matched substr in row->renderData

        if(ptr){
            last_match_y = current_y;

            Ed.cy = current_y;
            Ed.cx = editorRxToCx(row,ptr - row->renderData);
            Ed.scrollYOffset=Ed.numRows; // to make screen scroll to matched line
            break;
        }
    }
}

void editorSearch(){
    // saving cursor position to restore it after escaping search
    int old_cx = Ed.cx;
    int old_cy = Ed.cy;
    int old_Xoffset = Ed.scrollXOffset;
    int old_Yoffset = Ed.scrollYOffset;


    // this fallback func will be called again and again after keypress
    // [look at editorPrompt func to understand why!]
    char* query = editorPrompt("Search: %s (Esc : cancel | arrows : nextSearch | Enter : end search)", editorFallBackSearch);
    if (query) {
        free(query);
    }else{  
        Ed.cx = old_cx;
        Ed.cy = old_cy;
        Ed.scrollXOffset = old_Xoffset;
        Ed.scrollYOffset = old_Yoffset;
    }
}


/* ----- input processing ----- */

void editorMoveCursor(int key) {

    // storing current row to avoid moving towards
    // right of the content in line without pressing space.
    erow *curRow = NULL;
    if(Ed.cy < Ed.numRows){ //cursor on some file's line
        curRow = &Ed.row[Ed.cy];
    }

    switch (key) {
        case ARROW_UP:
        case SHIFT_ARROW_UP:
            if(Ed.cy > 0){
                Ed.cy--;
            }
            break;

        case ARROW_LEFT:
        case SHIFT_ARROW_LEFT:
            if(Ed.cx > 0){
                Ed.cx--;
            }else if(Ed.cy > 0){
                Ed.cy--;
                Ed.cx = Ed.row[Ed.cy].size;
            }
            break;

        case ARROW_DOWN:
        case SHIFT_ARROW_DOWN:
            if(Ed.cy < Ed.numRows-1){ // to allow cursor to go till end of file instead just end of screen!
                Ed.cy++;
            }
            break;
            
        case ARROW_RIGHT:
        case SHIFT_ARROW_RIGHT:
            if(curRow){
                if(Ed.cx < curRow->size){
                    Ed.cx++;
                }else if(Ed.cx == curRow->size){
                    Ed.cy++;
                    Ed.cx = 0;
                }
            }else{
                // cant move right on empty line
            }
            break;
    }

    // But user can still move up or down with current x cords,
    // and if that new row is shorter in length, its an issue!

    if(Ed.cy < Ed.numRows){ //cursor on some file's line
        curRow = &Ed.row[Ed.cy];
    }else curRow = NULL;

    if(curRow && Ed.cx > curRow->size)Ed.cx = curRow->size;


}

void editorUpdateSelectionEndPoints() {
    /*
        Main idea:
        I keep Ed.sx,Ed.sy always as the point from where
        we started selecting text (with SHIFT+X).

        then from Ed.ex,Ed.ey, even if any one is less that start 
        cords, I take care of it while memmove data from 
        row data to -> selectedData buffer.
        how? by taking min of both wherever necessary!!
    */

    if (Ed.selected) {
        Ed.ex = Ed.cx;
        Ed.ey = Ed.cy; 

        int len = 0;
        if (Ed.sy == Ed.ey) { // copying within single line

            len = (Ed.ex>Ed.sx ? Ed.ex - Ed.sx: Ed.sx - Ed.ex) + 1;
            Ed.selectedDataLen = len;

            Ed.selectedData = malloc(len+1);
            memmove(Ed.selectedData, &Ed.row[Ed.sy].data[(Ed.ex<Ed.sx?Ed.ex:Ed.sx)], len);
            Ed.selectedData[len++] = '\0';

        } else { // copying multiple lines
            int len;

            if(Ed.ey != Ed.sy){
                len = Ed.row[(Ed.ey<Ed.sy?Ed.ey:Ed.sy)].size - (Ed.ey<Ed.sy?Ed.ex:Ed.sx);
                Ed.selectedData = malloc(len+1);
                
                memmove(Ed.selectedData, &Ed.row[(Ed.ey<Ed.sy?Ed.ey:Ed.sy)].data[(Ed.ey<Ed.sy?Ed.ex:Ed.sx)], len);
                Ed.selectedData[len++] = '\n';

            }else{
                len = (Ed.ex>Ed.sx ? Ed.ex - Ed.sx: Ed.sx - Ed.ex) + 1;
                Ed.selectedData = malloc(len+1);

                memmove(Ed.selectedData, &Ed.row[Ed.sy].data[(Ed.ex<Ed.sx?Ed.ex:Ed.sx)], len);                
                Ed.selectedData[len++] = '\0';
                    
            }

            // In between
            for (int i = (Ed.ey<Ed.sy?Ed.ey:Ed.sy) + 1; i < (Ed.ey<Ed.sy?Ed.sy:Ed.ey); i++) {
                int prevLen = len;
                len += Ed.row[i].size;
                
                Ed.selectedData = realloc(Ed.selectedData, len+1);
                memmove(Ed.selectedData + prevLen, Ed.row[i].data, Ed.row[i].size);
                
                Ed.selectedData[len++] = '\n';

                
            }

            // Last line
            if(Ed.ey != Ed.sy){
                len += ((Ed.ey<Ed.sy?Ed.sx:Ed.ex) + 1);
                Ed.selectedData = realloc(Ed.selectedData, len+1);
                memmove(Ed.selectedData + len - ((Ed.ey<Ed.sy?Ed.sx:Ed.ex) + 1), Ed.row[(Ed.ey<Ed.sy?Ed.sy:Ed.ey)].data, (Ed.ey<Ed.sy?Ed.sx:Ed.ex) + 1);
            
                Ed.selectedData[len++] = '\0';
                Ed.selectedDataLen = len;

            }
        }
    }
}


void editorProcessKey(){
    int c = editorReadKey();
    static int quit_cntr = TEXTER_QUIT_CONFIRM;
    
    // handle selected data ends
    if(!Ed.selected && (c==SHIFT_ARROW_DOWN||c==SHIFT_ARROW_UP||c==SHIFT_ARROW_LEFT||c==SHIFT_ARROW_RIGHT)){
        // we started shift+x operation 
        Ed.selected=1;
        Ed.sx = Ed.cx;
        Ed.sy = Ed.cy;
        Ed.ex = Ed.cx;
        Ed.ey = Ed.cy;
    }

    // Processing keyboard button
    switch (c){
        case CTRL_KEY('f'):
            editorSearch();
            break;
             
        case CTRL_KEY('s'):
            editorSaveFile();
            break;

        case '\r': //enter key
            editorInsertNewLine();
            break;
        
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            // because del key delete current char at cursor pos.
            if(c==DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDeleteChar();
            break;
        
        case CTRL_KEY('l'): // to refresh screen
        case '\x1b': //escape
            // ignoring!
            break;

        case CTRL_KEY('q'):

            debugLog("selected Data:\r\n%s", Ed.copiedData);

            if(Ed.dirty && quit_cntr){
                editorSetStatusMessage("WARNING!!! File has unsaved changes."
                "Press Ctrl-Q %d more times to quit.", quit_cntr);   
                quit_cntr--;
                return;
            }

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
        
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int total = Ed.screenRows;
                while (total--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        
        case HOME_KEY:
            Ed.cx = 0;
            break;
        case END_KEY:
            if (Ed.cy < Ed.numRows)
                Ed.cx = Ed.row[Ed.cy].size;
            break;
        
        case CTRL_KEY('c'):
            if(Ed.selected){ // selected text copied with ctrl+c
                Ed.copied = 1;
            }else{
                // selected Data is NULL
                Ed.copied = 0;
            }
            break;

        case SHIFT_ARROW_UP:
        case SHIFT_ARROW_DOWN:
        case SHIFT_ARROW_RIGHT:
        case SHIFT_ARROW_LEFT:
            editorMoveCursor(c);
            editorUpdateSelectionEndPoints();
            break;

        default:
            editorInsertChar(c);
            break;
    }

    // handle select data
    if(Ed.selected && c!=SHIFT_ARROW_DOWN &&  c!=SHIFT_ARROW_UP &&  c!=SHIFT_ARROW_LEFT &&  c!=SHIFT_ARROW_RIGHT){
        // we left shift+x selection process
        
        if(Ed.copied){ // if copied, store it
        
            Ed.copiedData = malloc(Ed.selectedDataLen+1);
            memmove(Ed.copiedData, Ed.selectedData, Ed.selectedDataLen);
            Ed.copiedDataLen = Ed.selectedDataLen;
            Ed.copiedData[Ed.copiedDataLen++]='\0';
        }

        Ed.selected = 0;
        Ed.selectedData = NULL;
        Ed.selectedDataLen = 0;
    }
}

char *editorPrompt(char *prompt, void (*callback)(char*, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';
    
    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();

        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0){
                buf[--buflen] = '\0';
            }
        } else if (c == '\x1b') {
            editorSetStatusMessage("");
            if(callback) callback(buf, c);
            free(buf);

            return NULL; // will cause curosr position to reset! 
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if(callback) callback(buf, c);
                return buf;
            }
        }else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if(callback) callback(buf, c);
    }
}


/* ----- main ----- */

void initEditor() {

    // selected text
    Ed.sx = 0;
    Ed.sy = 0;
    Ed.ex = 0;
    Ed.ey = 0;
    Ed.selected = 0;
    Ed.selectedData = NULL;
    Ed.selectedDataLen = 0;
    Ed.copied = 0;
    Ed.copiedData = NULL;
    Ed.copiedDataLen = 0;

    Ed.rx = 0;
    Ed.cx = 0;
    Ed.cy = 0;
    
    Ed.numRows = 0;
    Ed.row=NULL;  
    Ed.scrollXOffset = 0;
    Ed.scrollYOffset = 0;
    Ed.filename = NULL;
    Ed.statusmsg[0] = '\0';
    Ed.statusmsg_time = 0;
    Ed.dirty = 0;


    if (getWindowSize(&Ed.screenRows, &Ed.screenCols) == -1) {
        die("getWindowSize");
    }
    Ed.screenRows -= 2;
    Ed.screenCols -= LINENO_BAR_WIDTH;

}

int main(int argc, char *argv[]){
    /*
        In raw mode, each character is processed immediately as it's typed, 
        allowing for real-time interaction without waiting for Enter. 
        In contrast, canonical mode allows users to edit their input 
        (e.g., using backspace) until they press Enter to submit the line. 
    */
    enableRawMode();
    initEditor();
    
    if(argc >=2){
        editorOpenFile(argv[1]);
    }
    
    editorSetStatusMessage("HELP: Ctrl-S : save | Ctrl-Q : quit | Ctrl-F : find");

    while (1){
        editorRefreshScreen();
        editorProcessKey();
    }

    return 0;
}

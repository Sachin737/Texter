#include<stdio.h>

int main(){
    printf("\033[48;5;17m"); // Set background color to dark blue

    printf("\033[2J");  // Clear the entire screen
    printf("\033[H");   // Move cursor to top-left corner

// Now the entire screen will have the specified background color
    return 0;
}
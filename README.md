
# Texter - A Lightweight Terminal Text Editor

Texter is a minimalistic text editor designed to run directly within the terminal. Inspired by the simplicity of classic editors like Vim and Nano, Texter aims to provide an efficient environment for editing files through a C-based solution.

## Features
- **Cross-platform compatibility**: Works on any Unix-based system with terminal support.
- **Simple terminal input handling**: Uses `termios` for capturing and managing terminal input.
- **Basic text manipulation**: Edit, save, copy, paste and manipulate text files with ease.
- **Cursor tracking and scrolling**: Keep track of cursor positions and scroll efficiently through files.
- **Status bar messages**: Display real-time status updates as you edit.

## Installation

To compile the source code, make sure you have a C compiler like `gcc` installed. Then, run the following command in your terminal:

```bash
@gcc texter.c -o texter -Wall -Wextra 
```

## Usage

Once compiled, you can run the editor by typing:

```bash
./texter <filename>
```

Replace `<filename>` with the name of the file you wish to edit, or leave it blank to start a new file.

### Key Bindings

- **Ctrl + Q**: Quit the editor
- **Ctrl + S**: Save the current file
- **Arrow Keys**: Navigate through the text
- **Fn + Arrow Keys**: Navigate quickly by pages
- **Ctrl + F**: Search through the text


## Contributing

Feel free to open issues and contribute to the project. All contributions are welcome!


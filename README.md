# Mini-Shell


## Objectives
- Implement a minimal Bash-like shell with key features:
  - **Directory navigation** (`cd`, `pwd`)
  - **Program execution** (support both relative and absolute paths)
  - **Environment variables** (`NAME="value"`, `$NAME`)
  - **Operators** for chaining commands (`;`, `&`, `|`, `&&`, `||`)
  - **I/O redirection** (`<`, `>`, `>>`, `2>`, `2>>`, `&>`)

## Core Functionalities

1. **Changing the Current Directory**
   - `cd <path>` changes the current directory to `<path>`.
   - Relative and absolute paths are supported.
   - `pwd` displays the current directory.
   - Edge cases:
     - `cd` with no arguments or more than one argument does nothing.

2. **Exiting the Shell**
   - Typing `quit` or `exit` ends the shell.

3. **Running Applications**
   - Executes commands in child processes using `fork()`.

4. **Environment Variables**
   - Inherit environment variables from the parent shell.
   - Undefined variables expand to `""` (empty string).
   - Syntax: `NAME="value"` or `NEW_VAR=$OLD_VAR`.

5. **Operators**
   - **Sequential (`;`)**: Run commands one after another.
   - **Parallel (`&`)**: Run commands simultaneously in different processes.
   - **Pipe (`|`)**: The output of one command is the input of the next.
   - **Conditional Execution**:
     - **`&&`**: Continue execution only if the previous command succeeds (exit code 0).
     - **`||`**: Continue execution only if the previous command fails (exit code non-0).
   - **Operator Precedence** (from highest to lowest):  
     1) Pipe (`|`)  
     2) Conditional operators (`&&`, `||`)  
     3) Parallel (`&`)  
     4) Sequential (`;`)

6. **I/O Redirection**
   - `< filename`  
   - `> filename`  
   - `2> filename`  
   - `&> filename`  
   - `>> filename`  
   - `2>> filename`  
   - Use `open`, `dup2`, and `close` to manage file descriptors.

## Building Mini-Shell
- Navigate to the `src/` directory and run `make`:
  ```sh
  cd src/
  make

// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "cmd.h"
#include "utils.h"

#define READ        0
#define WRITE       1

char *expand_variables(const char *input)
{
	// Return NULL if input is invalid
	if (!input)
		return NULL;

	// Allocate memory for the expanded string
	char *expanded = malloc(PATH_MAX);

	if (!expanded) {
		// Handle memory allocation failure
		perror("malloc");
		return NULL;
	}

	// Pointer for writing the expanded string
	char *output = expanded;
	// Pointer for reading the input string
	const char *ptr = input;

	while (*ptr) {
		if (*ptr == '$') {
			// Detect the start of a variable
			ptr++;
			char var_name[128] = {0};
			char *var_ptr = var_name;

			// Extract variable name
			while (*ptr && (isalnum(*ptr) || *ptr == '_'))
				*var_ptr++ = *ptr++;

			// Retrieve the variable value
			const char *value = getenv(var_name);

			if (value)
				// Append variable value
				output += sprintf(output, "%s", value);
		} else {
			// Copy regular characters
			*output++ = *ptr++;
		}
	}

	// Null-terminate the expanded string
	*output = '\0';
	// Return the expanded string
	return expanded;
}

char *remove_quotes(const char *input)
{
	if (!input)
		return NULL;

	char *result = malloc(strlen(input) + 1);

	if (!result) {
		perror("malloc");
		return NULL;
	}

	char *output = result;
	const char *ptr = input;

	while (*ptr) {
		if (*ptr != '\'' && *ptr != '\"')
			*output++ = *ptr;
		ptr++;
	}
	*output = '\0';

	return result;
}

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	const char *target_dir = NULL;

	// Decide target directory
	if (dir && dir->string && *dir->string != '\0') {
		target_dir = remove_quotes(dir->string);
		if (!target_dir) {
			fprintf(stderr, "cd: Memory allocation failed\n");
			return false;
		}
	} else {
		target_dir = getenv("HOME");
		if (!target_dir) {
			fprintf(stderr, "cd: HOME not set\n");
			return false;
		}
	}

	// Change directory
	if (chdir(target_dir) != 0) {
		fprintf(stderr, "cd: %s: No such file or directory\n", target_dir);
		if (dir && dir->string)
			free((char *)target_dir);
		return false;
	}

	if (dir && dir->string)
		free((char *)target_dir);

	return true;
}



/**
 * Internal pwd command.
 */
static void shell_pwd(void)
{
	char cwd[4096];

	// Get the current working directory
	if (!(getcwd(cwd, sizeof(cwd)) == NULL || sizeof(cwd) <= 1)) {
		// Print the current working directory
		if (cwd[0] != '\0') {
			printf("%s\n", cwd);
			fflush(stdout);
		} else {
			// If cwd is empty
			fprintf(stderr, "pwd: unexpected empty path\n");
		}
	} else {
		// Getcwd fails or buffer size is too small
		if (getcwd(cwd, sizeof(cwd)) == NULL) {
			perror("pwd");
		} else {
			// Buffer size is too small
			fprintf(stderr, "pwd: buffer too small to store the full path\n");
		}
	}
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	exit(0);
}

int get_io_flags(int io_flags, int append_flag)
{
	int flags = O_WRONLY | O_CREAT;

	if (io_flags & append_flag)
		flags |= O_APPEND;
	else
		flags |= O_TRUNC;

	return flags;
}

/**
 * Handle redirection for a command.
 */
static void handle_redirection(simple_command_t *cmd)
{
	if (!cmd)
		return;

	// Input redirection (< file)
	if (cmd->in) {
		char *expanded_in = expand_variables(cmd->in->string);

		if (expanded_in) {
			int fd_in = open(expanded_in, O_RDONLY);

			if (fd_in == -1) {
				perror("Error opening input file");
				free(expanded_in);
				exit(EXIT_FAILURE);
			}
			dup2(fd_in, STDIN_FILENO);
			close(fd_in);
			free(expanded_in);
		}
	}

	// Combined redirection for stdout and stderr (&>)
	if (cmd->out && cmd->err && strcmp(cmd->out->string, cmd->err->string) == 0) {
		char *expanded_out_err = expand_variables(cmd->out->string);

		if (expanded_out_err) {
			int flags = get_io_flags(cmd->io_flags, IO_OUT_APPEND);

			int fd_out_err = open(expanded_out_err, flags, 0644);

			if (fd_out_err == -1) {
				perror("Error opening combined output file");
				free(expanded_out_err);
				exit(EXIT_FAILURE);
			}
			dup2(fd_out_err, STDOUT_FILENO);
			dup2(fd_out_err, STDERR_FILENO);
			close(fd_out_err);
			free(expanded_out_err);
		}
	} else {
		// Output redirection (> file or >> file)
		if (cmd->out) {
			char *expanded_out = expand_variables(cmd->out->string);

			if (!expanded_out) {
				perror("Error expanding output file");
				exit(EXIT_FAILURE);
			}

			if (expanded_out) {
				int flags = get_io_flags(cmd->io_flags, IO_OUT_APPEND);

				int fd_out = open(expanded_out, flags, 0644);

				if (fd_out == -1) {
					perror("Error opening output file");
					free(expanded_out);
					exit(EXIT_FAILURE);
				}
				dup2(fd_out, STDOUT_FILENO);
				close(fd_out);
				free(expanded_out);
			}
		}

		// Error redirection (2> file or 2>> file)
		if (cmd->err) {
			char *expanded_err = expand_variables(cmd->err->string);

			if (expanded_err) {
				int flags = get_io_flags(cmd->io_flags, IO_ERR_APPEND);

				int fd_err = open(expanded_err, flags, 0644);

				if (fd_err == -1) {
					perror("Error opening error file");
					free(expanded_err);
					exit(EXIT_FAILURE);
				}
				dup2(fd_err, STDERR_FILENO);
				close(fd_err);
				free(expanded_err);
			}
		}
	}
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (!s || !s->verb || !s->verb->string)
		return -1;  // Command or verb is null, cannot proceed.

	char *word = get_word(s->verb);

	// Handle environment variable assignment
	if (strchr(word, '=')) {
		char *word_copy = strdup(word);

		if (!word_copy) {
			perror("strdup");
			return -1;
		}

		char *name = strtok_r(word_copy, "=", &word_copy);
		char *value = strtok_r(NULL, "=", &word_copy);

		if (name && value) {
			int result = setenv(name, value, 1);

			free(word_copy);
			return result;
		}

		free(word_copy);
		return -1;
	}

	// Handle internal commands
	if (strcmp(word, "cd") == 0 || strcmp(word, "pwd") == 0 ||
		strcmp(word, "exit") == 0 || strcmp(word, "quit") == 0) {
		int saved_stdout = dup(STDOUT_FILENO);
		int saved_stderr = dup(STDERR_FILENO);

		handle_redirection(s);

		int result = 0;

		if (strcmp(word, "cd") == 0) {
			if (shell_cd(s->params))
				result = 0;
			else
				result = 1;
		} else if (strcmp(word, "pwd") == 0) {
			shell_pwd();
		} else if (strcmp(word, "exit") == 0 || strcmp(word, "quit") == 0) {
			shell_exit();
		}

		dup2(saved_stdout, STDOUT_FILENO);
		dup2(saved_stderr, STDERR_FILENO);
		close(saved_stdout);
		close(saved_stderr);

		return result;
	}

	// Handle external commands
	pid_t pid = fork();

	if (pid == -1) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {  // Child process
		handle_redirection(s);

		int argv_size;
		char **argv = get_argv(s, &argv_size);

		execvp(word, argv);

		fprintf(stderr, "Execution failed for '%s'\n", word);
		free(argv);
		exit(EXIT_FAILURE);
	}

	int status;

	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	else
		return 1;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level, command_t *father)
{
	// Check if both commands are valid
	if (!cmd1 || !cmd2)
		return false;

	// Create the first child process
	pid_t pid1 = fork();

	if (pid1 == -1) {
		perror("fork (cmd1)");
		return false;
	}

	// Execute the first command
	if (pid1 == 0) {
		int exit_code = parse_command(cmd1, level + 1, father);

		exit(exit_code);
	}

	// Create the second child process
	pid_t pid2 = fork();

	if (pid2 == -1) {
		perror("fork (cmd2)");
		return false;
	}

	if (pid2 == 0) {
		// Exit the child process after executing the command
		int exit_code = parse_command(cmd2, level + 1, father);

		exit(exit_code);
	}

	// Wait for both child processes to finish
	int status1, status2;

	if (waitpid(pid1, &status1, 0) == -1 || waitpid(pid2, &status2, 0) == -1) {
		perror("waitpid");
		return false;
	}

	if (WIFEXITED(status1) && WIFEXITED(status2))
		return true;

	return false;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level, command_t *father)
{
	int pipefd[2];

	if (pipe(pipefd) == -1) {
		// Handle pipe creation failure
		if (errno == EMFILE || errno == ENFILE) {
			// Too many files open
			perror("pipe (resource-related error)");
			return false;
		}
		perror("pipe (error)");
		return false;
	}

	pid_t pid1 = fork();

	if (pid1 == -1) {
		// Handle fork failure
		if (errno == EAGAIN || errno == ENOMEM) {
			// Resource issues (too many processes or insufficient memory)
			perror("fork (resource-related error)");
			return false;
		}
		perror("fork (error)");

		// Clean up pipe file descriptors
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}

	if (pid1 == 0) {
		// First child (cmd1)
		close(pipefd[0]); // Close unused read end
		if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
			perror("dup2 (cmd1)");
			exit(EXIT_FAILURE);
		}
		close(pipefd[1]); // Close after duplication
		exit(parse_command(cmd1, level + 1, father));
	}

	pid_t pid2 = fork();

	if (pid2 < 0) {
		close(pipefd[0]);
		close(pipefd[1]);

		if (waitpid(pid1, NULL, 0) == -1)
			perror("Failed to clean up pid1"); // Ensure pid1 is cleaned up

		perror("Forking cmd2 failed");
		return false;
	}


	if (pid2 == 0) {
		// Second child (cmd2)
		close(pipefd[1]); // Close unused write end
		if (dup2(pipefd[0], STDIN_FILENO) == -1) {
			perror("dup2 (cmd2)");
			exit(EXIT_FAILURE);
		}
		close(pipefd[0]); // Close after duplication
		exit(parse_command(cmd2, level + 1, father));
	}

	// Parent process
	close(pipefd[0]);
	close(pipefd[1]);

	int status1, status2;

	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	// Return status of the last command in the pipeline
	return WIFEXITED(status2) && WEXITSTATUS(status2) == 0;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	if (!c || (father == NULL && level != 0))
		return -1;

	if (c->op == OP_NONE)
		return parse_simple(c->scmd, level, father);

	switch (c->op) {
	case OP_PARALLEL:
		if (run_in_parallel(c->cmd1, c->cmd2, level, c))
			return 0;
		else
			return -1;

	case OP_SEQUENTIAL: // ;
		parse_command(c->cmd1, level + 1, c);
		return parse_command(c->cmd2, level + 1, c);

	case OP_CONDITIONAL_ZERO: // &&
		if (parse_command(c->cmd1, level + 1, c) == 0)
			return parse_command(c->cmd2, level + 1, c);
		return 0;

	case OP_CONDITIONAL_NZERO: // ||
		if (parse_command(c->cmd1, level + 1, c) != 0)
			return parse_command(c->cmd2, level + 1, c);
		return 0;

	case OP_PIPE:
		if (run_on_pipe(c->cmd1, c->cmd2, level, c))
			return 0;
		fprintf(stderr, "Error: Pipe execution failed\n");
		return -1;

	default:
		return SHELL_EXIT;
	}

	return 0;
}

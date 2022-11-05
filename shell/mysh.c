#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// This is the maximum number of arguments your shell should handle for one command
#define MAX_ARGS 128

int main(int argc, char **argv)
{
  // If there was a command line option passed in, use that file instead of stdin
  if (argc == 2)
  {
    // Try to open the file
    int new_input = open(argv[1], O_RDONLY);
    if (new_input == -1)
    {
      fprintf(stderr, "Failed to open input file %s\n", argv[1]);
      exit(1);
    }

    // Now swap this file in and use it as stdin
    if (dup2(new_input, STDIN_FILENO) == -1)
    {
      fprintf(stderr, "Failed to set new file as input\n");
      exit(2);
    }
  }

  char *line = NULL;    // Pointer that will hold the line we read in
  size_t line_size = 0; // The number of bytes available in line

  int children_count = 0; // count how many background processes we have 

  // Loop forever
  while (true)
  {
    // Print the shell prompt
    printf("$ ");

    // Get a line of stdin, storing the string pointer in line
    if (getline(&line, &line_size, stdin) == -1)
    {
      if (errno == EINVAL)
      {
        perror("Unable to read command line");
        exit(2);
      }
      else
      {
        // Must have been end of file (ctrl+D)
        printf("\nShutting down...\n");

        // Exit the infinite loop
        break;
      }
    }
    
    // store the starting position of the next command 
    char *current_position = line;
    while (true)
    {
      // Call strpbrk to find the next occurrence of a delimeter
      char *delim_position = strpbrk(current_position, "&;");
      char delim;
      // ending with nothing is the same as ending with ;
      if (delim_position == NULL)
      {
        delim = ';';
      }
      else
      {
        // extract the delimiter
        delim = *delim_position;
        *delim_position = '\0';
      }

      // Read the input and convert it into a list of strings
      int arg_count = 0;
      char *argv[MAX_ARGS + 1];
      char *found;

      // convert the command into an array
      while ((found = strsep(&current_position, " \n")) != NULL)
      {
        if (strcmp(found, "") == 0)
          continue;
        argv[arg_count++] = found;
      }
      argv[arg_count] = NULL;

      // we don't do anything if no commands given
      if (arg_count == 0) 
      {
      }
      else if (strcmp(argv[0], "cd") == 0) // special command cd
      {
        chdir(argv[1]);
      }
      else if (strcmp(argv[0], "exit") == 0) // special command exit
      {
        exit(0);
      }
      else // other commands
      {
        // Create a child process
        pid_t child_id = fork();
        // Did fork fail?
        if (child_id == -1)
        {
          perror("fork failed");
          exit(EXIT_FAILURE);
        }

        // Are we in the parent or child?
        if (child_id == 0) // children
        {
          execvp(argv[0], argv);
          exit(-1);
        }
        else // parent
        {
          int status;
          if (delim == ';')
          {
            // wait for children to finish
            pid_t rc_front = waitpid(child_id, &status, 0);
            if (rc_front < 0)
            {
              perror("wait failed");
              exit(EXIT_FAILURE);
            }
            printf("[%s exited with status %d]\n", argv[0], WEXITSTATUS(status));
          }
          //background process
          else if (delim == '&')
          {
            children_count++;
          }
        }
      }

      // at the end of input, we print the status of background processes
      if (delim_position == NULL)
      {
        int status;
        int children_finished = 0;
        for (pid_t child_index = 0; child_index < children_count; child_index++)
        {
          // check the status of every running background processes without blocking the program
          pid_t rc_back = waitpid(-1, &status, WNOHANG);
          if (rc_back < 0) // the process we checked throw error
          {
            perror("wait() error");
          }
          else if (rc_back == 0) // the process is still running
          {
          }
          else if (rc_back > 0) // the process finished successfully. 
          {
            printf("[background process %d exited with status %d]\n", rc_back, WEXITSTATUS(status));
            children_finished++;
          }
        }
        children_count -= children_finished;
        break;
      }
      // Move our current position in the string to one character past the delimeter
      current_position = delim_position + 1;
    }
  }

  // If we read in at least one line, free this space
  if (line != NULL)
  {
    free(line);
  }

  return 0;
}


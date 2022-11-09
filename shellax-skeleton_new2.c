#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

const char *sysname = "shellax";

enum return_codes
{
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t
{
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next)
  {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
  if (command->arg_count)
  {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next)
  {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

typedef enum
{
  IN = 0,
  OUT,
  APPEND,
  DIRECTION_MAX
} DIRECTION;

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL)
  {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  }
  else
  {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1)
  {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue;                                          // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0)
    {
      struct command_t *c = malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before
                // handle input redirection
    redirect_index = -1;
    memset(command->redirects, 0, 3);
    if (arg[0] == '<')
      redirect_index = IN;
    if (arg[0] == '>')
    {
      if (len > 1 && arg[1] == '>')
      {
        redirect_index = APPEND;
        arg++;
        len--;
      }
      else
        redirect_index = OUT;
    }
    if (redirect_index != -1)
    {
      command->redirects[redirect_index] = malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;
  return 0;
}

void prompt_backspace()
{
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1)
  {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0)
      {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68)
    {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0)
      {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}
int process_command(struct command_t *command, int *pipefd);
int main()
{
  while (1)
  {
    struct command_t *command = malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command, NULL);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
int is_contains(char **arr, char *str, int arr_size)
{
  for (int i = 0; i < arr_size; i++)
  {
    if (!strcmp(arr[i], str))
      return i;
  }
  return -1;
}
int process_command(struct command_t *command, int *pipefd_r)
{
  int r;
  int pipefd[2];
  bool is_piped = command->next != NULL;
  if (is_piped)
    pipe(pipefd);

  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0)
  {
    if (command->arg_count > 0)
    {
      r = chdir(command->args[0]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  pid_t pid = fork();
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // increase args size by 2
    command->args = (char **)realloc(
        command->args, sizeof(char *) * (command->arg_count += 2));

    // shift everything forward by 1
    for (int i = command->arg_count - 2; i > 0; --i)
      command->args[i] = command->args[i - 1];

    // set args[0] as a copy of name
    command->args[0] = strdup(command->name);
    // set args[arg_count-1] (last) to NULL
    command->args[command->arg_count - 1] = NULL;

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    /* execvp(command->name, command->args); // exec+args+path
     exit(0);
   } else {
     // TODO: implement background processes here
     wait(0); // wait for child process to finish
     return SUCCESS;
   }*/

    DIRECTION dir_mode = 1000;
    char *file_name;
    for (int i = 0; i < DIRECTION_MAX; i++)
    {
      if (command->redirects[i] != NULL)
      {
        dir_mode = i;
        file_name = command->redirects[i];
      }
    }
    printf("exructing: %s\n", command->name);
    switch (dir_mode)
    {
    case IN:
      int fd_in = open(file_name, O_RDONLY, 0);
      if (fd_in == -1)
        fprintf(stderr, "file could not be read!");
      dup2(fd_in, STDIN_FILENO);
      close(fd_in);
      break;
    case OUT:
      int fd_out = creat(file_name, 0644);
      if (fd_out == -1)
        fprintf(stderr, "file could not be created or truncated!");
      dup2(fd_out, STDOUT_FILENO);
      close(fd_out);
      break;
    case APPEND:
      int fd_append = open(file_name, O_WRONLY | O_APPEND);
      if (fd_append == -1)
        fprintf(stderr, "file could not be found!");
      dup2(fd_append, STDOUT_FILENO);
      close(fd_append);
      break;
    }

    if (pipefd_r != NULL)
    {
      close(pipefd_r[1]);
      dup2(pipefd_r[0], STDIN_FILENO);
      close(pipefd_r[0]);
    }
    if (is_piped)
    {
      printf("baasdada\n");
      close(pipefd[0]);
      dup2(pipefd[1], STDOUT_FILENO);
      close(pipefd[1]);

      assert(!((pipefd_r != NULL) && is_piped));
      // printf("dsfaasfdsadfad\n");
      // printf("command name is%s\n", command->name);
      // printf("retrun value is %d\n", strcmp(command->name, "uniq"));
    }

    if (strcmp(command->name, "uniq") == 0)
    {
      // printf("dasfasdfasdf\n");
      char buf[BUFSIZ];
      memset(buf, 0, BUFSIZ);

      int n_bytes;
      char *str = malloc(BUFSIZ * sizeof(char));
      // printf("before while loop\n");
      // printf("%d\n",read(pipefd_r[0], &buf, sizeof(buf)));
      // perror("error ");
      while ((n_bytes = read(STDIN_FILENO, &buf, sizeof(buf))) > 0)
      {
        // printf("in while %d\n", n_bytes);
        strcat(str, buf);
        memset(buf, 0, BUFSIZ);
      }

      // printf("str value is %s\n", str);
      sleep(3);
      char *pch = NULL;

      pch = strtok(str, "\n");
      char *words[BUFSIZ];
      int i = 0;
      while (pch != NULL)
      {
        words[i++] = pch;
        // printf("from strtok %s\n", pch);
        pch = strtok(NULL, "\n");
      }

      // for (int x = 0; x < BUFSIZ; x++){
      //   if(words[x]!= NULL){
      //   printf("words in words list%s\n", words[x]);
      //   }
      // }

      char *unique_words[BUFSIZ];
      int unique_words_size = 0;
      
      if (command->arg_count > 1)
      {
        for (int i = 0; i < BUFSIZ; i++)
        {
          int j;
          int count = 0;
          if (words[i] != NULL)
          {
            for (j = 0; j < BUFSIZ; j++)
            {
              if (words[j] != NULL)
              {
                // printf("checking for ith word %s\n", words[i]);
                // printf("checking for jth word %s\n", words[j]);
                // printf("for these words strcmp value is %d\n", !strcmp(words[i], words[j]));

                if (!strcmp(words[i], words[j]))
                {
                  // printf("entering the if statement \n");

                  break;
                }
              }
            }
            if (i == j)
            {
              printf("From our uniq:\t %s\n", words[i]);
              unique_words[unique_words_size++] = words[i];
            }
          }
        }
      }
      if (command->arg_count > 1 && (!strcmp(command->args[1], "-c") || !strcmp(command->args[1], "--count")))
      {

        for (int i = 0; i < BUFSIZ; i++)
        {
          int count = 0;
          if (unique_words[i] != NULL)
          {
            for (int j = 0; j < BUFSIZ; j++)
            {
              if (words[j] != NULL)
              {
                if (!strcmp(unique_words[i], words[j]))
                {
                  count++;
                }
              }
            }
          }
          if (count != 0)
          {
            printf("From our uniq: \t %d %s\n", count, unique_words[i]);
          }
        }
      }

      // for (int x = 0; x < BUFSIZ; x++)
      // {
      //   printf("checking for this word %s\n", words[x]);
      //   int uniq_word_index = is_contains(unique_words, words[x], unique_words_size);
      //   if (uniq_word_index == -1)
      //   {
      //     words_counts[unique_words_size] = 1;
      //     unique_words[unique_words_size] = words[x];
      //     unique_words_size++;
      //   }
      //   else
      //   {
      //     words_counts[uniq_word_index]++;
      //   }
      // }
      // printf("%d\n", command->arg_count);
      // if (command->arg_count > 1)
      // {

      //   if (!strcmp(command->args[1], "-c") ||
      //       !strcmp(command->args[1], "--count"))
      //   {
      //     for (int i = 0; i < unique_words_size; i++)
      //     {
      //       printf("\t%d %s\n", words_counts[i], unique_words[i]);
      //     }
      //   }
      // }
      // else
      // {
      //   for (int i = 0; i < unique_words_size; i++)
      //   {
      //     printf("%s\n", unique_words[i]);
      //   }
      // }

      // printf("\n");
      // return SUCCESS;

      // char *words_count[BUFSIZ];
      // printf("args is %s\n", command->args[0]);

      // if (command->arg_count > 0)
      // {

      //   if (!strcmp(command->args[1], "-c") ||
      //       !strcmp(command->args[1], "--count"))
      //   {
      //     int i;
      //     printf("dasfasfdjl%d\n", i);

      //     for (i = 0; i < BUFSIZ; i++)
      //     {
      //       int count = 1;
      //       int j;
      //       for (j = 0; j < BUFSIZ; j++)
      //       {
      //         if (!strcmp(words[i], words[j]))
      //         {
      //           count++;
      //         }
      //       }
      //       // printf("%d", count);
      //       if (i == j)
      //       {
      //         char *num;
      //         if (asprintf(&num, "%d", count) == -1)
      //         {
      //           fprintf(stderr, "Num couldnt be converted!");
      //         }
      //         words_count[i] = strcat(strcpy(buf, num), words[i]);
      //       }
      //     }
      //     for (i = 0; i < BUFSIZ; i++)
      //     {
      //       printf("words count%s\n", words_count[i]);
      //       printf("\n");
      //     }
      //   }
      // }
    }
    char *dir_name;
    // printf("size of command arg is %d\n", sizeof(command->args));
    // printf("size of command arg[0] is %d\n", sizeof(command->args[0]));
    // if ((sizeof(command->args) / sizeof(command->args[0])) > 1)
    // {

    //   dir_name = malloc((strlen(command->args[1]) + 1) * sizeof(char));
    //   strcpy(dir_name, command->args[1]);
    //   printf("directory name is%s\n", dir_name);
    // }

    // DIR* dir = opendir(dir_name);
    // // printf("%p", dir);
    // if(errno = ENOENT){
    //   printf("creating a directory");
    //   mkdir(dir_name, 0700);

    // }

    char *path1 = "/usr/bin/";
    char *path2 = "/bin/";

    char *path1_command = (char *)malloc(
        (strlen(command->name) + strlen(path1) + 1) * sizeof(char));
    strcpy(path1_command, path1);
    strcat(path1_command, command->name);
    command->args[0] = path1_command;
    int exec1 = execv(path1_command, command->args);
    if (exec1 == -1)
    {
      char *path2_command = (char *)malloc(
          (strlen(command->name) + strlen(path2) + 1) * sizeof(char));
      strcpy(path2_command, path2);
      strcat(path2_command, command->name);
      command->args[0] = path2_command;
      int exec2 = execv(path2_command, command->args);
      if (exec2 == -1)
      {
        fprintf(stderr, "couldnt create execution!\n");
        return SUCCESS;
      }
    }
  }
  else
  {

    // TODO: implement background processes here
    // wait(0); // wait for child process to finish
    // return SUCCESS;
    // pid_t pid_child = -1;
    if (is_piped)
    {
      // wait(NULL);
      close(pipefd[1]);
      process_command(command->next, pipefd);

      // pid_child = fork();
      //   if (pid_child == 0)
      //   {
      //     printf(" %swe are entering the shell\n", command->name);
      //     close(pipefd[1]);
      //     process_command(command->next, pipefd);
      //     printf("we are exiting the shell %s", command->name);
      //     exit(0);
      //   }
    }

    if (!command->background)
    {

      waitpid(pid, NULL, 0);

      // waitpid(pid_child, NULL, 0);

      return SUCCESS;
    }
    else
    {
      printf("Background process %s is killed.", command->name);
      return SUCCESS;
    }
  }

  // TODO: your implementation here

  printf("-%s: %s: command not found\n", sysname, command->name);
  return UNKNOWN;
}

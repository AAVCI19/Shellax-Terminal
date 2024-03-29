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
// I/O Redirection enum 
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
            continue;                                        // empty arg, go for next
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
int process_command(struct command_t *command, int *pipefd_r)
{
    int r;
    // open pipe for handling program piping
    int pipefd[2];
    // check pipe relation between processes
    // if next command is not null, then previous command has pipe
    // connection with next command
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
        // check for filename and direction mode
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
        // According to direction mode, redirect the file
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
        // Function argument for piping multiple processes
        // Read from this pipefd_r, duplicating the STDIN to pipefd_r[0]
        if (pipefd_r != NULL)
        {
            close(pipefd_r[1]);
            dup2(pipefd_r[0], STDIN_FILENO);
            close(pipefd_r[0]);
        }
        // Check for pipe condition between processes and duplicate the 
        // STDOUT of previous process to pipefd[1]
        if (is_piped)
        {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);

            assert(!((pipefd_r != NULL) && is_piped));
        }
        // Uniq command implementation
        if (strcmp(command->name, "uniq") == 0)
        {
            char buf[BUFSIZ];
            memset(buf, 0, BUFSIZ);
            // read from the STDIN and write to str 
            int n_bytes;
            char *str = malloc(BUFSIZ * sizeof(char));
            while ((n_bytes = read(STDIN_FILENO, &buf, sizeof(buf))) > 0)
            {
                strcat(str, buf);
                memset(buf, 0, BUFSIZ);
            }
            // parse according to the newline
            char *pch = NULL;

            pch = strtok(str, "\n");
            char *words[BUFSIZ];
            memset(words, 0, BUFSIZ);
            int word_count = 0;
            while (pch != NULL)
            {
                words[word_count++] = pch;
                pch = strtok(NULL, "\n");
            }
            // find the unique words and put them into an array
            char *unique_words[BUFSIZ];
            int unique_words_size = 0;
            for (int i = 0; i < word_count; i++)
            {
                int j;
                int count = 0;
                if (words[i] != NULL)
                {
                    for (j = 0; j < word_count; j++)
                    {
                        if (words[j] != NULL)
                        {
                            if (!strcmp(words[i], words[j]))
                            {
                                break;
                            }
                        }
                    }
                    if (i == j)
                    {
                        unique_words[unique_words_size++] = words[i];
                    }
                }
            }
            // print unique elements
            if (command->arg_count == 2)
            {
                for (int i = 0; i < unique_words_size; i++)
                {
                    if (unique_words[i] != NULL)
                    {
                        printf("From our uniq:\t %s\n", unique_words[i]);
                    }
                }
            }
            // print unique elements with occurrences in the file
            if (command->arg_count == 3 && (!strcmp(command->args[1], "-c") || !strcmp(command->args[1], "--count")))
            {
                for (int i = 0; i < unique_words_size; i++)
                {
                    int count = 0;
                    if (unique_words[i] != NULL)
                    {
                        for (int j = 0; j < word_count; j++)
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
                        printf("From our uniq count: \t %d %s\n", count, unique_words[i]);
                    }
                }
            }
        }
        // implementation of the wiseman command
        if (!strcmp(command->name, "wiseman"))
        {
            // find the path of fortune command
            char *fortune = "/usr/games/fortune";
            char *fortune_arg[2] = {fortune, NULL};
            // open pipe
            int pipefd_e[2];
            pipe(pipefd_e);
            char read_message[1024];
            // Execute fortune and read it's STDOUT using read_message array
            pid_t pid_wiseman = fork();
            if (pid_wiseman == 0)
            {
                close(pipefd_e[0]);
                dup2(pipefd_e[1], STDOUT_FILENO);
                execv(fortune, fortune_arg);
            }
            else
            {
                wait(NULL);
                close(pipefd_e[1]);
                read(pipefd_e[0], read_message, sizeof(read_message));
                close(pipefd_e[0]);
                printf("read message is: %s\n", read_message);
                // find path of crontab
                char *cronfile_name = "fortune_cron";
                char *crontab = "/usr/bin/crontab";
                char *crontab_args[3] = {crontab, cronfile_name, NULL};
                char crontab_cmd[2048];
                // check for wiseman if it has enough arguments
                if (command->args[1] == NULL)
                {
                    fprintf(stderr, "Wiseman argument not provided!\n");
                    return UNKNOWN;
                }
                // since we add newline for crontab so we delete newline of read_message
                read_message[strlen(read_message) - 1] = 0;
                // crontab_cmd is syntax of our cronjob 
                sprintf(crontab_cmd, "*/%s * * * * DISPLAY=0 espeak \"%s\"\n", command->args[1], read_message);
                printf("%s\n", crontab_cmd);
                // write cronjob to a file then give as an argument to crontab
                FILE *file = fopen(cronfile_name, "w");
                if (file == NULL)
                {
                    perror("File opening error");
                }
                if (fwrite(crontab_cmd, sizeof(char), strlen(crontab_cmd), file) == -1)
                {
                    perror("Writing error");
                }
                fclose(file);
                // execute crontab
                pid_t pid_cron = fork();
                if (pid_cron == 0)
                {
                    execv(crontab, crontab_args);
                }
                else
                {
                    wait(NULL);
                }
                remove(cronfile_name);
                return SUCCESS;
            }
        }
        // Path of commands for execv to execute
        char *path1 = "/usr/bin/";
        char *path2 = "/bin/";
        // create full path of commands then give to execv
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
        // Recursive pipe calls, close pipefd[1]
        if (is_piped)
        {
            // wait(NULL);
            close(pipefd[1]);
            process_command(command->next, pipefd);
        }
        // handle background process
        if (!command->background)
        {
            waitpid(pid, NULL, 0);
            return SUCCESS;
        }
        else
        {
            printf("%s is in background process!", command->name);
            return SUCCESS;
        }
    }

    // TODO: your implementation here

    printf("-%s: %s: command not found\n", sysname, command->name);
    return UNKNOWN;
}
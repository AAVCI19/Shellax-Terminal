#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#define BUFFSIZ 512
int main(int argc, char *argv[])
{
    // check for enough arguments
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <roomname> <username>", argv[0]);
        return 1;
    }
    // create chatroom directory
    char chatroom[BUFFSIZ];
    sprintf(chatroom, "/tmp/chatroom-%s", argv[1]);

    mkdir(chatroom, 0700);
    
    // create named pipe
    char pipename[BUFFSIZ];
    sprintf(pipename, "/tmp/chatroom-%s/%s", argv[1], argv[2]);

    if (mkfifo(pipename, S_IRUSR | S_IWUSR) == -1)
    {
        perror("Error piping!");
    }
    // process for writing, since every named process has own writing process
    pid_t pid_write = fork();
    int fd_write;
    char message[BUFFSIZ];

    if (pid_write == 0)
    {
        while (1)
        {
            // take message from stdin
            printf("Your Message:");
            fflush(stdout);
            fgets(message, BUFFSIZ, stdin);
            // open the directory and iterate over all named pipes
            struct dirent *dir;
            DIR *chatroom_dir = opendir(chatroom);

            while ((dir = readdir(chatroom_dir)) != NULL)
            {

                if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
                {
                    continue;
                }

                char directory[2048];

                sprintf(directory, "%s/%s", chatroom, dir->d_name);

                char new_message[2048];
                sprintf(new_message, "%s:%s", argv[2], message);
                // open named pipe for writing
                fd_write = open(directory, O_WRONLY);
                if ((write(fd_write, new_message, strlen(new_message) + 1)) == -1)
                {
                    perror("Error in writing");
                }

                memset(new_message, 0, 2048);
                memset(directory, 0, 2048);

                close(fd_write);
            }
            memset(message, 0, BUFFSIZ);
            closedir(chatroom_dir);
        }
    }
    else
    {
        // parent process for reading
        int fd_read;
        char read_message[BUFFSIZ];

        while ((1))
        {
            // open named pipe for reading
            fd_read = open(pipename, O_RDONLY);
            // read messages
            if ((read(fd_read, read_message, sizeof(read_message))) == -1)
            {
                perror("Error in reading:");
            }
            printf("%s", read_message);

            memset(read_message, 0, BUFFSIZ);
            close(fd_read);
        }
    }
}
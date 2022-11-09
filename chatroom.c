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

#define BUFFSIZE 512
int main(int argc, char *argv[])
{
    int fd, n;

    char buf[BUFFSIZE];

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <roomname> <username>", argv[0]);
        return 1;
    }
    char pipename[BUFFSIZE];
    char chatroom[BUFFSIZE];
    sprintf(pipename, "/tmp/chatroom-%s/%s", argv[1], argv[2]);
    sprintf(chatroom, "/tmp/chatroom-%s", argv[1]);

    mkfifo(pipename, O_RDWR | O_CREAT);
    if (errno == ENOENT)
    {
        if (mkdir(chatroom, 0700) == -1)
        {
            perror("Error for creating directory: ");
            return 1;
        }
        mkfifo(pipename, O_RDWR | O_CREAT);
    }

    else if (errno == EEXIST)
    {

        fprintf(stderr, "already exist!\n");
        return 1;
    }
    int my_fd = open(pipename, O_RDWR);
    DIR *chatroom_dir = opendir(chatroom);
    pid_t pid_read = fork();
    if (pid_read == 0)
    {
        while (1)
        {
            printf("Reading messages from the users\n");
            while ((n = read(my_fd, buf, BUFFSIZE)) > 0)
            {

                printf("message:");    
                if (write(STDOUT_FILENO, buf, n) != n)
                {
                    exit(1);
                }
            }
        }
        return 0;
    }
    while (1)
    {

        struct dirent *dir;
        printf("Your message:");
        char message[BUFFSIZE];
        int message_size = read(STDIN_FILENO, message, BUFFSIZE);

        while ((dir = readdir(chatroom_dir)) != NULL)
        {
            pid_t pid_w = fork();
            if (pid_w == -1)
                perror("Error forking a new process");
            else if (pid_w == 0)
            {

                printf("%s\n", dir->d_name);
                int fd;
                if ((fd = open(dir->d_name, O_RDONLY)) < 0)
                    perror("Error opening user pipe:");
                if (write(fd, buf, message_size) != message_size)
                {
                    perror("Error for writing:");
                }
                return 1;
            }
        }
    }

    // if ((fd = open("fifo_x", O_WRONLY)) < 0)
    //     err("open");

    // while ((n = read(STDIN_FILENO, buf, BUFFSIZE)) > 0)
    // {
    //     if (write(fd, buf, strlen(buf)) != strlen(buf))
    //     {
    //         err("write");
    //     }
    // }
    // close(fd);

    // int fd, n;
    // char buf[BUFFSIZE];

    // if ((fd = open("fifo_x", O_RDONLY)) < 0)
    //     err("open");

    // while ((n = read(fd, buf, BUFFSIZE)) > 0)
    // {

    //     if (write(STDOUT_FILENO, buf, n) != n)
    //     {
    //         exit(1);
    //     }
    // }
    // close(fd);
}

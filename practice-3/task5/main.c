#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
int main(){
    
    pid_t pid;
    // OPEN FILES
    int fd;
    fd = open("test.txt" , O_RDWR | O_CREAT | O_TRUNC);
    if (fd == -1)
    {
        /* code */
        printf("Open test file failed!\n");
        return 0;
    }
    //write 'hello fcntl!' to file

    /* code */
    write(fd, "hello fcntl!", 12);

    // DUPLICATE FD

    /* code */
    int new_fd = fcntl(fd, F_DUPFD, 0);
    char *str;

    pid = fork();

    if(pid < 0){
        // FAILS
        printf("error in fork");
        return 1;
    }
    
    struct flock fl;

    if(pid > 0){
        // PARENT PROCESS
        //set the lock
        fl.l_type = F_WRLCK;
        fcntl(fd, F_SETLK, &fl);

        //append 'b'
        write(fd, "b", 1);

        //unlock
        sleep(3);
        
        fl.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &fl);

        // int fd2 = open("test.txt", O_RDONLY);
        read(fd, str, 14);
        // str[14] = '\0';
        printf("file after: %s", str); // the feedback should be 'hello fcntl!ba'
        
        exit(0);

    } else {
        // CHILD PROCESS
        sleep(2);
        //get the lock
        fcntl(fd, F_GETLK, &fl);
        //append 'a'
        write(fd, "a", 1);

        exit(0);
    }
    close(fd);
    return 0;
}
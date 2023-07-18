#include"../kernel/types.h"
#include"../kernel/stat.h"
#include"user.h"

int main(){
    //file discription
    char readbuffer[5];
    const char writebuffer[]="test";

    //ParentWriteChildRead pipe
    int PWCRpipefd[2];

    //parentreadchildwrite pipe
    int PRCWpipefd[2];

    //openpipe
    if(pipe(PWCRpipefd)||pipe(PRCWpipefd)){
        printf("pipe create error");
        exit(-1);
    }

    //fork
    int childpid=fork();
    if(childpid==-1){
        printf("fork error");
        exit(-1);
    }

    if(childpid==0){
        //close unuse pipe
        close(PWCRpipefd[1]);
        close(PRCWpipefd[0]);
        read(PWCRpipefd[0],readbuffer,1);
        printf("%d: received ping\n",getpid());
        write(PRCWpipefd[1],writebuffer,1);
        exit(0);

    }
    else
    {
        close(PWCRpipefd[0]);
        close(PRCWpipefd[1]);
        write(PWCRpipefd[1],writebuffer,1);
        read(PRCWpipefd[0],readbuffer,1);
        printf("%d: received pong\n",getpid());
        exit(0);
    }
    

  
}
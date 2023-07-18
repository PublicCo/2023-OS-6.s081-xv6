#include "user.h"
#include "../kernel/types.h"
#include "../kernel/stat.h"

#define MAXSIZE 35

void log(int passnum,int recordnum[]){
    printf("recordnum is");
    for(int i=0;i<passnum;i++){
        printf("%d,",recordnum[i]);
    }
    printf("\n");
}

int main()
{

    int recordnum[MAXSIZE];
    // init record buff
    int passnum = 0;
    for (int i = 2; i <= MAXSIZE; i++)
    {
        recordnum[passnum++] = i;
    }
    // create new pipe
    int pipefd[2];

    while (passnum>0)
    {
        if (pipe(pipefd))
        {
            printf("pipe create error");
            exit(-1);
        }
        int pid = fork();
        if (pid < 0)
        {
            // fail fork
            printf("fork error\n");
            exit(-1);
        }
        else if (pid > 0)
        {
            close(pipefd[0]);
            for (int i = 0; i < passnum; i++)
            {
                write(pipefd[1], recordnum + i, sizeof(i));
            }
            close(pipefd[1]);
            wait((int *)0);
            exit(0);
        }
        else
        {
            close(pipefd[1]);
            int index = -1, curprime = -1;
            int tmp = -3;
            while (read(pipefd[0], &tmp, sizeof(tmp)) > 0)
            {
                
                if(index<0){
                    curprime=tmp;
                    index++;
                }
                else if(tmp%curprime!=0){
                    recordnum[index++]=tmp;
                }
                // log(passnum,recordnum);
            }
            close(pipefd[0]);
            printf("prime %d\n", curprime);
            passnum = index;
            
        }
        
    }
    exit(0);
}

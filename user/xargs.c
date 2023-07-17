#include "user.h"
#include "../kernel/stat.h"
#include "../kernel/types.h"
#include "../kernel/param.h"

#define STDINFD 0
#define MAXLENGTH 128

int main(int args, char *argc[])
{
    char *nextargc[MAXARG];

    //exec execute from the nextargc[1]
    int curargsnum=0;

    // get cur argc
    for ( int j = 1; j < args; curargsnum++, j++)
    {
        nextargc[curargsnum] = argc[j];
        
    }

    // read from stdin
    int n;
    char buffer[MAXLENGTH];
    while ((n = read(STDINFD, buffer, MAXLENGTH)) > 0)
    {

        // fork
        int pid = fork();
        if (pid != 0)
        {
            wait(0);

        }
        else
        {
            int cnt=0;
            int nextargs=curargsnum;

            // read param from buffer
            for(int i=0;i<n;i++){
                if((buffer[i]==0||buffer[i]==' '||buffer[i]=='\n')&&cnt!=0){
                    //make it become 0
                    buffer[i]=0;

                    //malloc a string to add to the param part
                    char* tmp=(char*)malloc(cnt*sizeof(char)+1);
                    strcpy(tmp,&buffer[i-cnt]);
                    cnt=0;

                    //add it to nextargc
                    nextargc[nextargs++]=tmp;
                }
                else{
                    cnt++;
                }
            }
            
            // deal with the last param
            char* tmp = (char*) malloc(cnt*sizeof(char)+1);
            buffer[n]=0;
            strcpy(tmp,&buffer[n-cnt]);
            nextargc[nextargs++]=tmp;

            exec(argc[1], nextargc);

        }
    }

    exit(0);
}
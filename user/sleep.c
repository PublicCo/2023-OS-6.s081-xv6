#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]){
    // check param num

    if(argc<=1){ 
        printf("not enough param\n");
        exit(1);
    }
    
    //check the last char is number
    //char* mystring;
    //strcpy(mystring,argv[1]);
    int num = atoi(argv[1]);
    for(int i=0;i<num;i++){
        sleep(1);
        
    }
    exit(0);
}


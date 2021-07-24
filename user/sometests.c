#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAX_TOTAL_PAGES_USER 32

int tst_MaxPages()
{
    int page_number = count_pages();
    int child=fork();
    char * ptrs[32];
    if(child==0)
    {
        for (int i = 0; i < MAX_TOTAL_PAGES_USER-page_number; i++)
        {
            ptrs[i]=sbrk(4096);
            *ptrs[i]=i;
        }

        for (int i = 0; i < MAX_TOTAL_PAGES_USER-page_number; i++)
        {
            if(*ptrs[i]==i)
            {
                printf("OK\n");
            }
        }
        exit(0);
    }
    else
    {
        wait(&child);
        printf("tst_MaxPagesInsideExecTest: OK\n");
        return 0;
    }
    
}

int main(){
    tst_MaxPages();
    exit(0);
}
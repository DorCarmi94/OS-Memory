#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAX_TOTAL_PAGES_USER 32

int tst_MaxPages()
{
    fprintf(2,"--------------------------------------------------------------------------------\n");
    fprintf(2,"Max pages test:\n");
    fprintf(2,"--------------------------------------------------------------------------------\n");
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
        for (int i = 0; i < MAX_TOTAL_PAGES_USER/2; i++)
        {
            if(i%2==0)
            {
                if(*ptrs[1]==i)
                {
                    printf("OK\n");
                }
            }
            else
            {
                if(*ptrs[2]==i)
                {
                    printf("OK\n");
                }
            }
        }
        printf("ALL OK\n");
        exit(0);
    }
    else
    {
        wait(&child);
        fprintf(2,"Finish test\n");
        fprintf(2,"--------------------------------------------------------------------------------\n");
        return 0;
    }
    
}


int tst_TooManyPages()
{
    fprintf(2,"--------------------------------------------------------------------------------\n");
    fprintf(2,"Too many pages test:\n");
    fprintf(2,"--------------------------------------------------------------------------------\n");
    int page_number = count_pages();
    int child=fork();
    char * ptrs[32];
    if(child==0)
    {
        for (int i = 0; i < MAX_TOTAL_PAGES_USER-page_number+1; i++)
        {
            ptrs[i]=sbrk(4096);
            *ptrs[i]=i;
        }

        for (int i = 0; i < MAX_TOTAL_PAGES_USER-page_number+1; i++)
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
        int ans=wait(&child);
        if(ans<0)
        {
            printf("ALL OK\n");
        }
        else
        {
            printf("FAIL\n");
        }
        fprintf(2,"--------------------------------------------------------------------------------\n");
        return 0;
    }
    
}


void tst_Exec()
{
    fprintf(2,"--------------------------------------------------------------------------------\n");
    fprintf(2,"Exec test:\n");
    fprintf(2,"--------------------------------------------------------------------------------\n");
    int child=fork();
    if(child==0)
    {
        //child
        char *args[2] = { "sometests", 0 };
        exec("/sometests", args);
        printf("tst_Exec: FAIL\n");
    }
    else{
        wait(&child);
        printf("tst_Exec: OK\n");
        fprintf(2,"--------------------------------------------------------------------------------\n");
        return;
    }
}


void tst_Fork()
{
    fprintf(2,"--------------------------------------------------------------------------------\n");
    fprintf(2,"Fork test:\n");
    fprintf(2,"--------------------------------------------------------------------------------\n");
    char * ptrs[32];
    int currentPages=count_pages();
    for (int i = 0; i < MAX_TOTAL_PAGES_USER-currentPages; i++)
    {
        ptrs[i]=sbrk(4096);
        *ptrs[i]=i;
    }

    int child=fork();
    if(child==0)
    {
        int countFails=0;
        //child
        for (int i = 0; i < MAX_TOTAL_PAGES_USER-currentPages; i++)
        {
            if(*ptrs[i]==i)
            {
                printf("OK\n");
            }
            else{
                printf("FAIL\n");
                countFails++;
            }
        }
        

        if(countFails==0)
        {
            printf("Fork test: ALL PASS\n");
        }
        else
        {
            printf("Fork test:FAIL\n");
        }
        exit(0);
    }
    else{
        //father
        wait(&child);
        fprintf(2,"--------------------------------------------------------------------------------\n");
        return;
    }

}

void tst_Pagefaults(){
    fprintf(2,"--------------------------------------------------------------------------------\n");
    fprintf(2,"Pagefaults test:\n");
    fprintf(2,"--------------------------------------------------------------------------------\n");
    int page_number = count_pages();
    char * ptrs[32];

    int countWithoutPageFaults=0;
    fprintf(2,"Creations: add pages to proc: --------------\n");
    for (int i = 0; i < MAX_TOTAL_PAGES_USER/2-page_number; i++)
    {
        ptrs[i]=sbrk(4096);
        int wasPagefault = pagefault();
        if(wasPagefault)
        {
            printf("FAIL\n");
        }
        else
        {
            countWithoutPageFaults++;
            printf("Ok\n");
        }

    }
    fprintf(2, "%d pages was created with %d pagefaults\n",MAX_TOTAL_PAGES_USER/2-page_number,MAX_TOTAL_PAGES_USER/2-page_number-countWithoutPageFaults);
    int countPageFaults=0;
    for (int i =  MAX_TOTAL_PAGES_USER/2-page_number; i < MAX_TOTAL_PAGES_USER/2; i++)
    {
        ptrs[i]=sbrk(4096);
        int wasPagefault = pagefault();
        if(wasPagefault)
        {
            countPageFaults++;
        }
    }
    fprintf(2,"%d pages was created with %d pagefaults\n",MAX_TOTAL_PAGES_USER/2, countPageFaults);

    fprintf(2,"Access to memeory:-----------------\n");
    countPageFaults=0;
    for (int i = 0; i < MAX_TOTAL_PAGES_USER-page_number; i++)
    {
        *ptrs[i]=5;
        int wasPagefault = pagefault();
        if(wasPagefault)
        {
            countPageFaults++;
        }
    }
    fprintf(2,"%d accesses to pages went with %d pagefaults\n",(MAX_TOTAL_PAGES_USER/2), countPageFaults);
    fprintf(2,"--------------------------------------------------------------------------------\n");
    return;
}

int main(){

    //basic tsts
    tst_Pagefaults();

    //max tsts
    
    tst_MaxPages();
    
    
    
    //flipPageTracePrintFlag();
    //tst_TooManyPages();   


    //fork and exec
    tst_Exec();
    //flipPageTracePrintFlag();
    tst_Fork();
    
    exit(0);
}

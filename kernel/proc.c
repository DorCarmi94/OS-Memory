#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  


  //handle all about pages

  p->pageIdxInHeadOfQueue=START_OF_QUEUE;
  p->pageIdxInTailOfQueue=END_OF_QUEUE;
  p->wasLastPageFault = 0;

  p->pagePrintTraceOn=0;


  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }


  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  if(p->pid>2)
  {
    release(&p->lock);

    #ifndef NONE

      createSwapFile(p);

    #endif


    acquire(&p->lock);
    for(int i=0; i<MAX_TOTAL_PAGES; i++)
    {
      p->pageFileTable[i].fileLocation=0;
      p->pageFileTable[i].isInFile=0;
      p->pageFileTable[i].isInUse=0;
      p->pageFileTable[i].pageNum=i;
      p->pageFileTable[i].va=0;
      
      //SCFIFO
      p->pageFileTable[i].nextLink=-1;
      p->pageFileTable[i].prevLink=-1;

      //NFU
      p->pageFileTable[i].counter_NFU=0;
      p->pageFileTable[i].counter_LA=0XFFFFFFFF;
    }
    p->PageTablesInUse=0;

    for (int i = 0; i < MAX_PSYC_PAGES+1; i++)
    {
      p->fileAddressPool[i]=0;
    }
  }
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}



// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();  
  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;

  //printPageFileTableStruct(p->pagetable);
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();
  // printf("fork: p->number of pages: %d, p->sz: %d\n\n",p->PageTablesInUse,p->sz);
  // printPageFileTableStructForProc(p);

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;
  
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);
  
  if(np->pid>2)
  {
    for (i = 0; i < MAX_TOTAL_PAGES; i++)
    {
      np->pageFileTable[i].fileLocation=p->pageFileTable[i].fileLocation;
      np->pageFileTable[i].isInFile=p->pageFileTable[i].isInFile;
      np->pageFileTable[i].isInUse=p->pageFileTable[i].isInUse;
      np->pageFileTable[i].va=p->pageFileTable[i].va;
      np->pageFileTable[i].idxInFile=p->pageFileTable[i].idxInFile;

      np->pageFileTable[i].nextLink=p->pageFileTable[i].nextLink;
      np->pageFileTable[i].prevLink=p->pageFileTable[i].prevLink;

      np->pageFileTable[i].counter_LA=p->pageFileTable[i].counter_LA;
      np->pageFileTable[i].counter_NFU=p->pageFileTable[i].counter_NFU;

      #ifndef NONE
        if(p->pageFileTable[i].isInFile)
        {
          readFromSwapFile(p,np->buffer,p->pageFileTable[i].fileLocation,PGSIZE);
          writeToSwapFile(np,np->buffer,np->pageFileTable[i].fileLocation,PGSIZE);
        }
      #endif
    }

    np->pageIdxInHeadOfQueue=p->pageIdxInHeadOfQueue;
    np->pageIdxInTailOfQueue=p->pageIdxInTailOfQueue;

    np->pagePrintTraceOn=p->pagePrintTraceOn;

    np->PageTablesInUse=p->PageTablesInUse;
    for (int i = 0; i < MAX_PSYC_PAGES+1; i++)
    {
      np->fileAddressPool[i]=p->fileAddressPool[i];
    }

    //printPageFileTableStructForProc(np);
  }

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);
  //printf("end fork: np->number of pages: %d, np->sz: %d\n",np->PageTablesInUse,np->sz);
  
  return pid;
  
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  #ifndef NONE
    removeSwapFile(p);
  #endif
  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;
  
  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  
  #ifndef NONE
    if(p->pid>2)
    {
      updateCounters();
    }
  #endif
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

//assumption: all pages in ram are in queue
int scfifoChoose()
{
  struct proc *p=myproc();
  if(p->pagePrintTraceOn==1)
  {
    printQueue();
  }
  
  int currentPage=p->pageIdxInHeadOfQueue;
  int firstInLoop=currentPage;
  int firstTime=0;
  while(1)
  {
    if(p->pageFileTable[currentPage].isInUse==0 || p->pageFileTable[currentPage].isInFile==1)
    {
      panic("scfifo choose: can't choose page not in use or in file1");
    }
    if(currentPage==START_OF_QUEUE || currentPage==END_OF_QUEUE)
    {
        panic("scfifo choose: Illegal current page");
    }
    
    pte_t *pte=walk(p->pagetable,p->pageFileTable[currentPage].va,0);
    if(pte==0)
    {
      panic("scfifo choose: no pte");
    }
    if(*pte&PTE_A)
    {
      //printf("page %d accessed, his next is: %d, his prev is: %d\n",currentPage,p->pageFileTable[currentPage].nextLink,p->pageFileTable[currentPage].prevLink);
      if(firstTime!=0 && currentPage==firstInLoop)
      {
        panic("scfifo choose: full loop");
      }
      *pte&=~PTE_A;

      dequeue(currentPage);
      enqueue(currentPage);
      if(p->pagePrintTraceOn)
      {
        printQueue();
      }

      // printf("after enqueue and dequeue\n");
      // printQueue();
      // printf("\n");
      
      currentPage=p->pageIdxInHeadOfQueue;
      firstTime=1;
    }
    else
    {
      return currentPage;
    }
  }
  
}

void print64Binary(uint64 number)
{
  uint64 currnumber=number;
  //printf("number: %p\n",number);
  for (int i = 0; i < 64; i++)
  {
    uint64 check=currnumber&0x8000000000000000;
    if(check==0)
    {
      printf("0");
    }
    else
    {
      printf("1");
    }
    currnumber=(uint64)(currnumber<<1);
  }
  printf("\n");
  
}

void PrintNFUcounters()
{
  struct proc *p=myproc();
  printf("---NFU Counters:---------\n");
  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    if(p->pageFileTable[i].isInUse==1)
    {
      if(p->pageFileTable[i].isInFile==0)
      {
        printf("Page %d NFU counter: \t",i);
        print64Binary(p->pageFileTable[i].counter_NFU);
      }
      else
      {
        printf("Page %d: IN FILE\n",i);
      }
    }
  }
  printf("---------------------\n");
}


void PrintLAPAcounters()
{
  struct proc *p=myproc();
  printf("---LAPA Counters:---------\n");
  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    if(p->pageFileTable[i].isInUse==1)
    {
      if(p->pageFileTable[i].isInFile==0)
      {
        printf("Page %d LAPA counter: \t",i);
        print64Binary(p->pageFileTable[i].counter_LA);
      }
      else
      {
        printf("Page %d: IN FILE\n",i);
      }
    }
  }
  printf("---------------------\n");
}


void printQueue()
{
  struct proc *p=myproc();
  int count=0;
  //printf("--------- The queue:------------\n");
  int currentPageIdx=p->pageIdxInHeadOfQueue;
  printf("HEAD -> ");
  while(currentPageIdx!=END_OF_QUEUE && count<MAX_PSYC_PAGES)
  {
    printf("%d -> ",currentPageIdx);
    currentPageIdx=p->pageFileTable[currentPageIdx].nextLink;
    count++;
  }
  printf("TAIL\n");
  //printf("--------------------------------\n\n");


}

int NFU_choose()
{
  struct proc *p=myproc();
  
  int isFirst=1;
  
  int minCounter;
  int minCounterIdx=-1;
  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    if(p->pageFileTable[i].isInUse==1 && p->pageFileTable[i].isInFile==0)
    {
      if(isFirst)
      {
        minCounter=p->pageFileTable[i].counter_NFU;
        minCounterIdx=i;
      }
      else
      {
        if(p->pageFileTable[i].counter_NFU<minCounter)
        {
          minCounter=p->pageFileTable[i].counter_NFU;
          minCounterIdx=i;
        }
      }
      
    }
  }

  return minCounterIdx;
}


int countOnes(int number)
{
  int tempNum=number;
  int counter=0;
  for (int i = 0; i < 64; i++)
  {
    if((tempNum&1)==1)
    {
      counter++;
    }
    tempNum=tempNum>>1;
  }
  return counter;
}


int LAPA_choose2()
{
  struct proc *p=myproc();
  
  int isFirst=1;
  
  int minOnes;
  int minCounter;
  int minIdx=-1;

  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    if(p->pageFileTable[i].isInUse==1 && p->pageFileTable[i].isInFile==0)
    {
      if(isFirst)
      {
        minOnes=countOnes(p->pageFileTable[i].counter_LA);
        minCounter=p->pageFileTable[i].counter_LA;
        minIdx=i;
      }
      else
      {
        int currOnes=countOnes(p->pageFileTable[i].counter_LA);
        if(currOnes<minOnes)
        {
          minOnes=currOnes;
          minIdx=i;
          minCounter=p->pageFileTable[i].counter_LA;
        }
        else if (currOnes==minOnes)
        {
          if(p->pageFileTable[i].counter_LA<minCounter)
          {
            minOnes=currOnes;
            minIdx=i;
            minCounter=p->pageFileTable[i].counter_LA;
          }
        }
      }
    }
  }
  return minIdx;

}

int LAPA_choose()
{
  struct proc *p=myproc();
  
  int isFirst=1;
  int allTheSame=1;
  
  int minCounter;
  int minOnesCounter;
  int minCounterIdx=-1;
  int minOnesCounterIdx=-1;
  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    if(p->pageFileTable[i].isInUse==1 && p->pageFileTable[i].isInFile==0)
    {
      if(isFirst)
      {
        minCounter=p->pageFileTable[i].counter_NFU;
        minCounterIdx=i;
        minOnesCounter=countOnes(p->pageFileTable[i].counter_LA);
        minOnesCounterIdx=i;
      }
      else
      {
        if(allTheSame)
        {
          if(countOnes(p->pageFileTable[i].counter_LA)==minOnesCounter)
          {
            if(p->pageFileTable[i].counter_LA<minCounter)
            {
              minCounter=p->pageFileTable[i].counter_LA;
              minCounterIdx=i;
            }
          }
          else
          {
            allTheSame=0;
          }
        }
        
        if(allTheSame==0)
        {
          if(countOnes(p->pageFileTable[i].counter_LA)<minOnesCounter)
          {
            minOnesCounter=countOnes(p->pageFileTable[i].counter_LA);
            minOnesCounterIdx=i;
          }
        }
      }
      
    }
  }
  if(allTheSame==1)
  {
    return minCounterIdx;
  }
  else
  {
    return minOnesCounterIdx;
  }
}



//Swap out from memory
int choosePageNumToSwap()
{
  #ifdef NONE
    return -1;

  #else

  #ifdef SCFIFO
    return scfifoChoose();

  #else

  #ifdef NFUA
    return NFU_choose();

  #else
  
  #ifdef LAPA
    return LAPA_choose2();

  #endif
  #endif
  #endif
  #endif

  
}


//swap out from ram to disk only
int swapOut()
{
  struct proc *p=myproc();
  p->wasLastPageFault = 1;

  int pageNumInOurStructToSwap=choosePageNumToSwap(); // choose number of page to move to disk
  if(p->pagePrintTraceOn)
  {
    printf("swap out: choosing page number %d to swap out\n",pageNumInOurStructToSwap);
  }
  if(pageNumInOurStructToSwap<0)
  {
    panic("Page fault in swap");
  }

  dequeue(pageNumInOurStructToSwap);

  

  #ifdef SCFIFO
  if(p->pagePrintTraceOn)
  {
    printQueue();
  }
  #endif
  
  uint64 theVAtoChange=p->pageFileTable[pageNumInOurStructToSwap].va;
  
  uint64 pa=walkaddr(p->pagetable,theVAtoChange); //pa of page on ram

  if(pa<=0)
  {
    panic ("walk in swap");
  }

  int fileAddr=-1;
  //Find free location insdie the file (on disk)
  for (int i = 0; i < MAX_PSYC_PAGES+1 && fileAddr==-1; i++)
  {
    if(p->fileAddressPool[i]!=1)
    {
      p->fileAddressPool[i]=1;
      fileAddr=PGSIZE*i;
      p->pageFileTable[pageNumInOurStructToSwap].fileLocation=fileAddr;
      p->pageFileTable[pageNumInOurStructToSwap].idxInFile=i;
    }
  }
  if(fileAddr<0)
  {
    printPageFileTableStruct(p->pagetable);
    panic("no place on file2");
  }
  
  writeToSwapFile(p,(char*)pa,fileAddr,PGSIZE);
  
  p->pageFileTable[pageNumInOurStructToSwap].isInFile=1;
  kfree((void *)pa);
  
  pte_t* pte=walk(p->pagetable,theVAtoChange,0);
  *pte |=PTE_PG;
  *pte &= ~PTE_V;

  
  return pageNumInOurStructToSwap;
}


int checkIfNeedToSwapOut()
{
  struct proc *p=myproc();
  int isThereNeedToSwapOut=0;

  int countRAM=0;

  for (int i = 0; i < MAX_TOTAL_PAGES && isThereNeedToSwapOut==0; i++)
  {
    if(countRAM+1==MAX_PSYC_PAGES)
    {
      isThereNeedToSwapOut=1;
    }
    else
    {
      if(p->pageFileTable[i].isInUse==0)
      {
        isThereNeedToSwapOut=0;
      }
      else
      {
        if(p->pageFileTable[i].isInFile==0)
        {
          countRAM++;
        }
      }
    }
  }
  return isThereNeedToSwapOut;
}

//the argument is the number of page we need, that we wan't to bring from the disk
int swapInFromDisk(int pageIdxInProc)
{
  
  struct proc *p=myproc();
  
  

  if(checkIfNeedToSwapOut())
  {
  //write
    swapOut();
  }

  //read page in and free page in file
  char *mem;
  mem = kalloc();
  if(mem == 0){
    panic("not enough memory in swap in");
  }
  memset(mem, 0, PGSIZE);
  pte_t *pteToGetFromDisk=walk(p->pagetable,p->pageFileTable[pageIdxInProc].va,0);
  if(pteToGetFromDisk==0)
  {
    panic("not pte for page in swap in");
  }
  uint perm=PTE_FLAGS(*pteToGetFromDisk);
  perm |=PTE_V;
  perm &= ~PTE_PG;

  *pteToGetFromDisk = PA2PTE(mem) | perm;
  uint fileLocationOfPageThatWeBringBackFromDisk=p->pageFileTable[pageIdxInProc].fileLocation;

  int ansRead=readFromSwapFile(p,mem,fileLocationOfPageThatWeBringBackFromDisk,PGSIZE);
  if(ansRead<0)
  {
    panic("read page back swap in");
  }
  p->pageFileTable[pageIdxInProc].isInFile=0;
  int idxInAddressPool=p->pageFileTable[pageIdxInProc].idxInFile;
  p->fileAddressPool[idxInAddressPool]=0;
  
  enqueue(pageIdxInProc);

  p->pageFileTable[pageIdxInProc].counter_NFU=0;
  p->pageFileTable[pageIdxInProc].counter_LA=0xffffffffffffffff;
  
  //printf("some to test: %p\n",p->pageFileTable[pageIdxInProc].counter_LA);

  return 1;
}


// -1   : error
// 100  : temp page added by exec, no need to add to meta data
int 
addPageToProc(uint64 va, pagetable_t pagetable)
{
  
  struct proc *p=myproc();
  //printPageFileTableStruct(p->pagetable);
  

  if(p->pid<3)
  {
    return 100;
  }

  //printf("pid: %d, page tables: %d\n",p->pid,p->PageTablesInUse);
  if(p->pagePrintTraceOn==1)
  {
    printf("adding new page to process: %d\n\n",p->PageTablesInUse);
  }  
  //printf("pid: %d, pages in use: %d, va: %d\n",p->pid,p->PageTablesInUse,va);
  if(p->PageTablesInUse>=MAX_TOTAL_PAGES)
  {
    panic("Too many pages in proc");
  }
  int pageToAdd=-1;

  #ifdef SCFIFO
  if(p->pagePrintTraceOn)
  {
    printf("-----Queue changes:-------\n");
  }
  #endif

  if(p->pagetable==pagetable)
  {
    if(p->PageTablesInUse>=MAX_PSYC_PAGES)
    {
      if(checkIfNeedToSwapOut())
      {
      
        swapOut();
      }
    }
    
    for (int i = 0; i < MAX_TOTAL_PAGES && pageToAdd==-1; i++)
    {
      if(p->pageFileTable[i].isInUse==0)
      {
        pageToAdd=i;        
      }
    }

    if(pageToAdd<0)
    {
      panic("Swap logic");
    }
    p->pageFileTable[pageToAdd].isInUse=1;
    p->pageFileTable[pageToAdd].isInFile=0;
    p->pageFileTable[pageToAdd].va=va;
    p->PageTablesInUse+=1;
  }
  else
  {
    // of exec, new pagetable, the pages will be added later
    pageToAdd=100;
  }
  

  //only for scfifo
  if(pageToAdd!=-1 && pageToAdd!=100)
  {
    
    enqueue(pageToAdd);
    #ifdef SCFIFO
    if(p->pagePrintTraceOn)
    {
      printQueue();
      printf("-----End of Queue changes:-------\n");
    }
    #endif
    p->pageFileTable[pageToAdd].counter_NFU=0;
    p->pageFileTable[pageToAdd].counter_LA=0xffffffffffffffff;
  }
  // printQueue("\n----Queue after add----\n");
  // printQueue();
  return pageToAdd;
}


void printPageFileTableStructForProc(struct proc *p)
{
  printf("**************************\n");
  if(p->pid>=3)
  {
    for (int i = 0; i < MAX_TOTAL_PAGES; i++)
    {
      
      int currPa=walkaddr(p->pagetable,p->pageFileTable[i].va);
      pte_t *pte=walk(p->pagetable,p->pageFileTable[i].va,0);
      //printf("Calling walkaddr with: va: %p, currPa: %p\n",p->pageFileTable[i].va,currPa);
      printf("Page number: %d, current physical address: %p, va: %p, isInUse: %d, isInFile: %d, location in file: %p, PTE_PG: %d, PTE_V: %d\n",i,currPa,p->pageFileTable[i].va/PGSIZE, p->pageFileTable[i].isInUse,p->pageFileTable[i].isInFile, p->pageFileTable[i].fileLocation, (*pte&PTE_PG),(*pte&PTE_V));
    }
    
  }
  printf("**************************\n");
}

void printPageFileTableStruct(pagetable_t pagetable)
{
  printf("**************************\n");
  struct proc *p=myproc();
  if(p->pid>=3)
  {
    for (int i = 0; i < MAX_TOTAL_PAGES; i++)
    {
      
      int currPa=walkaddr(p->pagetable,p->pageFileTable[i].va);
      pte_t *pte=walk(p->pagetable,p->pageFileTable[i].va,0);
      //printf("Calling walkaddr with: va: %p, currPa: %p\n",p->pageFileTable[i].va,currPa);
      printf("Page number: %d, current physical address: %p, va: %p, isInUse: %d, isInFile: %d, location in file: %p, PTE_PG: %d, PTE_V: %d\n",i,currPa,p->pageFileTable[i].va/PGSIZE, p->pageFileTable[i].isInUse,p->pageFileTable[i].isInFile, p->pageFileTable[i].fileLocation, (*pte&PTE_PG),(*pte&PTE_V));
    }
    
  }
  printf("**************************\n");
}

void 
freePageToProc(int procPageID, int isFromExec)
{
  //printf("free to proc: %d\n",procPageID);
  struct proc *p=myproc();
  if(p->pid<3)
  {
    return;
  }
  if(procPageID>31 || procPageID<0)
  {
    panic("free page to proc");
  }
  
  if(!isFromExec && p->pageFileTable[procPageID].isInUse==0)
  {
    panic("freeing proc not in use");
  }
  if(p->pageFileTable[procPageID].isInFile)
  {
    int idxInPool=p->pageFileTable[procPageID].idxInFile;
    //printf("free page pool idx: %d\n",idxInPool);
    p->fileAddressPool[idxInPool]=0;
  }
  else
  {
    //not in file, in ram
    //scfifo
    if(!isFromExec)
    {
      dequeue(procPageID);
    }
  }
  p->pageFileTable[procPageID].isInUse=0;
  p->pageFileTable[procPageID].fileLocation=0;
  p->pageFileTable[procPageID].isInFile=0;
  p->pageFileTable[procPageID].va=0;
  p->pageFileTable[procPageID].nextLink=-1;
  p->pageFileTable[procPageID].prevLink=-1;
  p->pageFileTable[procPageID].counter_LA=0xffffffffffffffff;
  p->pageFileTable[procPageID].counter_NFU=0;
}

int freepage(uint64 va, pagetable_t pagetable)
{
  struct proc *p=myproc();
  if(pagetable!=p->pagetable)
  {
    return 1;
  }
  if(p->pid>2)
  {
    if(p->PageTablesInUse==0)
    {
      printf("pid: %d\n",p->pid);
      panic("Pages in process under zero");
    }
    int found=-1;
    for (int i = 0; i < MAX_TOTAL_PAGES && found!=1; i++)
    {
      if(p->pageFileTable[i].va== va && p->pageFileTable[i].isInUse == 1)
      {
        freePageToProc(i,0);
        found=1;
      }
    }
    if(found==1)
    {
      p->PageTablesInUse-=1;
    }
    return found;
  }
  return 0;  
}


int CheckIfInFile(uint64 va)
{
  struct proc *p=myproc();
  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    if(p->pageFileTable[i].va==va)
    {
      if(p->pageFileTable[i].isInUse && p->pageFileTable[i].isInFile)
      {
        return 1;
      }
      else if(p->pageFileTable[i].isInUse)
      {
        return 0;
      }
      else
      {
        return -1;
      }      
    }
  }
  return -1;
}

void enqueue(int pageIdxInMetaData)
{
  struct proc *p=myproc();

  if(p->pageFileTable[pageIdxInMetaData].isInFile==0)
  {
  //pagelinks explain:
      //  5000   : points to the end of the queue
      // -5000   : points to the head of the queue
      if(p->pageIdxInHeadOfQueue==START_OF_QUEUE && p->pageIdxInTailOfQueue==END_OF_QUEUE)
      {
        //Page added to queue when it is empty
        p->pageIdxInHeadOfQueue=pageIdxInMetaData;
        p->pageIdxInTailOfQueue=pageIdxInMetaData;
        p->pageFileTable[pageIdxInMetaData].nextLink=END_OF_QUEUE;
        p->pageFileTable[pageIdxInMetaData].prevLink=START_OF_QUEUE;
        //printf("page %d is first in queue: next: %d, prev: %d\n",pageIdxInMetaData,
        //p->pageFileTable[pageIdxInMetaData].nextLink,p->pageFileTable[pageIdxInMetaData].prevLink);
      }
      else if(p->pageIdxInTailOfQueue==START_OF_QUEUE || p->pageIdxInHeadOfQueue==END_OF_QUEUE)
      {
        panic("scfifo queue logic");
      }
      else
      {
        int currentPageInTheEndOfTheQueue=p->pageIdxInTailOfQueue;
        if(currentPageInTheEndOfTheQueue==-1)
        {
          panic("scfifo queue, not suppose to be empty");
        }
        p->pageFileTable[currentPageInTheEndOfTheQueue].nextLink=pageIdxInMetaData;
        p->pageIdxInTailOfQueue=pageIdxInMetaData;
        p->pageFileTable[pageIdxInMetaData].nextLink=END_OF_QUEUE;
        p->pageFileTable[pageIdxInMetaData].prevLink=currentPageInTheEndOfTheQueue;

       // printf("new page to process %d is in queue: next: %d, prev: %d\n",pageIdxInMetaData,
        //p->pageFileTable[pageIdxInMetaData].nextLink,p->pageFileTable[pageIdxInMetaData].prevLink);

       // printf("page that was in tail %d is in queue: next: %d, prev: %d\n",currentPageInTheEndOfTheQueue,
        //p->pageFileTable[currentPageInTheEndOfTheQueue].nextLink,p->pageFileTable[currentPageInTheEndOfTheQueue].prevLink);
      }
  }
}

void dequeue(int pageIdxInMetaData)
{
    struct proc *p=myproc();
    
    int prevToThisPage=p->pageFileTable[pageIdxInMetaData].prevLink;
    int nextToThisPage=p->pageFileTable[pageIdxInMetaData].nextLink;

    //printf("prev of page %d is %d\n",pageIdxInMetaData,prevToThisPage);
    
    if(prevToThisPage==START_OF_QUEUE && nextToThisPage==END_OF_QUEUE)
    {
      //the only one in the queue
      p->pageIdxInHeadOfQueue=START_OF_QUEUE;
      p->pageIdxInTailOfQueue=END_OF_QUEUE;
    }
    else if(prevToThisPage==START_OF_QUEUE)
    {
      
      p->pageFileTable[nextToThisPage].prevLink=START_OF_QUEUE;
      p->pageIdxInHeadOfQueue=nextToThisPage;
    }
    else if(prevToThisPage==END_OF_QUEUE)
    {
      
      p->pageFileTable[prevToThisPage].nextLink=END_OF_QUEUE;
      p->pageIdxInTailOfQueue=prevToThisPage;
    }
    else
    {
      p->pageFileTable[prevToThisPage].nextLink=nextToThisPage;
      p->pageFileTable[nextToThisPage].prevLink=prevToThisPage;
    }    
}

void freeAllProcessMetaData()
{
  struct proc *p=myproc();
  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    freePageToProc(i,1);
  }
  
  p->PageTablesInUse=0;
  removeSwapFile(p);
  createSwapFile(p);
  p->pageIdxInHeadOfQueue=START_OF_QUEUE;
  p->pageIdxInTailOfQueue=END_OF_QUEUE;

}



void updateNFU()
{
  struct proc *p=myproc();
  if(p->pid<3)
  {
    return;
  }
  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    if(p->pageFileTable[i].isInUse && p->pageFileTable[i].isInFile==0)
    {
      pte_t *pte=walk(p->pagetable,p->pageFileTable[i].va,0);
      if(pte==0)
      {
        panic("update counters: no pte");
      }
      p->pageFileTable[i].counter_NFU = p->pageFileTable[i].counter_NFU >> 1;
      if(*pte&PTE_A)
      {
        p->pageFileTable[i].counter_NFU |= 0x8000000000000000;
        *pte &= ~PTE_A;
      }
    }
  }
  #ifdef NFUA
  if(p->pagePrintTraceOn==1)
  {
    PrintNFUcounters();
  }
  #endif
}

void updateLAPA()
{
  struct proc *p=myproc();
  if(p->pid<3)
  {
    return;
  }
  for (int i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    if(p->pageFileTable[i].isInUse && p->pageFileTable[i].isInFile==0)
    {
      pte_t *pte=walk(p->pagetable,p->pageFileTable[i].va,0);
      if(pte==0)
      {
        panic("update counters: no pte");
      }
      p->pageFileTable[i].counter_LA =(uint64) (p->pageFileTable[i].counter_LA >> 1);
      if(*pte&PTE_A)
      {
        p->pageFileTable[i].counter_LA |= 0x8000000000000000;
        *pte &= ~PTE_A;
      }
      //printf("counter lapa for page %d: %p\n",i,p->pageFileTable[i].counter_LA);
    }
  }
  #ifdef LAPA
  if(p->pagePrintTraceOn)
  {
    PrintLAPAcounters();
  }
  #endif
}

void
updateCounters()
{
  updateNFU();
  updateLAPA();  
}

int
count_pages(){
  struct proc *p = myproc();
  return p->PageTablesInUse;
}

int
pagefault(){
  struct proc *p = myproc();
  int last = p->wasLastPageFault;
  p->wasLastPageFault=0;
  return last;
}

int
flipTrace()
{
  struct proc *p=myproc();
  if(p->pagePrintTraceOn==0)
  {
    p->pagePrintTraceOn=1;
    printf("trace %d\n",p->pagePrintTraceOn);
  }
  else
  {
    p->pagePrintTraceOn=0;
  }
  return p->pagePrintTraceOn;
  

}
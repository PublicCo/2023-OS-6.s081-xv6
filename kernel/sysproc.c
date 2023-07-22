#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  // get start addr, page num and buffer
  uint64 st_addr,muskaddr;
  int len;
  if(argaddr(0,&st_addr)<0){
    return -1;
  }
  if(argint(1,&len)<0){
    return -1;
  }
  if(argaddr(2,&muskaddr)<0){
    return -1;
  }
  
  // get the physics address
  pagetable_t pagetable = myproc()->pagetable;
  pte_t* first_physic_addr = walk(pagetable,st_addr,0);
  printf("\n\n");
  vmprint(pagetable,2);
  printf("\n");
  printf("%p and %p\n\n",muskaddr,&muskaddr);
  //read over all the pagetable that need to check
  uint result=0;
  //only check 64 bits of pages
  if(len>32){
    return -1;
  }

  for(int i=0;i<len;i++){
    if((first_physic_addr[i]&PTE_A)&&(first_physic_addr[i]&PTE_V)){
      result|=1<<i;
      //reset pte_A to avoid loop checking
      first_physic_addr[i]^=PTE_A;
    }
  }
  printf("result = %ud\n",result);
  //copy data from kernel to user
  if(copyout(pagetable,muskaddr,(char*)&result,sizeof(uint))<0){
    return -1;
  }
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

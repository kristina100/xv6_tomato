#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void) // 全局陷入系统初始化
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void) // 硬件线程hart陷入初始化
{
  w_stvec((uint64)kernelvec); // w_stvec: 写入stvec寄存器的函数    kernelvec： 指向内核陷入处理程序
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
// 从用户态发生的系统调用、硬件中断或者异常
void
usertrap(void)
{
  int which_dev = 0; // 用于表示哪种设备触发了中断

  if((r_sstatus() & SSTATUS_SPP) != 0) // 状态寄存器和该寄存器中的标志位，如果该位为1，则表示发生在内核态，不应该在此函数处理
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec); // 将内核陷入向量 kernelvec 的地址写入 stvec 寄存器

  struct proc *p = myproc(); 
  
  // save user program counter.
  p->trapframe->epc = r_sepc();  // 保护用户程序计数器
  
  if(r_scause() == 8){ // 8 表示系统调用
    // system call

    if(p->killed) // 如果当前进程标记为killed，那么直接退出
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4; // 将程序计数器向前移动4字节（一条指令的长度），跳过系统调用指令ecall；以便在返回用户态时继续执行下一条指令

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on(); // 打开中断

    syscall(); // 调用系统调用处理程序
  } else if((which_dev = devintr()) != 0){
    // ok
  } else { // 既不是系统调用也不是已知设备中断，则打印未知中断信息
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1; // 并标记该进程被killed
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
// 将当前进程从内核态切换回用户态
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off(); // 关闭中断

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline)); // 设置stvec寄存器，使其指向trampoline.S文件中的uservec

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table 保存内核页表
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack 保存内核栈指针
  p->trapframe->kernel_trap = (uint64)usertrap; // 保存内核陷入处理函数
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid() 保存硬件线程ID

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus(); // 读取sstatus寄存器的值
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode 清除状态位，返回用户态
  x |= SSTATUS_SPIE; // enable interrupts in user mode 设置状态位，允许用户态下启用中断
  w_sstatus(x); // 写回sstatus寄存器

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc); // 将用户程序计数器的值写入sepc寄存器，使系统直到在切换回用户态时应该从哪条指令继续执行

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable); // 设置用户页表

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline); 
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp); // 跳转到trampoline.S，恢复用户态的寄存器，并最终切换到用户态执行
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
// 内核态陷入处理函数
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}


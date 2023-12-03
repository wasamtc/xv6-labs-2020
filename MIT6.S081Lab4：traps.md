# RISC-V assembly 

阅读***call.asm***以及参考手册回答下列问题：

```
问：哪些寄存器保存函数的参数？例如，在main对printf的调用中，哪个寄存器保存13？
答：a0~a7保存函数参数，a2保存13

问：main的汇编代码中对函数f的调用在哪里？对g的调用在哪里
答：都内联了

问：printf函数位于哪个地址
答：0x630，也就是ra+1536，注意jalr时的ra为0x30

问：main中printf的jalr之后的寄存器ra中有什么值
答：jalr是跳转到ra加上偏移得到的地址，并且把ra置为pc+4，即0x38

问：运行以下代码
unsigned int i = 0x00646c72;
printf("H%x Wo%s", 57616, &i);
输出是什么
答：输出为He110 World，如果是大端存储的话，i值应该为0x726c6400。

问：在下面的代码中，“y=”之后将打印什么(注：答案不是一个特定的值）？为什么会发生这种情况？
printf("x=%d y=%d", 3);
答：取决于寄存器a2是什么值，因为处理器会以为第二个参数存放在a2中
```

# Backtrace

只要懂了这张图，基本就能写出来了。

![img](https://github.com/duguosheng/6.S081-All-in-one/raw/main/labs/requirements/images/p2.png)

从s0中获得当前栈帧的fp，然后-8得到返回地址打印，-16得到上一级的栈帧重复这个过程。

循环的条件就是fp要在页面顶端-16和页面底端+16的区域，如果顶端没有16了说明没有上一级了，如果底端没有16了说明当前这一级不完整。当然只需要判断是否在顶端-16下即可，毕竟fp一直是往高地址方向跳。代码如下：

```cpp
void
backtrace(void)
{
  uint64 fp = r_fp();
  uint64 begin = PGROUNDDOWN(fp);
  uint64 end = PGROUNDUP(fp);
  printf("backtrace:\n");
  while (fp >= (begin + 16) && fp <= (end - 16)) {
    uint64 ret = *(uint64*)(fp - 8);
    printf("%p\n", ret);
    fp = *(uint64*)(fp - 16);
  }
}
```

在qemu中运行bttest，得到

```
backtrace:
0x0000000080002d58
0x0000000080002bba
0x00000000800028a4
```

然后退出qemu，执行`addr2line -e kernel/kernel`命令，并把上面的地址复制粘贴上去，得到

```
0x0000000080002d58
/home/wasam/MIT6.S081/xv6-labs-2020/kernel/sysproc.c:74
0x0000000080002bba
/home/wasam/MIT6.S081/xv6-labs-2020/kernel/syscall.c:140
0x00000000800028a4
/home/wasam/MIT6.S081/xv6-labs-2020/kernel/trap.c:76
```

完成。在kernel/printf.c的panic函数中添加backtrace即可。

# Alarm

## test0

首先完成test0，按照提示所说，添加系统调用以及proc结构体。注意proc结构体中的handler函数指针用一个uint64字段存储。

proc结构体如下：

```cpp
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  struct proc *parent;         // Parent process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  int interval;                // every interval invoke handler
  uint64 handler;              
  int nextticks;               // remain nextticks to invoke handler
};
```

在我们的系统调用函数中存储两个传入的字段，通过alarmtest.asm可以看到两个参数分别存储在a0，a1中。

```cpp
uint64
sys_sigalarm(void)
{
  struct proc *p = myproc();
  if (argint(0, &p->interval)) {
    return -1;
  }
  // printf("good\n");
  if (argaddr(1, &p->handler)) {
    // printf("bad\n");
    return -1;
  }
  // printf("good\n");
  if (p->interval == 0 && p->handler == 0) {
    p->interval = -1;
  }
  p->nextticks = p->interval;
  return 0;
}
```

然后是在我们的计时器中断时判断是否到达了interval个ticks来调用handler，注意，调用handler不是直接在内核中调用，因为handler的指针是用户空间的，内核的页表无法翻译，所以要把返回用户空间要执行的指令的地址更改为handler的地址，即修改`p->trapframe->epc`

```cpp
else if((which_dev = devintr()) != 0){
    // ok
    if(which_dev == 2) {
      if (p->interval != -1) {
        --(p->nextticks);
        if (p->nextticks == 0) {
          // ((void(*)())0)();
          p->trapframe->epc = p->handler;
          // (*(p->handler))();
          p->nextticks = p->interval;
        }
      }
    }
  }
```

运行`alarmtest`可得到如下结果

```
$ alarmtest
test0 start
....................................................alarm!
test0 passed
test1 start
......alarm!
.....alarm!
......alarm!
.....alarm!
......alarm!
.....alarm!
.....alarm!
......alarm!
.....alarm!
......alarm!

test1 failed: foo() executed fewer times than it was called

test1 failed: foo() executed fewer times than it was called
usertrap(): unexpected scause 0x000000000000000c pid=3
            sepc=0xfffffffffffffb08 stval=0xfffffffffffffb08
```

可以看到test0是通过了的。

## test1/2

从前面一个实验我们也可以看到，当我们跳转到test0的handler后，如何跳转回test0的下一条指令是个问题。即跳转到处理函数后，如何跳转回原有函数应该执行的指令地址。

我们设计了一个返回的系统调用，通过这个系统调用来返回正确的指令地址。要正确返回，首先我们在从内核跳转到处理函数的时候就应该保存相应的寄存器状态，这些状态存储在`trapframe`中，我们可以在`proc`结构体中新加一个字段来对`trapframe`做一个备份，然后在返回的系统调用函数中用这个备份覆盖`TRAPFRAME`，这样系统调用返回到`trap`中时寄存器已恢复到相应的状态从而可以跳转到最开始函数对应的指令地址。同时再加一个字段判断是否正在执行处理函数，如果是则不能再通过计时中断来调用处理函数了。

`proc`结构体如下

```cpp
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  struct proc *parent;         // Parent process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  int interval;                // every interval invoke handler
  uint64 handler;              
  int nextticks;               // remain nextticks to invoke handler
  int inhandle;                // handling
  struct trapframe reservetrapframe; // reserved trapframe
};
```

`usertrap`如下

```cpp
else if((which_dev = devintr()) != 0){
    // ok
    if(which_dev == 2) {
      if (p->interval != -1 && p->inhandle == 0) {
        --(p->nextticks);
        if (p->nextticks == 0) {
          p->inhandle = 1;
          p->reservetrapframe = *(p->trapframe);
          p->trapframe->epc = p->handler;
          p->nextticks = p->interval;
        }
      }
    }
  }
```

`sys_sigreturn`如下

```cpp
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  *(p->trapframe) = p->reservetrapframe;
  p->inhandle = 0;
  return 0;
}
```

运行`alarmtest`

```
$ alarmtest
test0 start
............................................................alarm!
test0 passed
test1 start
......alarm!
.....alarm!
......alarm!
......alarm!
.....alarm!
......alarm!
......alarm!
......alarm!
.....alarm!
......alarm!
test1 passed
test2 start
....................................................................alarm!
test2 passed
```

运行`usertests`，最后得到`ALL TESTS PASSED`，编写成功！

# 总结

总的来说，lab4并不是很难，只要理解了trap的机制以及用户空间与内核在trap机制中如何转换，应该是能够写出来的。可能需要一点汇编代码的理解能力，不过应该还是能大致看懂需要用到的汇编的。在test0中我在`usertrap`直接执行处理函数，忽略了用户和内核页表的不一致导致出错，这一点需要注意，尤其是解引用的时候。

另外补充一点，在copyin中我们遍历用户空间的页表找到物理地址，然后使用memmove将物理地址对应的内容复制给了一个虚拟地址，我之前一直有疑惑为什么能够直接去找物理地址对应的内容，因为这里的物理地址还是按照内核页表翻译的，只是内核页表的free memory是恒等映射的，所以能够翻译成功！

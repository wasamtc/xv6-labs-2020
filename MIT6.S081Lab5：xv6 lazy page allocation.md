# Eliminate allocation from sbrk() 

这个相当简单，在sys_sbrk中对growproc的调用注释掉就行，然后把sz加上n。这样应该分配的页面没有分配，同时也没有映射到页表，所以使用这些分配的页面的时候会报错`panic: uvmunmap: not mapped`。

sys_sbrk如下：

```cpp
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  myproc()->sz += n;
  // if(growproc(n) < 0)
  //   return -1;
  return addr;
}
```

运行`echo hi`结果如下

```
$ echo hi
usertrap(): unexpected scause 0x000000000000000f pid=3
            sepc=0x00000000000012ac stval=0x0000000000004008
panic: uvmunmap: not mapped
```

# Lazy allocation

这个实验也还是比较简单，尤其是课程上已经有一些展示了，按照提示所说，在`usertrap`的最后一个else前加上处理页面错误的语句，分配一个新页面，置零，映射到页表上，然后重新执行指令，重新执行指令不需要做什么，因为此时`p->trapframe->epc`存储的就是引发错误的指令。不过这个实验好像没有考虑sbrk减少堆空间的情况。

`usertrap`新增如下

```cpp
else if (r_scause() == 13 || r_scause() == 15) {
    uint64 va = r_stval();
    uint64 begin = PGROUNDDOWN(va);
    pagetable_t pagetable = p->pagetable;
    char *mem = kalloc();
    if(mem == 0){
      kfree(mem);
      exit(-1);
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, begin, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      exit(-1);
    }
    vmprint(pagetable);
  }
```

因为释放进程内存的时候会通过页表释放堆空间，而很多堆空间并没有实际分配，即有效位为0，所以在`uvmunmap`中会panic，我们把相应的改为continue即可。

```cpp
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      continue;
      // panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}
```

运行`echo hi`，可以看到

```
$ echo hi
page table 0x0000000087f75000
..0: pte 0x0000000021fdc801 pa 0x0000000087f72000
.. ..0: pte 0x0000000021fd9401 pa 0x0000000087f65000
.. .. ..0: pte 0x0000000021fdc05f pa 0x0000000087f70000
.. .. ..1: pte 0x0000000021fd98df pa 0x0000000087f66000
.. .. ..2: pte 0x0000000021fdc40f pa 0x0000000087f71000
.. .. ..3: pte 0x0000000021fd68df pa 0x0000000087f5a000
.. .. ..4: pte 0x0000000021fd641f pa 0x0000000087f59000
..255: pte 0x0000000021fdd001 pa 0x0000000087f74000
.. ..511: pte 0x0000000021fdcc01 pa 0x0000000087f73000
.. .. ..510: pte 0x0000000021fd90c7 pa 0x0000000087f64000
.. .. ..511: pte 0x0000000020001c4b pa 0x0000000080007000
page table 0x0000000087f75000
..0: pte 0x0000000021fdc801 pa 0x0000000087f72000
.. ..0: pte 0x0000000021fd9401 pa 0x0000000087f65000
.. .. ..0: pte 0x0000000021fdc05f pa 0x0000000087f70000
.. .. ..1: pte 0x0000000021fd98df pa 0x0000000087f66000
.. .. ..2: pte 0x0000000021fdc40f pa 0x0000000087f71000
.. .. ..3: pte 0x0000000021fd68df pa 0x0000000087f5a000
.. .. ..4: pte 0x0000000021fd64df pa 0x0000000087f59000
.. .. ..19: pte 0x0000000021fd601f pa 0x0000000087f58000
..255: pte 0x0000000021fdd001 pa 0x0000000087f74000
.. ..511: pte 0x0000000021fdcc01 pa 0x0000000087f73000
.. .. ..510: pte 0x0000000021fd90c7 pa 0x0000000087f64000
.. .. ..511: pte 0x0000000020001c4b pa 0x0000000080007000
hi
```

有两次页面错误。

# Lazytests and Usertests

这个我觉得有一定难度，如果没有提示的话很难考虑这么仔细。

按照提示一条条来

+ 处理`sbrk()`参数为负的情况。这个好处理，为负的情况依旧调用growproc处理即可：

  ```cpp
  uint64
  sys_sbrk(void)
  {
    int addr;
    int n;
    struct proc * p = myproc(); 
    if(argint(0, &n) < 0)
      return -1;
    addr = myproc()->sz;
    if(n > 0){
      p->sz = p->sz + n;
    }else{
      if (growproc(n) == -1) {
        return -1;
      }
    }
    return addr;
  }
  ```

+ 如果某个进程在高于`sbrk()`分配的任何虚拟内存地址上出现页错误，则终止该进程：页面错误中断的时候在分配新页面前检查是否高于p->sz。

+ 在`fork()`中正确处理父到子内存拷贝：这里的拷贝主要指的是`uvmcopy`，很简单，把这个函数里面前两个panic注释掉或者改成continue即可，因为在父进程中这些页也是没有实际存在的，子进程只要p->sz保持好的父进程一致就可以了。

+ 处理这种情形：进程从`sbrk()`向系统调用（如`read`或`write`）传递有效地址，但尚未分配该地址的内存。这个是最难的，我们可以发现read或write的系统调用最终用的是copyout或者copyin，而这两者在walkaddr中如果找的是还没有实际分配页面的地址，那么发挥的物理地址就是0，从而系统调用返回-1，系统调用失败，在这个过程中并没有产生页面错误直接失败了，所以不会进入陷阱机制处理页面错误。所以要处理这种情况的话我们就要在walkaddr里面直接把没有实际分配的页面分配了，就像在trap中处理页面错误一样。

+ 正确处理内存不足：如果在页面错误处理程序中执行`kalloc()`失败，则终止当前进程。：如果kalloc返回的是0，则终止进程。

+ 处理用户栈下面的无效页面上发生的错误。如果虚拟地址va小于了用户栈，说明访问了不该访问的内存，终止进程。

综上，我们可以在vm.c里面写一个函数来分配映射lazy allocation的页面，如下：

```cpp
// 注意：如果要在vm.c中使用proc结构体及其字段，要先加上"spinlock.h"，再加上"proc.h"
int lazyalloc(uint64 va) {
  struct proc *p = myproc();
  // 如果高于sbrk的地址或者低于用户栈地址，终止进程
  if (va >= p->sz || va <= PGROUNDDOWN(p->trapframe->sp)) {
      return -1;
    }
    // printf("page fault:%p\n", va);
    uint64 begin = PGROUNDDOWN(va);
    pagetable_t pagetable = p->pagetable;
    char *mem = kalloc();
    if(mem == 0){
      return -1;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, begin, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      return -1;
    }
    return 0;
}
// 需要在defs.h中加上这个函数的声明才能在其他地方使用这个函数
```

然后usertrap和walkaddr中调用这个函数

```cpp
// usertrap
else if (r_scause() == 13 || r_scause() == 15) {
    uint64 va = r_stval();
    if (lazyalloc(va) == -1) {
      p->killed = 1;
    }
  }
```

```cpp
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;
  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_V) == 0) {
    if (lazyalloc(va) == -1) {
      return 0;
    } else {
      pte = walk(pagetable, va, 0);
    }
  }
  //   return 0;
  // if((*pte & PTE_V) == 0)
  //   return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}
```

如果运行时出现了panic，把相应地方注释掉或者改为continue即可。

运行`lazytests`

```
$ lazytests
lazytests starting
running test lazy alloc
test lazy alloc: OK
running test lazy unmap
test lazy unmap: OK
running test out of memory
test out of memory: OK
ALL TESTS PASSED
```

运行`usertests`，可以看到所有测试通过，如果walkaddr编写的有问题的话，sbrkarg不会通过。

# 总结

这个实验总的来说我觉得还是比lab3简单一点，最需要注意的就是最后一个中关于系统调用涉及到lazy allocation不会触发页面错误需要手动编写代码来分配页面。

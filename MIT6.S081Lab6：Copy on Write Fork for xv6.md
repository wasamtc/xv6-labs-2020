# Copy-on-Write Fork for xv6

这个实验本身并不难，思路还是很清晰的，但是我想的有点多了。首先我纠结得最久的就是索引数组在哪里声明以及索引数组的大小。开始我想索引数组在哪里声明才能让所有进程都能访问到，如果声明在某个函数里面则随着函数的结束空间就释放了，所以我想申请在堆里，但是不切实际，毕竟malloc函数都没有，而且这个数组是关于内核本身的。最后才发现其实直接在某个文件里面定义成全局变量，就自动是放在内核的BSS段或者DATA段，其他文件中用extern声明访问即可。

然后是索引数组的大小了，开始我想极致地缩小数组大小，所以我想的是PHYSTOP-end，这个end就是kalloc.c中的那个end，后来我发现其他文件很难访问到这个end的大小，于是我想了一些其他的办法很复杂，于是干脆用PHYSTOP-0发现也行。

其他的就也要注意一些细节吧。kalloc的时候把物理地址对应的索引数组置为1，kfree的时候先减1，然后判断是否大于0来决定要不要释放页面。

```cpp
// kalloc
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    pagearray[(uint64)r/PGSIZE] = 1;
  }
  return (void*)r;
}
```

```cpp
// kfree
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  pagearray[(uint64)pa/PGSIZE]--;
  if (pagearray[(uint64)pa/PGSIZE] > 0) {
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

然后是uvmcopy函数，在这个里面不分配页面了，直接让新页表指向原来的物理地址并且更改两个PTE的flag，改为非写并且用CSW位来标识是cow对应PTE（其实不用CSW这个实验也能完成）。

```cpp
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  // char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    (*pte) = (*pte) & (~PTE_W);
    (*pte) = (*pte) | (PTE_CSW);
    flags = PTE_FLAGS(*pte);
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      goto err;
    }
    pagearray[(uint64)pa/PGSIZE]++;
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

然后是写一个函数用来处理页面因为cow导致的页面错误，就是判断PTE是否符合要求，分配页面，复制页面，尝试释放原来的页面，更改PTEflag，将PTE与物理地址map上。

```cpp
int
cowpage(pagetable_t pagetable, uint64 va) 
{
  if (va > MAXVA) {
    return -1;
  }
  va = PGROUNDDOWN(va);
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) {
    return -1;
  }
  if (((*pte) & PTE_CSW) == 0) {
    return -1;
  }

  char *mem = kalloc();
  if (mem == 0) {
    return -1;
  }
  uint64 pa = PTE2PA(*pte);
  uint flags = PTE_FLAGS(*pte);
  flags |= PTE_W;
  flags &= (~PTE_CSW);
  memmove(mem, (char*)pa, PGSIZE);
  uvmunmap(pagetable, va, 1, 1);
  // (*pte) = 0;
  // pagearray[pa/PGSIZE]--;
  // kfree((void*)pa);
  // *pte = PA2PTE(mem) | flags;
  if(mappages(pagetable, va, PGSIZE, (uint64)mem, flags) != 0) {
    kfree(mem);
    return -1;
  }
  return 0;
}
```

然后在usertrap和copyout中调用这个函数

```cpp
//usertrap
else if (r_scause() == 15) {
    uint64 va = r_stval();
    if (cowpage(p->pagetable, va) != 0) {
      p->killed = 1;
    }
  }
```

```cpp
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if (va0 > MAXVA) {
      return -1;
    }
    pte_t *pte = walk(pagetable, va0, 0);
    if (pte == 0)
      return -1;
    if (((*pte) & PTE_W) == 0) {
      if (cowpage(pagetable, va0) != 0)
        return -1;
    }
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

在defs.h中加上相应定义。

最后运行`cowtest`和`usertests`通过。

# 总结

实验不难，但是要仔细，而且很多时候，当你觉得你的想法实现太复杂了，多半是想法有问题。
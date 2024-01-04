# Uthread: switching between threads

两个要求，一是当第一次运行某个线程时要运行对应的函数（在create函数中传入的那个函数），并且要运行在对应的堆栈中；二是切换上下文。第二个很简单，就是往thread结构体中加一个和context一样的元素，然后和kernel/swtch.S一样的汇编切换即可。关键在于第一个，如何在第一次运行时执行对应的函数呢，很简单，将ra的值改为函数地址即可，而运行在对应的堆栈中也同样的道理，将sp的值改为stack数组的末尾地址即可（栈是由高到低增长）。

`uthread.c`

```cpp
struct usercontext {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  void       (*func)();
  struct usercontext usercontext;

};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;
extern void thread_switch(struct usercontext *, struct usercontext *);


if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    thread_switch(&t->usercontext, &next_thread->usercontext);
  } else
    next_thread = 0;
}



void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
  t->usercontext.ra = (uint64)func;
  t->usercontext.sp = (uint64)(t->stack + STACK_SIZE);
}
```

# Using threads

这个我感觉很简单，只要明白哪里产生临界情况就很容易写出来。其实产生临界情况的就一种情况，就是put时，当前没有这个key，所以要insert，而另一个线程也在插入，覆盖了另一个插入。其实key对应的value不重要，在get_thread不关心value的情况，只要key存在即可。

所以创建一个lock，然后在insert前后保护即可。

```cpp
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


// the new is new.
pthread_mutex_lock(&lock);
insert(key, value, &table[i], table[i]);
pthread_mutex_unlock(&lock);
}
```

# Barrier

emm，感觉这个也很简单。按照题意，就是到barrier的时候检查是否全部线程到达，如果全部到达就唤醒之前等待的线程，如果没有全部到达就wait，用bstate.nthread==nthread来判断是否全部到达，每次到一个线程bstate.nthread加1，当全部线程到达了将bstate.ntrhead置零并将bstate.round加1。注意上面的修改全在临界区中。

```cpp
static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  pthread_mutex_lock(&bstate.barrier_mutex);
  if (++bstate.nthread == nthread) {
    bstate.nthread = 0;
    ++bstate.round;
    pthread_cond_broadcast(&bstate.barrier_cond);
  } else {
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
  
}
```

# 总结

总的来说这几个实验都不难，只要把已有的代码看懂加上提示做出来还是没问题的，第一个实验对我的提升最大，进一步理解了ra和sp，尤其是sp，原来更改堆栈这么简单，所谓程序的状态，其实主要也就是那几个寄存器的值而已。
# coroutine

## Test Kit Development
- [x] co_start
- [x] co_getid
- [x] co_getret
- [x] co_yield
- [x] co_yield(nested)
- [x] co_waitall
- [x] co_wait
- [x] co_status

* Note: Actually, `co_wait` and `co_waitall` is unnecessary in 1-to-N model. (One thread to several coroutines) Think why.

---

When read/write coroutine, we need to lock

We only have one pool(stack), but many threads

4.28 update:

不同线程的当前协程，以及不同线程的主协程都是互相独立的。(此时需要加锁)。

> 除此之外，其他的协程之间是可以 yield 的?

不是多个 pool，所有的 coroutine id 是合在一起计算的。

需要大改，把 coroutine_pool 删除

一个线程，对应一个协程? yield，是否要清空原本的 thread? 何处加锁？

简单来说，我的实现是让 RUNNING 和 （execute）FINISH 位于不同 context。对于单线程来说，RUNNING之后马上会切换到FINISH，但是多线程可能不会直接切换下去？

5.2

是否需要多个锁？也就是每个协程都开一个锁

当然最外面的schedule创建应该是要加锁的

爆栈了，莫名其妙越界，根本没有输出?无法在程序中调用 printf

> 破案了，用户空间的 STACK_SIZE 开太小了，从 1024 到 1<<16

终于可以调试了...

对 running 的 co 如何 yield？往上跳

对于一个普通的协程，执行完也是往上跳

但是每个线程的最外层的 main_ctx，如何解决？

一层嵌套，不涉及 yield, wait，是正确的

我的理解可能有问题？不能全用 ucontext 实现，需要涉及汇编？
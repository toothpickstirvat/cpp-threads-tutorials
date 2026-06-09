# CPP Threads Tutorials

## False Sharing（伪共享）

下面的代码，多线程执行要用10+秒，而单线程执行只需2秒，这是为什么？

```c++
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
using namespace std;
using namespace std::chrono;
typedef unsigned long long ull;

ull OddSum = 0;
ull EvenSum = 0;

void findEven(const ull start, const ull end) {
    for (ull i = start; i <= end; ++i) {
        if ((i & 1) == 0) {
            OddSum += i;
        }
    }
}

void findOdd(const ull start, const ull end) {
    for (ull i = start; i <= end; ++i) {
        if ((i & 1) == 1) {
            EvenSum += i;
        }
    }
}


int main() {
    constexpr ull start = 0;
    constexpr ull end = 1900000000;
    const auto startTime = high_resolution_clock::now();
    
    // 多线程执行
    std::thread t1(findOdd, start, end);
    std::thread t2(findEven, start, end);
    t1.join();
    t2.join();

    // 单线程执行
    // findOdd(start, end);
    // findEven(start, end);
    
    const auto stopTime = high_resolution_clock::now();
    const auto duration = duration_cast<microseconds>(stopTime - startTime);
    
    cout << "OddSum: " << OddSum << endl;
    cout << "EvenSum: " << EvenSum << endl;
    cout << "Sec: " << duration.count()/1000000 << endl;
    
    return 0;
}
```

这就是伪共享问题。`OddSum`和`EvenSum`是两个相邻的全局变量，在内存中紧挨着，大概率落在同一各个CPU缓存行（cache line，通常64字节）里。
多核CPU的缓存一致性协议（MESI）规定：任何一个核修改了缓存行，其他持有该缓存行的核必须将其标记为无效，然后重新从内存或其他核同步。
结果就是：

- t1修改OddSum -> t2持有的缓存行失效 -> t2必须重新加载
- t2修改EvenSum -> t1持有的缓存行失效 -> t1必须重新加载
- 两个线程相互“踢”对方的缓存，每次循环都在做无效化操作（标记无效，强制重新同步），比单线程穿行执行慢得多

### 修复方法

* 用`alignas(64)`把两个变量强制对齐到不同的缓存行
    ```c++
    alignas(64) ull OddSum = 0;
    alignas(64) ull EvenSum = 0;
    ```
* 使用局部变量
    ```c++
    void findEven(const ull start, const ull end) {
        ull sum = 0; // 栈上的局部变量，每个线程独立
        for (ull i = start; i <= end; ++i) {
            if ((i & 1) == 0) {
                sum += i;
            }
        }
        OddSum = sum; // 整个循环只写一次全局变量
    }
    
    void findOdd(const ull start, const ull end) {
        ull sum = 0;
        for (ull i = start; i <= end; ++i) {
            if ((i & 1) == 1) {
                sum += i;
            }
        }
        EvenSum = sum;
    }
    ```
  局部变量在各自线程的栈上，内存地址相距很远，根本不再同一个缓存行，自然就没有false sharing。

## Join & Detach

操作系统里，线程是一个独立的执行单元，但它不是完全自治的，它的生命周期需要有人来交代清楚。`std::thread`对象被析构时，如果它仍然是
`joinable`状态（既没有`join`也没有`detach`），C++标准规定直接调用`std::terminate()`
杀死整个进程。这个设计是故意的强迫你明确表态：这个子线程你到底想怎么处理？

注意：
`joinable()`检查的不是线程是否在运行，而是这个thread对象有没有被交代清楚（join或detach）。哪怕自线程已经跑完了，只要你没有调用
`join/detach`，`t.joinable()`让人是`true`，析构时仍然`terminate`。

### join做了什么

主线程在`t.join()`处阻塞，等待子线程执行完毕，然后回收它的资源（回收线程句柄/OS资源）。

### detach做了什么

把子线程从`std::thread`对象上分离，让它变成一个后台线程（`daemon thread`），自己跑完自己消亡，主线程不管它。换句话说，
`detach()`就是切断C++对象对OS线程的持有，OS线程继续跑，C++对象变成空壳。

具体来说是：

- `std::thread::detach()`内部调用了`pthread_detach()`，在OS线程上打上一个标记，表示这个线程结束后资源自动回收，需要人来join。
    - 所谓打上标记，就是把`detach_state`从`JOINABLE`改成`DETACHED`。线程结束时，OS检查这个字段。
        - `JOINABLE`：保留资源，等待有人`pthread_join`
        - `DETACHED`：直接回收资源
    - TCB里有一个字段记录线程的分离状态
        - `detach_state = PTHREAD_CREATE_JOINABLE`
        - `detach_state = PTHREAD_CREATE_DETACHED`
- 清空`std::thread`对象，即解除绑定。

### 资源回收角度

#### join

`pthread`（Linux底层线程库）里，一个线程跑完后，它的资源（栈、TCB线程控制块、退出状态）不会立刻被回收，而是进入僵尸的状态，等待有人来
`pthread_join`取走退出状态，然后才释放。`std::thread::join()`底层就是`pthread_join()`，所以说`join`涉及资源回收，这部分是真的。
但`join`更核心的语义是同步：主线程在这里等自线程跑完，资源回收只是等完之后顺带的事。

#### detach

`detach`做的事：放弃对这个线程的管理权，线程跑完后让OS自己清理，我不管了。资源是线程结束时OS自动回收的，不是`detach()`调用时回收的。
`detach`本身不释放任何资源，它只是把`std::thread`对象和底层OS线程解绑。

#### 更准确的理解

| 视角     | join        | detach     |
|--------|-------------|------------|
| 谁等线程结束 | 主线程         | 没人等，OS自己处理 |
| 资源何时释放 | `join()`返回时 | 线程自己跑完时    |
| 核心语义   | 同步          | 放弃所有权      |

### C++线程

C++里线程这个词在两个层面是混用的：

- `std::thread`对象：C++层面的管理句柄，是一个普通的栈上变量，负责持有对OS线程的所有权。
- OS线程：正真在跑的执行单元，有自己的栈、寄存器、调度状态。

#### 再次理解

```c++
std::thread t(run, 10);
```

这行代码做了两件事：

- 调用OS接口（Linux上是`pthread_create`）创建一个OS线程，开始执行`run(10)`
- 把这个OS线程的句柄存进`std::thread`对象t里，由t持有所有权
- 后续join/detach的本质就是在处理这个绑定关系：
    - `join()`：等OS线程跑完，然后解除绑定，释放资源
    - `detach()`：直接解除绑定，OS线程自生自灭

### OS线程

创建OS线程，本质上是让OS为这个线程准备做好独立运行所需的一切，然后交给调度器。
具体做了：

#### 1. 分配独立的栈

每个线程有自己的栈（Linux上默认是8MB），存放局部变量、函数调用帧。这是线程间最核心的隔离。

#### 2. 创建TCB（Thread Control Block）

OS为这个线程创建一个数据结构，记录它的状态：寄存器值、栈指针、程序计数器（PC）、线程ID、调度优先级等。

#### 3. 设置初始执行上下文

把程序计数器指向你传入的函数（如run），把栈指针只想新分配的栈。

#### 4. 加入调运起的运行队列

OS调度器看到这个新线程，在合适的时机把它分配到某个CPU核心上执行。

和进程相比，线程创建的代价小得多，原因就在这里：

- 创建进程：需要复制整个地址空间、文件描述符表等等
- 创建线程：只需要新栈+TCB，地址空间共享，不复制

在Linux上，`std::thread` -> `pthread_create()` -> `clone()`系统调用，本质就是创建一个共享空间的轻量级进程。

## try_lock_for VS try_lock_until

这两个函数都属于std::timed_mutex，功能相似但参数语义不同。

### try_lock_for - 等待一段时长

```c++
m.try_lock_for(std::chrono::seconds(1));
```

- 参数是相对时间（duration）：从调用那一刻起，最多等待多久
- 内部等价于：`try_lock_until(now()+duration)`
- 语义：我愿意等1秒

### try_lock_until - 等待到某个时间点

```c++
auto now = std::chrono::steady_clock::now();
m.try_lock_until(now + std::chrono::seconds(1));
```

- 参数是绝对时间点（time_point）：等到哪个时刻为止
- 你需要自己计算截止时间，灵活性更高
- 语义：我等到now+1秒这个时刻

### 总结

| 对比项  | try_lock_for | try_lock_until               |
|------|--------------|------------------------------|
| 参数类型 | duration（时长） | time_point（时间点）              |
| 参数示例 | seconds(1)   | now() + seconds(1)           |
| 使用场景 | 只关心等待多久      | 需要精确截止时间（如多步操作共享同一个deadline） |

## std::recursive_mutex

### 背景

普通`std::mutex`有一个硬性约束：同一个线程不能对它lock两次，第二次lock会造成死锁。但现实中有一类很自然的场景：递归函数或函数A调用函数B，两者都需要加锁，而且调用方已经持有锁。

```c++
// 用普通mutex会死锁
void funA() {
    m.lock();
    funcB(); // funcB也要lock m，死锁
    m.unlock();
}

void funcB() {
    m.lock(); // 同一线程再次lock，卡死
    // ...
    m.unlock();
}
```

`recursive_mutex`就是为了解决这个问题而生的：同一线程可以多次`lock`，内部维护一个计数器，`lock`几次就要`unlock`几次才真正释放。

### 使用场景

- 递归函数内部需要保护共享数据
- 同一线程内多个函数相互调用且都要加锁（但通常这是代码设计问题，能重构则重构）

### 注意

`recursive_mutex`比普通`mutex`开销更大（需要维护所有权和技术），不要随手替代普通`mutex`。

## std::lock_guard

### 背景

手动`lock/unlock`有个经典问题：如果中间抛异常，`unlock`永远不会被调用，锁泄漏，其他线程永远阻塞。

```c++
// 危险写法
m.lock();
doSomething(); // 如果这里抛异常，unlock不执行，死锁
m.unlock();
```

这是RAII（Resource Acquisition Is Initialization）思想的典型应用场景。`lock_guard`在构造时加锁，析构时自动解锁，无论函数正常推出还是异常退出，析构函数必定执行。

```c++
void task(const char* threadNumber, int loopFor) {
    std::lock_guard<mutex> lock(m1);  // 构造，m1.lock()
    for (int i = 0; i < loopFor; ++i) {
        buffer++;
        cout << threadNumber << buffer << endl;
    }
}   // 函数退出，lock析构，m1.unlock()自动调用
```

### 使用场景

- 绝大多数简单加锁场景：进入作用域加锁，离开作用域解锁
- 只需要“锁住整个代码块”的情况

### 局限

`lock_guard`及其简单，没有任何灵活性：

- 不能中途手动`unlock`
- 不能延迟加锁
- 不能转移所有权

## std::unique_lock

### 背景

`lock_guard`够用，但不够灵活。`unique_lock`是`lock_guard`
的功能超集，在保留RAII自动解锁的同时，增加了大量控制能力。它的出现是为了满足条件变量（`condition_variable`）、延迟加锁、超时加锁等高级场景。

```c++
std::unique_lock<mutex> lock(m); // 等价于lock_guard，构造时加锁

std::unique_lock<mutex> lock(m, std::defer_lock); // 构造时不加锁
lock.lock(); // 手动决定何时加锁
```

`std::defer_lock`是一个tag，告诉`unique_lock`先别加锁，我晚点手动锁。

### 使用场景

```c++
std::condition_variable cv;
std::unique_lock<mutex> lock(m);
cv.wait(lock, []{return ready;});
// wait内部需要能unlock（等待时释放锁）再lock（被唤醒后再重新获取）
// lock_guard做不到这一点
```

## 锁的“所有权”

本质就是：“谁负责unlock这把锁？”

### 从底层看

`mutex`本身只是一个状态机：

- `locked`：某人持有它
- `unlocked`：没人持有

它不记录“谁”锁了它（除了`recursive_mutex`需要记录线程ID）。`lock()`就是抢占，`unlock()`就是释放，仅此而已。
所有权是建立在`mutex`之上的软件层约定：由某个对象来独占地承担调用`unlock()`的职责。

### lock_guard VS unique_lock的区别就在这里

`lock_guard`的所有权是绑死的：

```c++
// 绑死：lock_guard活着，锁就不会释放;lock_guard死了，锁就释放
// 没有任何方式吧这个职责转交给别人
std::lock_guard<mutex> lock(m);
```

`unique_lock`的所有权是可转移的：

```c++
std::unique_lock<mutex> lock1(m); // lock1拥有锁
std::unique_lock<mutex> lock2 = std::move(lock1);
// 现在lock2拥有锁，lock1什么都没有
// 析构时，lock2负责unlock，lock1什么都不做
```

`move`之后，`lock1.owns_lock()`返回`false`，`lock2.own_lock()`返回`true`。“谁负责unlock”这个职责被转移了。

### defer_lock也是所有权的体现

```c++
std::unique_lock<mutex> lock(m, std::defer_lock);
// lock对象存在，但owns_lock() == false
// 此时lock不拥有m，析构时不会unlock

lock.lock(); // 现在owns_lock()==true，析构时会unlock
```

`unique_lock`内部就是维护了一个`bool owned`标志，析构函数检查这个标志再决定要不要`unlock`。

### 总结

所有权=析构时是否有义务调用`unlock()`。

- `lock_guard`这个义务不可转移
- `unique_lock`这个义务可以通过`move`转交给另一个对象

## std::condition_variable

条件变量是一种线程同步机制，用来解决这类问题：一个线程需要等待某个条件成立，才能继续执行。

### 为什么需要条件变量

如果没有条件变量，只能用轮训：

```c++
// 忙等，非常浪费CPU
while (balance == 0) {
    // 一直循环检查
}
```

条件变量让线程可以挂起睡眠，等条件满足时再被唤醒，不浪费CPU。

### 核心操作

| 操作                      | 说明                 |
|-------------------------|--------------------|
| cv.wait(mtx, predicate) | 条件不满足时释放锁并挂起，满足时继续 |
| cv.notify_one()         | 唤醒一个正在等待的线程        |
| cv.notify_all()         | 唤醒所有正在等在的线程        |

### wait()

wait()内部做的事：

```
unlock(m)   ← 释放锁
sleep()     ← 挂起
被唤醒后...
lock(m)     ← 重新加锁
检查条件
条件不满足    → 重复上面流程
条件满足      → 返回（此时锁是持有状态）
```

### Lost Wakeup（丢失唤醒）

```
withdrawMoney                                         addMoney
-------------------------------------------------------------------------------------------------------------
检查balance==0，确实为0
                                                      balance += 500
                                                      cv.notify_one()   ← 通知发出了(但此时还没人在等，通知丢失)
wait()  ← 开始加锁、挂起（永远睡着，没人再唤醒它了）
```

问题根源：检查条件和进入等待这两步之间有空隙，notify可能恰好这个空隙发生。

先持有锁，在调用wait()就消灭了这个空隙：

```
withdrawMoney                                         addMoney
-------------------------------------------------------------------------------------------------------------
lock(m)
检查balance == 0，确实为0
wait() → unlock(m) + 挂起 ← 这两步是原子的
                                                      lock(m)  ← 必须等 unlock 之后才能进来
                                                      balance += 500
                                                      notify_one()
                                                      unlock(m)
被唤醒，重新lock(m)，继续执行
```

addMoney在wait()释放锁之前根本进不来，所以通知永远不会丢失。





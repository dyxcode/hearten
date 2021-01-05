# Hearten
这是一个简洁的http server library，基于C++17，包含以下组件，每个组件以header only的方式编写。

| 组件 | 文件名 | 功能描述 |
| ------ | ------ | ------ |
| io事件循环 | ioeventloop.h | 多路io复用，根据io事件触发相应回调
| 网络模块 | servernet.h | 封装ip tcp，提供应用层以下的网络服务
| http模块 | http.h | 封装http协议，支持interceptor
| 日志模块 | log.h | 提供日志服务，支持自定义格式，流式输出
| 线程池模块 | threadpool.h | 提供并发服务，支持定时任务，周期任务，取消任务等功能
| 存储模块 | bitcaskdb.h | 基于bitcask模型，支持get, put, delete
| json模块 | json.h | 封装json的解析和序列化

## io事件循环
事件循环基于epoll，采用水平触发。IOEventLoop负责管理epoll和channel，其中channel是一个辅助类，负责处理所有和回调相关的任务。channel对外不可见，用户需要通过IOEventLoop来访问channel，这样可以保证所有的channel都在IOEventLoop的管理之下。

## 网络模块
网络模块封装了tcp和IPv4，内部管理acceptor和connector，分别负责管理监听套接字和连接套接字。对外暴露两个回调，分别在connection和message时触发。acceptor在接受新的tcp连接时会建立一个connection，connection处理后续所有事务，因为所有套接字都是非阻塞的，所以数据传输需要使用一个用户态的buffer（具体原因详见muduo），因此也定义了一个辅助类buffer。

## http模块
http模块目前只支持http1.1。该模块负责解析request并产生response，能够自动处理静态资源，并且支持interceptor，在请求到来时自行拦截（否则按请求静态资源处理），多个interceptor按注册顺序执行。

## 日志模块
日志模型参考log4j，做了比较大的改动，使其更符合C++的特性。支持自定义格式化打印，多目的地打印时需要用户管理iostream的生命周期，并循环调用log函数即可。相比于log4j，该日志模型取消了继承+虚函数的多态机制，并且将责任进一步划分，在保留原功能的情况下进一步简化代码，提高速度。最后封装了stream，各模块的ASSERT都基于该日志模块。

## 线程池模块
线程池模块提供并发服务，具体详见本人另外一个仓库 [点击这里](https://github.com/dyxcode/thread_pool)

## 存储模块
存储模块基于bitcask模型，支持最小api集合（put，get，delete），提供持久化存储。数据存储于storage文件中，另外active文件保存目前活跃的信息，hint文件保存索引，用于快速建立内存索引。每次active文件大小到达上限时就会merge新旧数据，将active转化为storage，并更新hint文件，因此hint和storage总是匹配的，每次重新打开数据库时根据hint和active就可以建立内存索引，而不用管storage。

## json模块
json模块负责json的解析和序列化，使用C++17的variant进行存储。暂时没有封装访问操作，而是对外暴露variant变量，因此访问需要对variant进行操作。
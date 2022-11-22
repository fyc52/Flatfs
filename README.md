## 这是一个文件系统框架

### to do：

1. debug

2. 对接 VBS-SSD

3. 目前挂载在内存上；之后需要挂载到盘上并修改sb，inode，file等数据结构的分配方式

## How to run
先运行femu，下载flatfs代码
make编译生成.ko模块，sudo insmod flatfs.ko,然后 mount文件系统到bdev


femu分配空间时要全部memset 0

# 分支
- master;
- writeback: 不保证数据和元数据持久化的顺序
- hashfs： baseline

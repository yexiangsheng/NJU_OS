# 实现pstree
## 1. 通过getopt解析command参数
[参数的格式](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap12.html)
~~~bash
man 3 getopt
~~~
* DESCRIPTION
传入argc与argv，每次调用成功，return参数；失败return -1。并更新optind(下一个参数在argv中的索引)
* RETURN 
success     --> option character 
fail        --> -1
not found   --> ?

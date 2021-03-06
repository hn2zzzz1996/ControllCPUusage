代码主要来自陈硕的Muduo库，这里主要自己记录一下原理。

## Windows的任务管理器如何检测CPU使用率
如果我们打开任务管理器，就会发现CPU的使用率差不多是1s更新一次，在这1s内，CPU忙（执行应用程序）的时间与刷新周期总时间（1s）的比率，就是CPU的使用率。
如果我们想要控制CPU的使用率，比如要控制在50%，就应该让应用程序一半的时间忙，一半的时间空闲。但是又不能前0.5s忙，后0.5s空闲，这样可能会产生锯齿状的形状，而不是一个水平的直线。

## 如何控制？
首先要做的就是平均分配程序在1s内忙和空闲的分配，忙和空闲应该交替进行，那么如何分配呢？
这里使用了**Bresenham's line algorithm**算法（https://blog.csdn.net/sinat_41104353/article/details/82858375）。
该算法是一个画直线的算法，然后我们在这里将1s平均分成100份，每一份10ms，然后用该画直线算法画一条横坐标从[0,100]，纵坐标从[0,percent]的直线，percent是CPU的使用率（从0 ~ 100）。在该算法画直线的时候，横坐标从0增加到100，纵坐标从0增加到percent（假设70），那么就会在100次中有70%的忙和30%的空闲，并且是均匀分布在100个小份中。
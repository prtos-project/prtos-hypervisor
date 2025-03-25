# 贡献代码指南

PRTOS 使用Github托管其源代码，如果希望贡献代码请使用github的PR（Pull Request）的流程:

1. [创建 Issue](https://github.com/prtos-project/prtos-hypervisor/issues/new) - 对于较大的改动（如新功能，大型重构等）建议先开issue讨论一下，较小的improvement（如文档改进，bugfix等）直接发PR即可

2. Fork [PRTOS](https://github.com/prtos-project/prtos-hypervisor) - 点击右上角**Fork**按钮

3. Clone你自己的fork: ```git clone https://github.com/$userid/prtos-hypervisor.git```
	* 如果你的fork已经过时，需要手动sync：[同步方法](https://help.github.com/articles/syncing-a-fork/)

4. 从**main**创建你自己的feature branch: ```git checkout -b $my_feature_branch main```

5. 在$my_feature_branch上修改并将修改push到你的fork上

6. 创建从你的fork的$my_feature_branch分支到主项目的**main**分支的[Pull Request] -  [在此](https://github.com/prtos-project/prtos-hypervisor/compare?expand=1)点击**compare across forks**，选择需要的fork和branch创建PR

7. 等待review, 需要继续改进，或者被Merge!

在提交代码的时候，请遵守以下规则，以提高代码质量：

  * 使用clang-format -i ``your_new_file_changed``格式化你的代码
  * 运行 [本地CI测试](https://github.com/prtos-project/prtos-hypervisor/blob/main/scripts/run_test.sh) 确保没有回归问题产生. 在PRTOS源码跟目录运行命令``bash scripts/run_test.sh check-all``

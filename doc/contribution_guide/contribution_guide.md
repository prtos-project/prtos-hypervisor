# Contributing Code Guide

PRTOS uses Github to host its source code, if you wish to contribute code please use the PR (Pull Request) process of github:

1. [create Issue](https://github.com/prtos-project/prtos-hypervisor/issues/new) - For the larger changes (such as new features, large refactoring, etc.) it is best to first open an issue to discuss, and smaller improvements (such as document improvements, bugfixes, etc.) can be sent directly to PR

2. Fork [PRTOS](https://github.com/prtos-project/prtos-hypervisor) - Click the **Fork** button in the upper right corner

3. Clone Your own fork: ```git clone https://github.com/$userid/prtos-hypervisor.git```

   * If your fork is out of date, you need to manually sync: [Sync ](https://help.github.com/articles/syncing-a-fork/)

4. Create your own feature branch from **main**: ```git checkout -b $my_feature_branch main```

5. Make changes on $my_feature_branch and push the changes to your fork

6. Create a [Pull Request] from your fork's $my_feature_branch branch to the main project's **dev** branch - [here](https://github.com/prtos-project/prtos-hypervisor/compare?expand=1) Click on **compare across forks** and select the required fork and branch to create the PR

7. Waiting for review, need to continue to improve, or be Merge!

When submitting code, please observe the following rules to improve the quality of the code:

  * Format your codes with clang-format -i ``your_new_file_changed``
  * Run your code with [Local CI Test](https://github.com/prtos-project/prtos-hypervisor/blob/main/scripts/run_test.sh) to make sure there are no regression issues. Just run ``bash scripts/run_test.sh check-all`` in the project root directory.
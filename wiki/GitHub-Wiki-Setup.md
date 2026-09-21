# 启用 GitHub 网页 Wiki（可选）

本仓库已经在目录 `wiki/` 里维护文档，**不依赖** GitHub Wiki 也能用。

若希望在 GitHub 仓库页面出现 **Wiki** 标签，需要先在网页上初始化一次（当前远程 `*.wiki.git` 尚不存在）。

## 步骤

1. 打开：https://github.com/littletenyuan01/photoshopDemo  
2. **Settings → General → Features**，确认 **Wikis** 已勾选  
3. 打开仓库顶部 **Wiki** → **Create the first page**  
4. Title 填 `Home`，内容可先粘贴本仓库 `wiki/Home.md`，保存  

初始化后，可用独立仓库同步（可选）：

```bat
git clone git@github.com:littletenyuan01/photoshopDemo.wiki.git
```

将本仓库 `wiki/` 下的 `.md` 复制进去（GitHub Wiki 首页文件名为 `Home.md`；侧边栏为 `_Sidebar.md`），再 `git add` / `commit` / `push`。

## 建议

简历与日常开发优先维护 **仓库内 `wiki/`**（和代码一起版本管理、克隆即可见）。GitHub Wiki 作为镜像即可。

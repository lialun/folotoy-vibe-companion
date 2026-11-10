<p align="right">
  <strong>简体中文</strong> · <a href="CI-sync-main.md">English</a>
</p>

# 上游同步

`.github/workflows/sync-main.yml` 用于把 fork 的 `main` 与 `FoloToy/ai-passport:main`
同步。本应用分支已关闭定时同步，仅允许手动触发；非 fork 仓库自动跳过。
本文件与工作流一同维护。

工作流不保存检出凭证，使用固定完整 SHA 的同步 Action。自动生成的 `GITHUB_TOKEN`
拥有 `contents: write`，无需自行配置令牌。若上游工作流变化导致同步失败，检查 Actions
日志，必要时使用 GitHub 的 **Sync fork**。

应用代码和文档应保存在应用分支，硬件基线的 `main` 才用于上游同步。如果应用开发
直接使用 `main`，手动运行前必须检查拟合并的上游改动，不得用同步覆盖应用工作。
fork 所有者需要自行开启 Actions。

检出明确指定 `ref: main`，即使手动触发时选了其他分支也不改变目标。该 Action 依赖
本地 `main` 分支，否则 `git checkout main` 可能把仓库中的 `main/` 目录解释为路径。
升级 Action 时，核对其官方版本与完整提交 SHA，并同步修改版本注释。

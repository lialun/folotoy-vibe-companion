<p align="right">
  <a href="CI-sync-main.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Upstream Synchronization

`.github/workflows/sync-main.yml` synchronizes a fork's `main` branch with `FoloToy/ai-passport:main`. It runs only by manual dispatch and is skipped when the repository is not a fork. Scheduled synchronization is disabled for this application checkout.

The workflow checks out the target `main` without persisting credentials and uses the full-SHA-pinned upstream-sync Action. Its generated `GITHUB_TOKEN` has only `contents: write`; no manually configured token is required. If synchronization fails after an upstream workflow change, use GitHub's **Sync fork** once and inspect the Actions log.

Keep fork-specific firmware and documentation on feature branches so `main` stays synchronized. If development must happen directly on `main`, review the proposed upstream merge before running this workflow; never use it to replace application work. Fork owners must explicitly enable Actions after forking.

The checkout explicitly sets `ref: main`, even when a manual dispatch selects
another branch. The sync Action expects a local `main` branch before fetching;
without that explicit checkout, `git checkout main` can interpret this
repository's `main/` directory as a path instead of switching branches.

When upgrading an Action, verify the release and full commit SHA from the official repository and update the inline version comment.

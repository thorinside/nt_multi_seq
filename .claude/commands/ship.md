Ship a new release by creating and pushing a semver tag.

## Arguments

Optional: `$ARGUMENTS` — one of `major`, `minor`, or `patch`.

If omitted, determine the bump type semantically by analyzing the commits since the last release tag.

## Instructions

1. Run `git tag --sort=-v:refname -l 'v*'` to find the latest semver tag. If none exists, treat the current version as `v0.0.0`.

2. Parse the latest tag into major, minor, patch components.

3. If `$ARGUMENTS` is `major`, `minor`, or `patch`, use that bump type. Otherwise, analyze the commits since the last tag using `git log <last_tag>..HEAD --oneline` and determine the bump type:
   - **major**: commits contain breaking changes (e.g. "breaking", "BREAKING", major architectural rewrites)
   - **minor**: commits contain new features (e.g. "feat", "add", "new", "refactor")
   - **patch**: commits contain only fixes, docs, chores, CI changes

4. Compute the new version string (e.g. `v0.2.0`).

5. Show the user:
   - Current version
   - Bump type and reason
   - New version
   - List of commits that will be included

   Ask for confirmation before proceeding.

6. After confirmation:
   - Create the tag: `git tag v<new_version>`
   - Push the tag: `git push origin v<new_version>`
   - Report the tag that was pushed and that the GitHub Actions release workflow will handle the rest

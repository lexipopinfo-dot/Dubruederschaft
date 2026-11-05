# PUSH_CHECKLIST — get Dubrüderschaft onto GitHub and trigger the first build

> Status 2026-11-05 (Phase 2): this repo is **ready to push**, but no GitHub
> repo exists yet and this environment has **no GitHub credentials**
> (`gh auth status`: "You are not logged into any GitHub hosts", no
> `~/.config/gh`, no `~/.gitconfig`). Do NOT invent credentials and do NOT
> create a GitHub account in Lexi's name. One human step is needed first.

## Decision needed (Lexi / Mo, before pushing)

- **Who owns the GitHub repo?** Options:
  1. Lexi creates a free GitHub account (or uses an existing one) and
     creates an empty repo named `Dubruederschaft` (private recommended),
     then gives Mo access — or
  2. Mo creates the repo under a project account and shares the artifact
     with Lexi.
- The repo must be **private** (it is Lexi's unreleased plugin).

## Exact steps (once the empty repo exists)

Run these from `workspace/dub_siren_v1/plugin/` on a machine with git
and `gh` logged in (or with an SSH key added to the GitHub account):

```sh
cd workspace/dub_siren_v1/plugin

# 1. Initialise git and point at the new empty repo (replace <owner>)
git init -b main
git add -A
git commit -m "Dubrüderschaft v0.1.0 — Phase 2 smoke-test scaffold (iPlug2, VST3 x86_64)"
git remote add origin git@github.com:<owner>/Dubruederschaft.git

# 2. Push (this triggers the smoke-test workflow on push)
git push -u origin main

# 3. Watch the run
gh run watch --exit-status
# or open: https://github.com/<owner>/Dubruederschaft/actions
```

Notes:

- The first push triggers `.github/workflows/build-macos-vst3.yml`
  (it also runs on `workflow_dispatch` from the Actions tab).
- The workflow checks out the iPlug2 submodule recursively, downloads
  the VST3/CLAP SDKs, configures with the `macos-xcode-x86_64` preset,
  builds Release, verifies the bundle is x86_64 with `lipo -archs`,
  ad-hoc signs it, and uploads
  `Dubruederschaft-vst3-x86_64.zip` as an artifact.
- **Expect the first run to fail at least once.** That is normal for a
  first iPlug2 CI build. Read the failing step's log, fix, push again.
  Do not claim a working `.vst3` until the workflow is green AND the
  bundle loads in Ableton on Lexi's Intel Mac.
- If `git clone` of the iPlug2 submodule is slow from this VM (the
  `git` protocol stalled during Phase 2; HTTPS tarballs worked fine),
  prefer the Actions runner for the clone — it is not affected by this
  VM's network.

## After a green build

1. Download `Dubruederschaft-vst3-x86_64.zip` from the Actions run.
2. Unzip, copy `Dubruederschaft.vst3` to
   `~/Library/Audio/Plug-Ins/VST3/` on Lexi's MacBook Pro 2016.
3. If macOS quarantines it: `xattr -d com.apple.quarantine Dubruederschaft.vst3`
4. Restart Ableton 12 Suite (or Rescan in Preferences > Plug-ins),
   load the plugin, and ear-test: Trigger/Hold, Wave switch loudness,
   Mode behaviour, Echo/Reverb, Master. Report back; Mo fixes from there.

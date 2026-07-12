# Running the Linux release build on your own machine

The `linux-x64` job in `.github/workflows/release.yml` targets a **self-hosted runner** labelled
`gfx-linux`. This document sets one up.

Why bother: vcpkg builds openimageio, assimp, shader-slang and the Vulkan validation layers from
source. GitHub's free Linux runner gives 4 vCPU and is destroyed afterwards, so it re-downloads (or
worse, rebuilds) the world every run. A self-hosted runner is your actual desktop — 32 cores, and a
vcpkg binary cache under `~/.cache/vcpkg` that stays warm between runs.

**`windows-x64` is currently disabled** (commented out in the matrix). It cannot move to this runner:
a self-hosted job runs *natively* — no cross-compilation, no emulation — so building for Windows
needs a Windows machine. On GitHub's hosted runner it worked but cost the better part of an hour per
run, because vcpkg builds openimageio/assimp/slang/validation-layers from source under MSVC on 4
vCPU with a cold cache.

To bring it back, uncomment the block in the matrix. If you have a Windows box or VM by then,
register a second runner on it (Visual Studio 2022 Build Tools installed), label it `gfx-windows`,
and set that entry's `runner:` to the label. The binary cache needs no per-platform setting: the
"Point vcpkg at a binary cache that survives the run" step keys off `runner.environment` and picks
the persistent on-disk cache for a self-hosted box, the Actions cache for a hosted one.

**While it is disabled, releases are Linux-only** — the `release` job will draft a release from
whatever artifacts exist, so a tag pushed today produces a Linux-only release.

## Is this safe?

Yes, for this repo. The standard warning about self-hosted runners is that a pull request from a
**fork** can execute arbitrary code on your machine. That does not apply here on two counts:
`GFX_RELOADED` is private, so there are no outside forks; and this workflow triggers only on
`push: tags` and `workflow_dispatch`, never on `pull_request`. The only thing that can run on the
runner is a tag *you* push or a run *you* start.

## Prerequisites

You already have all of these — you build this project locally — but for the record, the runner user
needs: `cmake` (>= 3.28), `ninja`, a C++23-modules-capable `gcc`/`clang`, `git`, `curl`, `zip`,
`unzip`, `tar`, `pkg-config`, and the X11/Wayland/GL development headers that GLFW, GLEW and the
Vulkan loader compile against. On Arch/CachyOS that last group is:

```sh
sudo pacman -S --needed libx11 libxrandr libxinerama libxcursor libxi libxext libxfixes \
                        libxrender libxkbcommon wayland wayland-protocols mesa glu libtool nasm
```

The hosted-runner `apt` step in the workflow is skipped on self-hosted runners (it keys off
`runner.environment`), so a missing header shows up as a vcpkg build failure naming the *header*,
not the package. It is a one-time fix, not a per-run cost.

## Register the runner

The registration token is short-lived and has to come from the web UI:

**Settings → Actions → Runners → New self-hosted runner → Linux / x64**
(<https://github.com/radueduard/GFX_RELOADED/settings/actions/runners/new>)

Copy the token it shows you, then:

```sh
mkdir -p ~/actions-runner && cd ~/actions-runner

# Grab the current runner release rather than pinning a version that will rot.
version=$(curl -fsSL https://api.github.com/repos/actions/runner/releases/latest \
          | grep -oP '"tag_name":\s*"v\K[^"]+')
curl -fsSL -o runner.tar.gz \
  "https://github.com/actions/runner/releases/download/v${version}/actions-runner-linux-x64-${version}.tar.gz"
tar xzf runner.tar.gz && rm runner.tar.gz

# --labels must include gfx-linux: that is what the workflow's `runs-on` selects.
./config.sh \
  --url https://github.com/radueduard/GFX_RELOADED \
  --token <PASTE_TOKEN_HERE> \
  --labels gfx-linux \
  --name cachyos-desktop \
  --unattended
```

Then install it as a service so it survives a reboot and you never have to remember to start it:

```sh
sudo ./svc.sh install    # runs as your user, not root
sudo ./svc.sh start
sudo ./svc.sh status
```

Foreground `./run.sh` also works if you would rather see the logs while you are still setting this
up.

## It does not touch your working tree

Worth being explicit, because `actions/checkout` runs `git clean -ffdx` and that sounds alarming.
The runner makes its **own clone**, under `~/actions-runner/_work/GFX_RELOADED/GFX_RELOADED`. It
never looks at `/mnt/w/CLionProjects/GFX`. The clean wipes that workspace's build artifacts, not
yours.

The one thing shared with your normal development is the vcpkg binary cache in `~/.cache/vcpkg` —
which is the point, and is safe to share: entries are keyed by an ABI hash, so a package built for a
different compiler or triplet is a different entry, not a collision.

## Running it

Actions → Release → **Run workflow**. That is `workflow_dispatch`, which builds and uploads
artifacts but cannot publish anything (the `release` job is gated on `if: github.ref_type == 'tag'`).
Only `git tag v0.0.1 && git push origin v0.0.1` produces a release, and even then it is a draft.

## If the machine is off

The job queues, indefinitely, waiting for a runner with the `gfx-linux` label. GitHub will not fall
back on its own. To go back to a hosted runner, change one line in the matrix:

```yaml
- name: linux-x64
  runner: ubuntu-24.04                        # was: gfx-linux
  triplet: x64-linux
```

Nothing else in the workflow needs to change — the `apt` step and the binary-cache step re-enable
themselves, because they key off `runner.environment` rather than the runner's name. It is worth
doing this occasionally anyway: a green run on a hosted runner is the only thing that proves the
build does not quietly depend on something that happens to be installed on your desktop. Expect it
to be slow: a hosted runner starts with a cold cache and builds every port from source.

## The binary cache

`~/.cache/vcpkg/archives`, and it is the reason a run takes one minute rather than nine. It is the
same cache your local builds populate — the runner runs as you — so a dependency you have already
built at your desk is never rebuilt in CI.

Do not set `VCPKG_BINARY_SOURCES` to `clear;default,readwrite` here, however much the vcpkg docs
suggest it. `lukka/run-vcpkg` exports its own `VCPKG_DEFAULT_BINARY_CACHE` pointing at a per-run
directory inside the workspace, and `default` means "wherever that variable points" — so the cache
was empty on arrival and deleted on exit, every run rebuilt all 64 ports from source, and the only
sign of it was one line reading `Restored 0 package(s)`. The workflow now names the path
explicitly with the `files` provider, which requires an absolute path and therefore cannot be
redirected out from under you.

If it ever needs clearing (a corrupt archive, disk pressure), `rm -rf ~/.cache/vcpkg/archives` is
safe — the next run is slow and then it is fast again.

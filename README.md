# Cute-Alden

Modified by senxiu on 2026-04-21.

> `cute-alden` is a downstream fork of Matthew Skala's [`alden`](https://aur.archlinux.org/packages/alden), the
> "detachable terminal sessions without breaking scrollback" tool, based on
> the `alden-0.2` source release.

`cute-alden` keeps terminal sessions alive across disconnects without turning
itself into a terminal multiplexer like `tmux` or `screen`.

This fork adds a few practical session-management features on top of the
original design:

- named sessions
- attach by session name
- session listing
- session rename
- explicit detach command
- visible connect / detach / close status messages
- optional bounded history replay on reconnect
- local hardening around session discovery and attachment

## Build

```bash
cd cute-alden-0.2
./configure --prefix=/usr
make -j"$(nproc)"
```

The binary is:

```bash
./cute-alden
```

## Usage

Start a new session:

```bash
./cute-alden
```

Start or reuse a named session:

```bash
./cute-alden --name demo
```

Attach to an existing named session:

```bash
./cute-alden --attach demo
./cute-alden demo
```

Reconnect by PID:

```bash
./cute-alden --pid 12345 -r
```

List sessions:

```bash
./cute-alden list
./cute-alden --list
```

Rename the current or targeted session:

```bash
./cute-alden rename new-name
./cute-alden --rename new-name
./cute-alden --attach old-name --rename new-name
./cute-alden --pid 12345 --rename new-name
```

Detach without closing the managed session:

```bash
./cute-alden detach
./cute-alden --detach
```

If the client is attached in a normal terminal, `Ctrl+D` can also act as a
detach gesture when the hosted program does not use that key itself.

## TUI Note

`cute-alden` can optionally keep a bounded output log and replay it on
reconnect:

```bash
./cute-alden --name demo --history-bytes 65536 -- /bin/sh
./cute-alden --attach demo --history-bytes 65536
```

This replay is raw terminal output. It works well for shell-like sessions, but
it can look wrong for full-screen TUIs. For TUIs, the safer default is usually
to keep session persistence on and history replay off.

## Environment

Inside a managed session, `cute-alden` exports:

- `CUTE_ALDEN=1`
- `CUTE_ALDEN_SESSION_ACTIVE=1`
- `CUTE_ALDEN_SESSION_PID=<pid>`
- `CUTE_ALDEN_SESSION_NAME=<name>` for named sessions
- `CUTE_ALDEN_SESSION_LOG=<path>` when history replay is enabled

Compatibility aliases are also kept for the older `ALDEN_*` names.

## Packaging

For a local Ubuntu/Debian package build, see [ubuntu/README.md](ubuntu/README.md).

## License

This repository is a modified fork of `alden-0.2` and remains under
`GPL-3.0-only`.

- repository license text: [LICENSE](LICENSE)
- source-tree GPL text: [cute-alden-0.2/COPYING](cute-alden-0.2/COPYING)

When distributing binaries or packages, make the corresponding source for the
same version available as well.

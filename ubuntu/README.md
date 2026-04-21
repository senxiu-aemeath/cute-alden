# Ubuntu Packaging

This repository already contains the `cute-alden-0.2` source tree and
tarball. The `build-deb.sh` helper builds a local Debian/Ubuntu package
without requiring `debhelper` or a full Debian packaging directory.

## Requirements

- `build-essential`
- `dpkg`
- `gzip`
- `tar`

On a normal Ubuntu system, only `build-essential` may need to be installed.

## Build

```bash
./ubuntu/build-deb.sh
```

The package is written to:

```text
dist/cute-alden_0.2-1_<arch>.deb
```

For example, on amd64:

```bash
sudo apt install ./dist/cute-alden_0.2-1_amd64.deb
```

## What The Script Does

- copies the bundled `cute-alden-0.2` source into a temporary build directory
- runs `./configure --prefix=/usr`
- runs `make`
- runs `make DESTDIR=... install`
- adds package metadata and documentation
- emits a `.deb` package with `dpkg-deb`

If you need this build flow to survive container or image rebuilds, add the
required Ubuntu packages to your Dockerfile or image setup instead of
installing them manually in a running container.

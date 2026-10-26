# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# JieLi's Windows toolchain under Wine, and PowerShell, on Linux: the build a
# Mac runs and CI's Linux job. Run it through tools/container/build.sh.
#
# The toolchain is 32-bit. On Apple silicon, Rosetta runs the x86_64 parts
# (PowerShell, wineserver) and Docker's QEMU runs 32-bit Wine. Rosetta for
# Linux can't run 32-bit code, which rules out Wine's WoW64 mode, so Wine
# here is Debian's multiarch wine32.
FROM debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61

ARG PWSH_VERSION=7.4.22
ARG PWSH_SHA256=9de443bef974f219c98a33b0f4096aace7f30b42188f37a1b14c5da7e82c8a6a

RUN dpkg --add-architecture i386 \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      wine wine32:i386 wine64 innoextract ca-certificates curl libicu72 \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/pwsh.tar.gz \
      "https://github.com/PowerShell/PowerShell/releases/download/v${PWSH_VERSION}/powershell-${PWSH_VERSION}-linux-x64.tar.gz" \
 && echo "${PWSH_SHA256}  /tmp/pwsh.tar.gz" | sha256sum -c - \
 && mkdir -p /opt/powershell \
 && tar -xzf /tmp/pwsh.tar.gz -C /opt/powershell \
 && chmod +x /opt/powershell/pwsh \
 && ln -s /opt/powershell/pwsh /usr/local/bin/pwsh \
 && rm /tmp/pwsh.tar.gz

# en-US, as on the Windows runner: Build.ps1 sorts its sources by name, and
# the link order follows that sort.
ENV LANG=en_US.UTF-8 \
    POWERSHELL_TELEMETRY_OPTOUT=1 \
    WINEPREFIX=/opt/wineprefix \
    WINEDEBUG=-all

RUN wineboot --init && wineserver --wait

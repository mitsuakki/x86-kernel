# Security Policy

## Supported versions

This is an educational/hobby x86 kernel. Current development branch:

| Branch   | Supported          |
| -------- | ------------------ |
| `main`   | :white_check_mark: |
| Others   | :x:                |

## Reporting a vulnerability

This is not a production kernel. For bugs or vulnerabilities:

1. Open a GitHub issue with label `bug`
2. Include exact reproduction steps and QEMU output
3. If the issue is sensitive, contact the maintainer directly

## Scope

Security concerns relevant to an OS kernel:
- Privilege escalation (ring 3 → ring 0)
- Memory safety (buffer overflows, use-after-free in kernel code)
- Improper interrupt handling (double faults, triple faults)
- Page table misconfiguration

## Acknowledgments

We appreciate responsible disclosure. Since this is an educational project, there are no bounties or embargoes. All issues are fixed publicly.

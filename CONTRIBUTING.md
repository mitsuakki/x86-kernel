# Contributing to x86-kernel

## Getting started

```bash
# Install toolchain (Debian/Ubuntu)
sudo apt-get install nasm gcc-multilib binutils qemu-system-x86

# Build and run
make build
make run
```

## Development workflow

1. Fork the repo
2. Create a feature branch: `git checkout -b feat/<my-feature>`
3. Make your changes
4. Build and test: `make build && make run`
5. Commit using [Conventional Commits](https://www.conventionalcommits.org/):
   - `feat: add IDT initialization`
   - `fix: correct GDT segment limit`
   - `docs: update boot sequence diagram`
   - `...`
6. Push and open a PR against `main`

## Code style

### Assembly (NASM)
- Use lowercase mnemonics
- Indent instructions with 4 spaces
- Comment every block

### C
- C11 standard
- 4-space indentation
- All functions `static` unless intentionally exported
- No standard library, kernel is freestanding

## Commit conventions

| Prefix   | Use for                                      |
| -------- | -------------------------------------------- |
| `feat`   | New feature or subsystem                     |
| `fix`    | Bug fix                                      |
| `docs`   | Documentation only                           |
| `ci`     | CI/CD, workflows, tooling                    |
| `refactor` | Code restructuring without behavior change |

## Architecture milestones

| Milestone | Description                              |
| --------- | ---------------------------------------- |
| M1        | Boot to long mode                        |
| M2        | Interrupts (IDT, PIC/APIC)               |
| M3        | Paging (page tables, higher-half)        |
| M4        | Timer (APIC timer / PIT)                 |
| M5        | Memory management (frame allocator, heap)|
| M6        | Scheduling (context switch, round-robin) |
| M7        | Syscalls (syscall/sysret, first binary)  |

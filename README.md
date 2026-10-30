# AArch64 Operating System Kernel

A compact bare-metal operating system kernel for the BCM2837-based Raspberry Pi 3B. The kernel is written in C and AArch64 assembly and is developed and exercised in QEMU.

The project implements the path from machine startup to isolated user execution: boot and exception setup, kernel scheduling, system calls, virtual memory, and user-process tests.

## Capabilities

- **AArch64 startup:** transitions from EL2 to EL1, initializes exception vectors and the kernel stack, and enables the MMU.
- **Address spaces:** maintains a high-half kernel mapping and per-process user page tables using 4 KB translation granules.
- **Scheduling:** supports kernel threads and user processes, with timer-driven preemption for user execution.
- **Exceptions and system calls:** saves and restores user state through trap frames; implements process, UART, mailbox, and signal operations.
- **Physical memory:** uses a buddy allocator and reference-counted pages for shared user frames.
- **Virtual memory:** supports private anonymous mappings, demand paging, access-fault reporting, and copy-on-write after `fork`.
- **Device I/O:** provides a UART shell and Raspberry Pi mailbox support, including user access to framebuffer memory.
- **Initramfs:** loads user images from a `newc` CPIO archive.

## Architecture

```text
QEMU Raspberry Pi 3B
        │
        ▼
 AArch64 boot code ──► EL1 kernel and MMU
                            │
               ┌────────────┴────────────┐
               ▼                         ▼
        Kernel shell                User process (EL0)
        threads and scheduler       syscalls and trap frames
                                         │
                         ┌───────────────┴──────────────┐
                         ▼                              ▼
                  Demand paging                 fork with COW
```

On a user page fault, the kernel checks the process's memory regions and maps the requested page when the access is valid. A fault outside a valid region terminates that process. `fork` initially shares normal user pages as read-only; a write fault creates a private copy, while page reference counts keep shared frames alive until their final mapping is released.

## Build and run

Required tools are GNU Make, the AArch64 GNU compiler/binutils, `cpio`, and `qemu-system-aarch64`.

From the repository root:

```sh
make
make run
```

`make run` builds the user test images, packages `rootfs/` into `initramfs.cpio`, builds the kernel, and starts QEMU. The UART shell is available in the terminal; QEMU's normal display is enabled.

Available shell commands include:

```text
thread
process vm_adv_test.img
process mmap_test.img
process vm_fault_test.img
process vm_unmapped_test.img
process signal_test.img
```

The test images are generated from assembly sources in `user/` whenever `make run` is invoked. Running `process` without a filename looks for the optional local `syscall.img`; that prebuilt payload is intentionally excluded from this repository.

## Memory tests

| Image | Behavior checked |
|---|---|
| `vm_adv_test.img` | Demand allocation of code, stack, and anonymous memory; parent/child/grandchild COW isolation across repeated forks. |
| `mmap_test.img` | Anonymous user mapping and first-touch behavior. |
| `vm_fault_test.img` | A write to a read-only mapping is rejected and terminates the process. |
| `vm_unmapped_test.img` | An access outside registered user regions is rejected. |
| `signal_test.img` | Signal handler entry and return to the interrupted user context. |

The prebuilt `rootfs/syscall.img` payload is kept local and is not distributed with this repository. The test images listed above are built from the included user-program sources.

## Source layout

| Path | Contents |
|---|---|
| `kernel/src/boot.S`, `kernel/src/schedule.S` | Startup, exception entry, context switching, and user-mode return. |
| `kernel/src/` | Kernel initialization, scheduler, system calls, MMU, virtual memory, and device support. |
| `kernel/include/` | Kernel interfaces and data structures. |
| `common/src/`, `common/include/` | UART, CPIO, locking, and shared runtime utilities. |
| `user/` | User-program assembly sources and build rules. |
| `rootfs/` | Runtime files and locally generated user-program images. |
| `THIRD_PARTY_NOTICES.md` | Attribution and license information for the included Raspberry Pi DTB. |
| `datasheet/` | BCM2835/2836/2837 hardware reference documents. |

## License

Original source code and project documentation are licensed under the MIT License. Third-party materials listed in `THIRD_PARTY_NOTICES.md` remain under their respective licenses.

## Scope

This is a small kernel implementation focused on core operating-system mechanisms. Anonymous private mappings, basic process operations, and the listed system calls are supported; it does not aim to provide a complete POSIX environment or a general-purpose filesystem.

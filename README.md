# AV6

A small UNIX-like OS for AArch64. It boots on QEMU `virt` and brings up the cores.
Its user programs are hand-written assembly, because there is no libc yet.

> I am making this as a fun OS. It will only ever support AArch64. It is never meant
> to be big like Linux or the BSDs. It will always be a hobby operating system.

It started out as a straight reimplementation of UNIX V6 for AArch64. And the name
comes from combining AArch64 and V6, so A(Arch64)V6, which is AV6. Then it turned out
that V6 has no GICv3 driver, no ASIDs, no PAN, and no slab allocator with per-CPU
magazines. So "V6" is now mostly the name and a fond memory.

## What works

Boot and SMP bring-up over PSCI, GICv3 and the generic timer, TTBR0/TTBR1 with ASIDs and a
direct map of physical memory, PAN-armed `copyin`/`copyout` that returns an error instead
of panicking when handed a bad pointer, a page allocator, a slab allocator, real
`fork`/`exec`/`exit`/`wait`/`kill`, and nine syscalls.

## What does not

Files. Also descriptors, a filesystem, pipes, signals, mmap, a shell, a libc, threads,
and networking. `read` and `write` go to the console because there is nowhere else for
them to go.

## Build and run

You need `aarch64-linux-gnu-gcc` and `qemu-system-aarch64`, or the equivalents:
`make CROSS=<prefix> QEMU=<binary>`.

```sh
make
make run          # ctrl-a x to quit
make check        # boots a test kernel and grades itself
```

Add `CPUS=8 MEM=2` if four cores and a gigabyte feel cramped.

Nothing here is stable or finished, and none of it should go near hardware you are fond of.

## License

BSD 3-Clause. See [LICENSE](LICENSE).

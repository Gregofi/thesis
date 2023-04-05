# Debugger
A simple debugger written for Linux to demonstrate how `ptrace` can be used for
its implementation. This debugger is not indented to be used in real world, it
is proof of concept. To use it, look at the `debugee.c`, which has asm directive
to insert a breakpoint. This is recommended, since we have no clue where the
main function starts.

Simplicity is favored, robust error checking is omitted for its sake.

Example of usage
```
c
> Hit first breakpoint in main at <address>
m w <address> 0x90
> Rewrite the breakpoint with NOP
b <address> + 1
c
> Hit breakpoint at <address> + 1
c
> Hit breakpoint in foo at <address>
m w <address> 0x90
s o
> Step out of foo
c
> debugee finished execution
```
Since the bp inserted by the `__asm__` directive was not set by the debugger,
we need to manually unset them. Otherwise as seen with the breakpoint set with
`b`, those will be properly skipped when `c` is used. Try to call `c` on the
first breakpoint and see what happens.

Commands are single letters:
- `c` - continue
- `b <address>` - Sets breakpoint at address
- `x` - exit
- `m w <address> <1 byte value>` - Writes into memory at `<address>` an 1 byte
  value (can also be code).
- `m r <address>` - Reads 1B memory at address.
- `r r <reg>` - Reads register named `<reg>` (`rip`, `rbp`, `rax`, ...)
- `r w <reg> <val>` - Writes an 8B value to register.
- `s s` - Single step
- `s o` - Step out - Beware, this doesn't really work outside of main and in
  some dynamic functions, since they do not properly set their return address
  every time. The code is dependent on return address being set on `rbp + 8`,
  so if you step out of function right at the beginning it may also not work.
## How to build
`gcc -D_XOPEN_SOURCE=700 main.c`

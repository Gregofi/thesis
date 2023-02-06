#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <unistd.h>
#include <sys/user.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MAX_BREAKPOINTS 64
#define BP_OPCODE 0xCC
#define STR_EQ(S1, S2) (strcmp(S1, S2) == 0)

struct breakpoint {
    uint64_t address;
    uint8_t backup;
    bool enabled;
};

struct breakpoint breakpoints[MAX_BREAKPOINTS];
size_t bp_cnt;

uint64_t* get_register(struct user_regs_struct* regs, const char* name);
uint64_t read_register(pid_t pid, const char* name);
void set_register(pid_t pid, const char* name, uint64_t data);
void set_bp(pid_t pid, uint64_t address);
void step_over_bp(pid_t pid);

siginfo_t get_siginfo(pid_t pid) {
    siginfo_t inf;
    ptrace(PTRACE_GETSIGINFO, pid, NULL, &inf);
    return inf;
}

void sigtrap(pid_t pid, siginfo_t inf) {
    switch(inf.si_code) {
    case SI_KERNEL:
    case TRAP_BRKPT: {
        uint64_t pc = read_register(pid, "rip") - 1;
        set_register(pid, "rip", pc);
        // Currently, this gets hit even in step out, which is unfortunate.
        printf("Hit breakpoint at address 0x%lx\n", pc);
        break;
    }
    case TRAP_TRACE:
        // Single step
        break;
    default:
        fprintf(stderr, "Unknown trap code '%d'\n", inf.si_code);
    }
}

int wait_for_debugee(pid_t pid) {
    int status;
    waitpid(pid, &status, 0);
    siginfo_t inf = get_siginfo(pid);
    switch (inf.si_signo) {
    case SIGTRAP:
        sigtrap(pid, inf);
        break;
    case SIGSEGV:
        fprintf(stderr, "Debugee received SIGSEGV, segmentation fault.\n");
        return 1;
    default:
        printf("Received signal '%s'\n", strsignal(inf.si_signo));
        break;
    }

    return status;
}

void cont(pid_t pid) {
    step_over_bp(pid);
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    wait_for_debugee(pid);
}

uint8_t read_memory(pid_t pid, uint64_t address) {
    uint64_t data = ptrace(PTRACE_PEEKDATA, pid, address, NULL);
    return (uint8_t)data;
}

void write_memory(pid_t pid, uint64_t address, uint8_t data) {
    uint64_t old_data = ptrace(PTRACE_PEEKDATA, pid, address, NULL);
    uint64_t new_data = (old_data & ~0xFF) | data;
    ptrace(PTRACE_POKEDATA, pid, address, new_data);
}

uint64_t* get_register(struct user_regs_struct* regs, const char* name) {
#define FETCH_REG(NAME)       \
    if (STR_EQ(name, #NAME)) { \
        return (uint64_t*)&regs->NAME;   \
    }
    FETCH_REG(r15);
    FETCH_REG(r14);
    FETCH_REG(r13);
    FETCH_REG(r12);
    FETCH_REG(rbp);
    FETCH_REG(rbx);
    FETCH_REG(r11);
    FETCH_REG(r10);
    FETCH_REG(r9);
    FETCH_REG(r8);
    FETCH_REG(rax);
    FETCH_REG(rcx);
    FETCH_REG(rdx);
    FETCH_REG(rsi);
    FETCH_REG(rdi);
    FETCH_REG(orig_rax);
    FETCH_REG(rip);
    FETCH_REG(cs);
    FETCH_REG(eflags);
    FETCH_REG(rsp);
    FETCH_REG(ss);
    FETCH_REG(fs_base);
    FETCH_REG(gs_base);
    FETCH_REG(ds);
    FETCH_REG(es);
    FETCH_REG(fs);
    FETCH_REG(gs);
    fprintf(stderr, "Unknown register named '%s'\n", name);
    return NULL;
#undef FETCH_REG
}

uint64_t read_register(pid_t pid, const char* name) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    uint64_t* data = get_register(&regs, name);
    if (data == NULL) {
        return 0;
    }
    return *data;
}

void set_register(pid_t pid, const char* name, uint64_t data) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    uint64_t* reg_data = get_register(&regs, name);
    *reg_data = data;
    ptrace(PTRACE_SETREGS, pid, NULL, &regs);
}

void enable(pid_t pid, struct breakpoint* bp) {
    if (bp->enabled) {
        return;
    }
    bp->backup = read_memory(pid, bp->address);
    write_memory(pid, bp->address, BP_OPCODE);
    bp->enabled = true;
}

void disable(pid_t pid, struct breakpoint* bp) {
    if (!bp->enabled) {
        return;
    }
    write_memory(pid, bp->address, bp->backup);
    bp->enabled = false;
}

/// Returns bp if exists, otherwise returns NULL
struct breakpoint* find_bp(uint64_t address) {
    for (size_t i = 0; i < bp_cnt; ++i) {
        if (breakpoints[i].address == address) {
            return &breakpoints[i];
        }
    }
    return NULL;
}

/// Creates new breakpoint at address or returns existing one.
/// Returns NULL if maximum number of bps has been reached.
struct breakpoint* new_bp(uint64_t address) {
    struct breakpoint* bp = find_bp(address);
    if (bp != NULL) {
        return bp;
    }

    if (bp_cnt >= MAX_BREAKPOINTS) {
        return NULL;
    }

    bp = &breakpoints[bp_cnt++];
    bp->address = address;
    return bp;
}

void set_bp(pid_t pid, uint64_t address) {
    struct breakpoint* bp = new_bp(address);
    if (bp == NULL) {
        fprintf(stderr, "Max breakpoints reached, exiting...");
    }
    
    enable(pid, bp);
}

void single_step(pid_t pid) {
    if (read_memory(pid, read_register(pid, "rip")) == BP_OPCODE) {
        step_over_bp(pid);
    } else {
        ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL);
    }
}

/// Singlesteps over breakpoint if there is one, otherwise does nothing
void step_over_bp(pid_t pid) {
    uint64_t loc = read_register(pid, "rip");
    struct breakpoint* bp = find_bp(loc);
    if (bp == NULL) {
        return;
    }

    disable(pid, bp);
    ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL);
    wait_for_debugee(pid);
    enable(pid, bp);
}

void step_out(pid_t pid) {
    uint64_t out_idx_stack = read_register(pid, "rbp") + 8;
    uint64_t out_addr = ptrace(PTRACE_PEEKDATA, pid, out_idx_stack, NULL);
    struct breakpoint bp;
    bp.address = out_addr;
    bp.enabled = false;
    enable(pid, &bp);
    cont(pid);
    disable(pid, &bp);
}

int debug_loop(pid_t pid) {
    char command = 0;
    while (command != EOF) {
        scanf(" %c", &command);
        switch (command) {
        // continue
        case 'c':
            cont(pid);
            break;
        // break
        case 'b': {
            uint64_t address;
            scanf(" %lx", &address);
            printf("Setting a breakpoint on address %lx\n", address);
            set_bp(pid, address);
            break;
        }
        // step
        case 's': {
            char subcommand;
            scanf(" %c", &subcommand);
            if (subcommand == 's') {
                single_step(pid);
            } else if (subcommand == 'o') {
                step_out(pid);
            } else {
                printf("step subcommand - 's' for singlestep, 'o' for step out\n");
            }
            break;
        }
        // memory
        case 'm': {
            char c;
            uint64_t addr;
            scanf(" %c %lx", &c, &addr);
            if (c == 'w') {
                uint8_t data;
                scanf(" %hhx", &data);
                printf("Writing data '0x%x' at address '0x%lx'\n", data, addr);
                write_memory(pid, addr, data);
            } else if (c == 'r') {
                uint8_t data = read_memory(pid, addr);
                printf("Data at '0x%lx' = '0x%hhX'\n", addr, data);
            } else {
                fprintf(stderr, "memory subcommand usage: m r|w addr\n"
                                "For example 'm r 0x100000'\n");
            }
            break;
        }
        // register
        case 'r' : {
            char subcommand;
            char reg[10];
            scanf(" %c %9s", &subcommand, reg);
            // read register
            if (subcommand == 'r') {
                uint64_t data = read_register(pid, reg);
                printf("Contents of register '%s' are %ld (0x%lx)\n", reg,
                        data, data);
            } else if (subcommand == 'w') {
                uint64_t data;
                scanf(" %lx", &data);
                set_register(pid, reg, data);
            } else {
                fprintf(stderr, "The register subcommand usage: r r|w reg\n"
                                "For example 'r r rip'\n");
            }
            break;
        }
        // exit
        case 'x': 
            goto END;        
        }
    }
END:
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: debugger debugee\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        personality(ADDR_NO_RANDOMIZE);
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        execve(argv[1], argv + 1, NULL);
    } else {
        printf("Debugging process with PID %d\n", pid);
        wait_for_debugee(pid);
        debug_loop(pid);
    }
}


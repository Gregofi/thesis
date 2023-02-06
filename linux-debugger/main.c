#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/personality.h>
#include <sys/wait.h>
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

int wait_for_debugee(pid_t pid) {
    int status;
    waitpid(pid, &status, 0);
    return status;
}

void cont(pid_t pid) {
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    wait_for_debugee(pid);
}

uint8_t read_memory(pid_t pid, uint64_t address) {
    uint64_t data = ptrace(PTRACE_PEEKDATA, pid, address, NULL);
    return (uint8_t)data;
}

void write_memory(pid_t pid, uint64_t address, uint8_t data) {
    uint64_t old_data = read_memory(pid, address);
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

uint64_t set_register(pid_t pid, const char* name, uint64_t data) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    uint64_t* reg_data = get_register(&regs, name);
    *reg_data = data;
    ptrace(PTRACE_SETREGS, pid, NULL, &regs);
}

void enable(pid_t pid, struct breakpoint* bp) {
    bp->backup = read_memory(pid, bp->address);
    write_memory(pid, bp->address, BP_OPCODE);
    bp->enabled = true;
}

void disable(pid_t pid, struct breakpoint* bp) {
    write_memory(pid, bp->address, bp->backup);
    bp->enabled = false;
}

/// Returns 0 if found, 1 if not found and returned new and
/// 2 if max breakpoints reached
int find_bp(uint64_t address, struct breakpoint* bp) {
    for (size_t i = 0; i < bp_cnt; ++i) {
        if (breakpoints[i].address == address) {
            bp = &breakpoints[i];
            return 0;
        }
    }
    if (bp_cnt >= MAX_BREAKPOINTS) {
        fprintf(stderr, "Maximum number of breakpoints reached.\n");
        return 2;
    }
    bp = &breakpoints[bp_cnt++];
    return 1;
}

void set_bp(pid_t pid, uint64_t address) {
    struct breakpoint* bp = NULL;
    int ret = find_bp(address, bp);
    if (ret == 2) {
        return;
    } else if (ret == 0) {
        fprintf(stderr, "Breakpoint is already set at address %lx\n", address);
        return;
    }
    
    enable(pid, bp);
}

int debug_loop(pid_t pid) {
    while (true) {
        char command;
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
        case 's':
            break;
        // enable breakpoint
        case 'e':
            break;
        // disable breakpoint
        case 'd':
            break;
        // memory
        case 'm': {
            char c;
            uint64_t addr;
            scanf(" %c %lx", &c, &addr);
            if (c == 'w') {
                uint8_t data;
                scanf(" %lx", &data);
                printf("Writing data '%x' at address '%lx'\n", data, addr);
            } else if (c == 'r') {
                uint8_t data = read_memory(pid, addr);
                printf("Data at '%lx' = '%hhx'\n", addr, data);
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
                printf("Contents of register '%s' are %lld (0x%llx)\n", reg,
                        data, data);
            } else if (subcommand == 'w') {
                uint64_t data;
                scanf(" %llx", &data);
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


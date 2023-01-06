#include <vector>
#include <string>
#include <string_view>
#include <iostream>
#include <iomanip>
#include <map>
#include <ranges>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/personality.h>
#include <sys/wait.h>

enum class reg {
    r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8, rax, rcx,
    rdx, rsi, rdi, orig_rax, rip, cs, eflags, rsp, ss, fs_base,
    gs_base, ds, es, fs, gs,
};

static const std::map<std::string_view, reg> register_map = {
    { "r15", reg::r15 },
    { "r14", reg::r14 },
    { "r13", reg::r13 },
    { "r12", reg::r12 },
    { "rbp", reg::rbp },
    { "rbx", reg::rbx },
    { "r11", reg::r11 },
    { "r10", reg::r10 },
    { "r9", reg::r9 },
    { "r8", reg::r8 },
    { "rax", reg::rax },
    { "rcx", reg::rcx },
    { "rdx", reg::rdx },
    { "rsi", reg::rsi },
    { "orig_rax", reg::orig_rax },
    { "rip", reg::rip },
    { "cs", reg::cs },
    { "eflags", reg::eflags },
    { "rsp", reg::rsp },
    { "ss", reg::ss },
    { "fs_base", reg::fs_base },
    { "gs_base", reg::gs_base },
    { "ds", reg::ds },
    { "es", reg::es },
    { "fs", reg::fs },
    { "gs", reg::gs },
};

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> res;
    for (auto word: std::views::split(s, delim)) {
        res.emplace_back(word.begin(), word.end());
    }
    return res;
}

bool is_prefix(std::string_view prefix, std::string_view of) {
    if (prefix.size() > of.size()) {
        return false;
    }
    auto [res, _] = std::mismatch(prefix.begin(), prefix.end(), of.begin());
    return res == prefix.end();
}

class Breakpoint {
public:
    Breakpoint(uint8_t backup): active(true), backup(backup) { }
private:
    bool active;
    uint8_t backup;
};

class Debugger {
public:
    Debugger(std::string program_name, pid_t pid)
        : program_name(std::move(program_name)), pid(pid) {

    }
    
    void run() {
        int wait_status;
        int options = 0;
        waitpid(pid, &wait_status, options);
        
        while (true) {
            std::cout << "> ";
            std::string line;
            if (!std::getline(std::cin, line)) {
                break;
            }
            auto args = split(line, ' ');
            if (args.size() == 0) {
                continue;
            }
            
            if (is_prefix(args[0], "continue")) {
                continue_exec();
            } else if (is_prefix(args[0], "break")) {
                set_breakpoint(std::stoul(args.at(1), nullptr, 16));
            } else if (is_prefix(args[0], "register")) {
                if (is_prefix(args[1], "dump")) {
                    dump_registers();
                } else if (is_prefix(args[1], "read")) {
                    auto val = read_register(args[2]);
                    output_register(args[2], val);
                } else {
                    std::cerr << "Unknown register command" << std::endl;
                }
            } else if (is_prefix(args[0], "step")) {
                do_single_step();
            } else {
                std::cerr << "Unknown command" << std::endl;
            }
        }
    }
private:
    static const uint8_t INT3 = 0xCC;

    void set_breakpoint(uintptr_t address) {
        uint64_t original_data;
        ptrace(PTRACE_PEEKTEXT, pid, &original_data, nullptr);

        uint8_t backup = original_data & 0xFF;
        breakpoints.emplace(address, Breakpoint{backup});

        ptrace(PTRACE_POKETEXT, pid, address, (original_data & ~0xFF) | INT3); 
        std::cout << "Breakpoint was set at " << address << "\n";
    }

    void do_single_step() {
        ptrace(PTRACE_SINGLESTEP, pid, nullptr, nullptr);
    }

    void continue_exec() {
        ptrace(PTRACE_CONT, pid, nullptr, nullptr);
        int wait_status;
        int options = 0;
        waitpid(pid, &wait_status, options);
    }

    void output_register(std::string_view name, uint64_t value) {
        std::cout << name << " = " << "0x" << std::setfill('0') << std::setw(16) << std::hex << value << "\n";
    }

    uint64_t read_register(std::string_view name) {
        // This structure can be found at /usr/include/sys/user.h
        user_regs_struct regs;
        ptrace(PTRACE_GETREGS, pid, nullptr, &regs);
        auto reg = register_map.at(name);
        return *(reinterpret_cast<uint64_t*>(&regs) + static_cast<int>(reg));
    }

    void dump_registers() {
        // Not particulary effective, since we're requesting ptrace call for each register
        // whereas we could do with one. But it is short to write.
        for (auto [reg_name, _]: register_map) {
            output_register(reg_name, read_register(reg_name));
        }
    }

    std::map<uintptr_t, Breakpoint> breakpoints;
    std::string program_name;
    pid_t pid;
};

auto main(int argc, char* argv[]) -> int {
    if (argc < 2) {
        std::cerr  << "usage: debugger executable\n";
        return 1;
    }

    auto program = argv[1];
    auto pid = fork();
    if (pid == 0) {
        // child
        personality(ADDR_NO_RANDOMIZE);
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        execl(program, program, nullptr);
    } else {
        // parent
        std::cout << "Begin debugging process " << pid << "\n";
        Debugger dbg{program, pid};
        dbg.run();
    }
};

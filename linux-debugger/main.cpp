#include <vector>
#include <string>
#include <string_view>
#include <iostream>
#include <map>
#include <ranges>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/personality.h>
#include <sys/wait.h>

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> res;
    for (auto word: std::views::split(s, delim)) {
        res.emplace_back(word.begin(), word.end());
    }
    return res;
}

bool is_prefix(std::string_view prefix, std::string_view of) {
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
            
            if (is_prefix(args[0], "continue")) {
                continue_exec();
            } else if (is_prefix(args[0], "break")) {
                set_breakpoint(std::stoul(args.at(1), nullptr, 16));
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

    void continue_exec() {
        ptrace(PTRACE_CONT, pid, nullptr, nullptr);
        int wait_status;
        int options = 0;
        waitpid(pid, &wait_status, options);
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
}
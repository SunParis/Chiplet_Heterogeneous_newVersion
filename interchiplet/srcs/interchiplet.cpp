
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ctime>
#include <vector>

#include "cmdline_options.h"
#include "global_manager.h"
#include "spdlog/spdlog.h"

std::vector<ProcessStruct *> proc_struct_list;
InterChiplet::GlobalManager *GM;

void parse_command(char* __pipe_buf, ProcessStruct* __proc_struct, int __stdin_fd) {
    // Split line by '\n'
    std::string line = std::string(__pipe_buf);
    std::vector<std::string> lines;

    int start_idx = 0;
    for (std::size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\n') {
            std::string l = line.substr(start_idx, i + 1 - start_idx);
            start_idx = i + 1;
            lines.push_back(l);
        }
    }
    if (start_idx < line.size()) {
        std::string l = line.substr(start_idx, line.size() - start_idx);
        lines.push_back(l);
    }

    // Unfinished line.
    if (__proc_struct->m_unfinished_line.size() > 0) {
        lines[0] = __proc_struct->m_unfinished_line + lines[0];
        __proc_struct->m_unfinished_line = "";
    }
    if (lines[lines.size() - 1].find('\n') == -1) {
        __proc_struct->m_unfinished_line = lines[lines.size() - 1];
        lines.pop_back();
    }

    // Get line start with [INTERCMD]
    for (std::size_t i = 0; i < lines.size(); i++) {
        std::string l = lines[i];
        if (l.substr(0, 10) == "[INTERCMD]") {
            InterChiplet::SyncCommand cmd = InterChiplet::parseCmd(l);
            cmd.m_stdin_fd = __stdin_fd;
            cmd.m_clock_rate = __proc_struct->m_clock_rate;
            cmd.m_cycle = cmd.m_cycle / __proc_struct->m_clock_rate;

            if (cmd.m_cycle > 0) {
                GM->update_proc_time(__proc_struct, cmd.m_cycle);
            }

            {
                const std::lock_guard<std::mutex> lock(GM->m_cmd_handler_lock);
                // Check order and clear delay infomation.
                if (cmd.m_type == InterChiplet::SC_BARRIER || cmd.m_type == InterChiplet::SC_SEND ||
                    cmd.m_type == InterChiplet::SC_LOCK || cmd.m_type == InterChiplet::SC_UNLOCK ||
                    cmd.m_type == InterChiplet::SC_LAUNCH) {
                    InterChiplet::SyncCommand write_cmd = cmd;
                    if (cmd.m_type == InterChiplet::SC_BARRIER) {
                        write_cmd.m_desc |= InterChiplet::SPD_BARRIER;
                        write_cmd.m_desc |= write_cmd.m_nbytes;
                    } else if (cmd.m_type == InterChiplet::SC_LOCK) {
                        write_cmd.m_desc |= InterChiplet::SPD_LOCK;
                    } else if (cmd.m_type == InterChiplet::SC_UNLOCK) {
                        write_cmd.m_desc |= InterChiplet::SPD_UNLOCK;
                    } else if (cmd.m_type == InterChiplet::SC_LAUNCH) {
                        write_cmd.m_desc |= InterChiplet::SPD_LAUNCH;
                    }
                }

                spdlog::debug("Process {}:: {}", __proc_struct->m_pid, InterChiplet::dumpCmd(cmd));

                // Call functions to handle corresponding command.
                switch (cmd.m_type) {
                    case InterChiplet::SC_CYCLE:
                        GM->handle_cycle_cmd(cmd, __proc_struct);
                        break;
                    case InterChiplet::SC_BARRIER:
                        GM->handle_barrier_cmd(cmd, __proc_struct);
                        break;
                    case InterChiplet::SC_SEND:
                    case InterChiplet::SC_RECEIVE:
                        GM->handle_pipe_cmd(cmd, __proc_struct);
                        break;
                    case InterChiplet::SC_LOCK:
                        GM->handle_lock_cmd(cmd, __proc_struct);
                        break;
                    case InterChiplet::SC_UNLOCK:
                        GM->handle_unlock_cmd(cmd, __proc_struct);
                        break;
                    case InterChiplet::SC_LAUNCH:
                        GM->handle_launch_cmd(cmd, __proc_struct);
                        break;
                    case InterChiplet::SC_WAITLAUNCH:
                        GM->handle_waitlaunch_cmd(cmd, __proc_struct);
                        break;
                    case InterChiplet::SC_READ:
                        GM->handle_read_cmd(cmd, __proc_struct);
                        break;
                    case InterChiplet::SC_WRITE:
                        GM->handle_write_cmd(cmd, __proc_struct);
                        break;
                    default:
                        break;
                }
            }            

            if (cmd.m_cycle > 0) {
                GM->update_proc_time(__proc_struct, cmd.m_cycle);
            }

        }
    }
}

void parse_popnet_command(char* __pipe_buf, PopnetProcess* __proc_struct, int __stdin_fd) {
    // Split line by '\n'
    std::string line = std::string(__pipe_buf);
    std::vector<std::string> lines;

    int start_idx = 0;
    for (std::size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\n') {
            std::string l = line.substr(start_idx, i + 1 - start_idx);
            start_idx = i + 1;
            lines.push_back(l);
        }
    }
    if (start_idx < line.size()) {
        std::string l = line.substr(start_idx, line.size() - start_idx);
        lines.push_back(l);
    }

    // Unfinished line.
    if (__proc_struct->m_unfinished_line.size() > 0) {
        lines[0] = __proc_struct->m_unfinished_line + lines[0];
        __proc_struct->m_unfinished_line = "";
    }
    if (lines[lines.size() - 1].find('\n') == -1) {
        __proc_struct->m_unfinished_line = lines[lines.size() - 1];
        lines.pop_back();
    }

    // Get line start with [INTERCMD]
    for (std::size_t i = 0; i < lines.size(); i++) {
        std::string l = lines[i];
        if (l.substr(0, 10) == "[INTERCMD]") {
            InterChiplet::SyncCommand cmd = InterChiplet::parseCmd(l);
            cmd.m_stdin_fd = __stdin_fd;
            cmd.m_clock_rate = __proc_struct->m_clock_rate;
            cmd.m_cycle = cmd.m_cycle / __proc_struct->m_clock_rate;

            // Call functions to handle corresponding command.
            switch (cmd.m_type) {
                case InterChiplet::SC_CYCLE:
                    GM->update_popnet_time(cmd.m_cycle);
                    break;
            }
        }
    }

    if (__proc_struct->m_has_wrote_end_flag_) {
        return;
    }

}

#define PIPE_BUF_SIZE 1024

void simlet_process(ProcessStruct *__args_ptr) {
    ProcessStruct* proc_struct = __args_ptr;
    proc_struct->m_thread_id = std::this_thread::get_id();

    int pipe_stdin[2];   // Pipe to send data to child process
    int pipe_stdout[2];  // Pipe to receive data from child process
    int pipe_stderr[2];  // Pipe to receive data from child process

    // Create pipes
    if (pipe(pipe_stdin) == -1 || pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
        throw std::runtime_error("Failed to create pipes");
        exit(EXIT_FAILURE);
    }

    // Create sub directory for subprocess.
    char* sub_dir_path = new char[128];
    sprintf(sub_dir_path, "./proc_t%d", proc_struct->m_thread);
    if (access(sub_dir_path, F_OK) == -1) {
        mkdir(sub_dir_path, 0775);
    }

    // Fork a child process
    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("Failed to fork process");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {  // Child process
        // Close unnecessary pipe ends
        close(pipe_stdin[1]);
        close(pipe_stdout[0]);
        close(pipe_stderr[0]);

        // Redirect stdin and stdout to pipes
        dup2(pipe_stdin[0], STDIN_FILENO);
        dup2(pipe_stdout[1], STDOUT_FILENO);
        dup2(pipe_stderr[1], STDERR_FILENO);

        // Change directory to sub-process.
        if (chdir(sub_dir_path) < 0) {
            throw std::runtime_error("Failed to change directory");
            exit(EXIT_FAILURE);
        }
        // TODO: Copy necessary configuration file.
        if (!proc_struct->m_pre_copy.empty()) {
            std::string cp_cmd = std::string("cp ") + proc_struct->m_pre_copy + " .";
            if (system(cp_cmd.c_str()) != 0) {
                throw std::runtime_error("Failed to copy file");
                exit(EXIT_FAILURE);
            }
        }

        std::cout << "CWD: " << get_current_dir_name() << std::endl;

        // Build arguments.
        int argc = proc_struct->m_args.size();
        char** args_list = new char*[argc + 2];
        args_list[0] = new char[proc_struct->m_command.size() + 1];
        strcpy(args_list[0], proc_struct->m_command.c_str());
        args_list[0][proc_struct->m_command.size()] = '\0';
        for (int i = 0; i < argc; i++) {
            int arg_len = proc_struct->m_args[i].size();
            args_list[i + 1] = new char[arg_len + 1];
            strcpy(args_list[i + 1], proc_struct->m_args[i].c_str());
            args_list[i + 1][arg_len] = '\0';
        }
        args_list[argc + 1] = NULL;

        // Execute the child program
        std::cout << "Exec: ";
        for (int i = 0; i < proc_struct->m_args.size() + 1; i++) {
            std::cout << " " << args_list[i];
        }
        std::cout << std::endl;
        execvp(args_list[0], args_list);

        // If execl fails, it means the child process couldn't be started
        throw std::runtime_error("Failed to execute command");
        exit(EXIT_FAILURE);
    } else {  // Parent process
        spdlog::info("Start simulation process {}. Command: {}", pid, proc_struct->m_command);
        proc_struct->m_pid = pid;

        // Close unnecessary pipe ends
        close(pipe_stdin[0]);
        close(pipe_stdout[1]);
        close(pipe_stderr[1]);
        int stdin_fd = pipe_stdin[1];
        int stdout_fd = pipe_stdout[0];
        int stderr_fd = pipe_stderr[0];

        pollfd fd_list[2] = {{fd : stdout_fd, events : POLL_IN},
                             {fd : stderr_fd, events : POLL_IN}};

        // Move log to subfolder.
        std::ofstream log_file(std::string(sub_dir_path) + "/" + proc_struct->m_log_file);

        // Write execution start time to log file.
        std::time_t t = std::time(0);
        std::tm* now = std::localtime(&t);
        log_file << "Execution starts at " << (now->tm_year + 1900) << "-" << (now->tm_mon + 1)
                 << "-" << now->tm_mday << "  " << (now->tm_hour) << ":" << (now->tm_min) << ":"
                 << (now->tm_sec) << std::endl;
        proc_struct->m_state = PS_RUNNING;

        char* pipe_buf = new char[PIPE_BUF_SIZE + 1];
        bool to_stdout = proc_struct->m_to_stdout;
        int res = 0;
        while (true) {
            int res = poll(fd_list, 2, 1000);
            if (res == -1) {
                spdlog::error("Process {} poll error: {}", proc_struct->m_pid, strerror(errno));
                break;
            }

            bool has_stdout = false;
            if (fd_list[0].revents & POLL_IN) {
                has_stdout = true;
                int res = read(stdout_fd, pipe_buf, PIPE_BUF_SIZE);
                if (res <= 0) break;
                pipe_buf[res] = '\0';
                // log redirection.
                log_file.write(pipe_buf, res).flush();
                if (to_stdout) {
                    std::cout.write(pipe_buf, res);
                    std::cout.flush();
                }
                // Parse command in pipe_buf
                parse_command(pipe_buf, proc_struct, stdin_fd);
            }
            if (fd_list[1].revents & POLL_IN) {
                has_stdout = true;
                int res = read(stderr_fd, pipe_buf, PIPE_BUF_SIZE);
                if (res <= 0) break;
                pipe_buf[res] = '\0';
                // log redirection.
                log_file.write(pipe_buf, res).flush();
                if (to_stdout) {
                    std::cerr.write(pipe_buf, res);
                    std::cerr.flush();
                }
                // Parse command in pipe_buf
                parse_command(pipe_buf, proc_struct, stdin_fd);
            }

            // Check the status of child process and quit.
            int status;
            if (!has_stdout && (waitpid(pid, &status, WNOHANG) > 0)) {
                // Optionally handle child process termination status
                if (status == 0) {
                    spdlog::info("Simulation process {} terminate with status = {}.",
                                 proc_struct->m_pid, status);
                } else {
                    spdlog::error("Simulation process {} terminate with status = {}.",
                                  proc_struct->m_pid, status);
                }
                GM->update_status(proc_struct, PS_END);
                break;
            }
        }

        delete pipe_buf;
    }

    return;
}

void popnet_process(PopnetProcess* __args_ptr) {
    PopnetProcess* proc_struct = __args_ptr;
    proc_struct->m_thread_id = std::this_thread::get_id();
    proc_struct->m_state = PS_RUNNING;

    int pipe_stdin[2];   // Pipe to send data to child process
    int pipe_stdout[2];  // Pipe to receive data from child process
    int pipe_stderr[2];  // Pipe to receive data from child process

    // Create pipes
    if (pipe(pipe_stdin) == -1 || pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
        throw std::runtime_error("Failed to create pipes");
        exit(EXIT_FAILURE);
    }

    // Create sub directory for subprocess.
    char* sub_dir_path = new char[128];
    sprintf(sub_dir_path, "./proc_popnet");
    if (access(sub_dir_path, F_OK) == -1) {
        mkdir(sub_dir_path, 0775);
    }

    // Fork a child process
    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("Failed to fork process");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {  // Child process
        // Close unnecessary pipe ends
        close(pipe_stdin[1]);
        close(pipe_stdout[0]);
        close(pipe_stderr[0]);

        // Redirect stdin and stdout to pipes
        dup2(pipe_stdin[0], STDIN_FILENO);
        dup2(pipe_stdout[1], STDOUT_FILENO);
        dup2(pipe_stderr[1], STDERR_FILENO);

        // Change directory to sub-process.
        if (chdir(sub_dir_path) < 0) {
            throw std::runtime_error("Failed to change directory");
            exit(EXIT_FAILURE);
        }
        // TODO: Copy necessary configuration file.
        if (!proc_struct->m_pre_copy.empty()) {
            std::string cp_cmd = std::string("cp ") + proc_struct->m_pre_copy + " .";
            if (system(cp_cmd.c_str()) != 0) {
                throw std::runtime_error("Failed to copy file");
                exit(EXIT_FAILURE);
            }
        }

        std::cout << "CWD: " << get_current_dir_name() << std::endl;

        // Build arguments.
        int argc = proc_struct->m_args.size();
        char** args_list = new char*[argc + 2];
        args_list[0] = new char[proc_struct->m_command.size() + 1];
        strcpy(args_list[0], proc_struct->m_command.c_str());
        args_list[0][proc_struct->m_command.size()] = '\0';
        for (int i = 0; i < argc; i++) {
            int arg_len = proc_struct->m_args[i].size();
            args_list[i + 1] = new char[arg_len + 1];
            strcpy(args_list[i + 1], proc_struct->m_args[i].c_str());
            args_list[i + 1][arg_len] = '\0';
        }
        args_list[argc + 1] = NULL;

        // Execute the child program
        std::cout << "Exec: ";
        for (int i = 0; i < proc_struct->m_args.size() + 1; i++) {
            std::cout << " " << args_list[i];
        }
        std::cout << std::endl;
        execvp(args_list[0], args_list);

        // If execl fails, it means the child process couldn't be started
        throw std::runtime_error("Failed to execute command");
        exit(EXIT_FAILURE);
    } else {  // Parent process
        spdlog::info("Start Popnet process {}. Command: {}", pid, proc_struct->m_command);
        proc_struct->m_pid = pid;

        // Close unnecessary pipe ends
        close(pipe_stdin[0]);
        close(pipe_stdout[1]);
        close(pipe_stderr[1]);
        int stdin_fd = pipe_stdin[1];
        int stdout_fd = pipe_stdout[0];
        int stderr_fd = pipe_stderr[0];

        pollfd fd_list[2] = {{fd : stdout_fd, events : POLL_IN},
                             {fd : stderr_fd, events : POLL_IN}};

        // Move log to subfolder.
        std::ofstream log_file(std::string(sub_dir_path) + "/" + proc_struct->m_log_file);

        // Write execution start time to log file.
        std::time_t t = std::time(0);
        std::tm* now = std::localtime(&t);
        log_file << "Execution starts at " << (now->tm_year + 1900) << "-" << (now->tm_mon + 1)
                 << "-" << now->tm_mday << "  " << (now->tm_hour) << ":" << (now->tm_min) << ":"
                 << (now->tm_sec) << std::endl;

        char* pipe_buf = new char[PIPE_BUF_SIZE + 1];
        bool to_stdout = proc_struct->m_to_stdout;
        int res = 0;
        while (true) {
            
            if (proc_struct->m_state == ProcessState::PS_SUSPEND) {
                spdlog::debug("Popnet process wating for restart.");
            }
            
            while (proc_struct->m_state == ProcessState::PS_SUSPEND) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            int res = poll(fd_list, 2, 1000);
            if (res == -1) {
                throw std::runtime_error("Poll error");
                break;
            }

            bool has_stdout = false;
            if (fd_list[0].revents & POLL_IN) {
                has_stdout = true;
                int res = read(stdout_fd, pipe_buf, PIPE_BUF_SIZE);
                if (res <= 0) break;
                pipe_buf[res] = '\0';
                // log redirection.
                log_file.write(pipe_buf, res).flush();
                if (to_stdout) {
                    std::cout.write(pipe_buf, res);
                    std::cout.flush();
                }
                // Parse command in pipe_buf
                parse_popnet_command(pipe_buf, proc_struct, stdin_fd);
            }
            if (fd_list[1].revents & POLL_IN) {
                has_stdout = true;
                int res = read(stderr_fd, pipe_buf, PIPE_BUF_SIZE);
                if (res <= 0) break;
                pipe_buf[res] = '\0';
                // log redirection.
                log_file.write(pipe_buf, res).flush();
                if (to_stdout) {
                    std::cerr.write(pipe_buf, res);
                    std::cerr.flush();
                }
                // Parse command in pipe_buf
                parse_popnet_command(pipe_buf, proc_struct, stdin_fd);
            }

            if (proc_struct->m_state == ProcessState::PS_SUSPEND) {
                spdlog::debug("Popnet process wating for restart.");
            }

            while (proc_struct->m_state == ProcessState::PS_SUSPEND) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Check the status of child process and quit.
            int status;
            if (!has_stdout && (waitpid(pid, &status, WNOHANG) > 0)) {
                // Optionally handle child process termination status
                if (status == 0) {
                    spdlog::info("Popnet process {} terminate with status = {}.",
                                 proc_struct->m_pid, status);
                } else {
                    spdlog::error("Popnet process {} terminate with status = {}.",
                                  proc_struct->m_pid, status);
                }
                break;
            }
        }

        delete pipe_buf;
    }

    return;
}

void main_process(
    const std::vector<ProcessConfig>& __proc_phase1_cfg_list,
    PopnetProcess *popnet_proc
) {
    // Create multi-thread.
    int thread_i = 0;

    std::vector<std::thread *> thread_pool;
    
    for (auto& proc_cfg : __proc_phase1_cfg_list) {
        ProcessStruct* proc_struct = new ProcessStruct(proc_cfg);
        proc_struct->m_thread = thread_i;
        thread_pool.push_back(new std::thread(&simlet_process, proc_struct));
        proc_struct_list.push_back(proc_struct);
        thread_i++;
    }
    std::thread *popnet_thread = new std::thread(&popnet_process, popnet_proc);

    // Wait threads to finish.
    for (auto& thread_item : thread_pool) {
        thread_item->join();
        delete thread_item;
    }
    popnet_proc->write_end_flag();
    popnet_thread->join();
    delete popnet_thread;

    for (auto& proc_struct : proc_struct_list) {
        delete proc_struct;
    }
    
    spdlog::info("All process has exit.");

    // Remove file.
    GM->clear_pipes();

    return;
}

int main(int argc, const char* argv[]) {
    // Parse command line.
    CmdLineOptions options;
    if (options.parse(argc, argv) != 0) {
        return 0;
    };

    // Initializate logging
    if (options.m_debug) {
        spdlog::set_level(spdlog::level::debug);
    }
    spdlog::info("==== LegoSim Chiplet Simulator ====");

    // Change working directory if --cwd is specified.
    if (options.m_cwd != "") {
        if (access(options.m_cwd.c_str(), F_OK) == 0) {
            if (chdir(options.m_cwd.c_str()) < 0) {
                throw std::runtime_error("Failed to change directory");
            }
            spdlog::info("Change working directory {}.", get_current_dir_name());
        }
    }

    // Check exist of benchmark configuration yaml.
    if (access(options.m_bench.c_str(), F_OK) < 0) {
        spdlog::error("Cannot find benchmark {}.", options.m_bench);
        exit(EXIT_FAILURE);
    }

    // Load benchmark configuration.
    BenchmarkConfig configs(options.m_bench);
    spdlog::info("Load benchmark configuration from {}.", options.m_bench);
    PopnetProcess *popnet = new PopnetProcess(configs.m_phase2_proc_cfg_list.front());
    GM = new InterChiplet::GlobalManager(popnet, proc_struct_list);

    // Get start time of simulation.
    struct timeval simstart, simend, roundstart, roundend;
    gettimeofday(&simstart, 0);

    InterChiplet::InnerTimeType sim_cycle = 0;
    for (int round = 1; round <= options.m_timeout_threshold; round++) {
        // Get start time of one round.
        gettimeofday(&roundstart, 0);
        spdlog::info("**** Round {} Phase 1 ****", round);
        main_process(configs.m_phase1_proc_cfg_list, popnet);
        break;
    }

    // Get end time of simulation.
    spdlog::info("**** End of Simulation ****");
    gettimeofday(&simend, 0);
    unsigned long elaped_sec = simend.tv_sec - simstart.tv_sec;
    spdlog::info("Benchmark elapses {} cycle.", static_cast<InterChiplet::TimeType>(sim_cycle));
    spdlog::info("Simulation elapseds {}d {}h {}m {}s.", elaped_sec / 3600 / 24,
                 (elaped_sec / 3600) % 24, (elaped_sec / 60) % 60, elaped_sec % 60);
}

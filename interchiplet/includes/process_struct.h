# pragma once 

# ifndef _PROCESS_STRUCT_H_
# define _PROCESS_STRUCT_H_ 1

# include <boost/filesystem.hpp>
# include <iostream>
# include <fstream>
# include <thread>
# include <sys/signal.h>

# include "net_delay.h"
# include "net_bench.h"
# include "benchmark_yaml.h"
# include "global_define.h"

# include "spdlog/spdlog.h"

namespace fs = boost::filesystem;

enum ProcessState {
    PS_INIT,
    PS_RUNNING,
    PS_SUSPEND,
    PS_END,
    PS_STRAIGHT_TO_END
};

static std::string processStateToString(ProcessState procst) {
    if (procst == PS_INIT) {
        return "PS_INIT";
    }
    else if (procst == PS_RUNNING) {
        return "PS_RUNNING";
    }
    else if (procst == PS_SUSPEND) {
        return "PS_SUSPEND";
    }
    else if (procst == PS_END) {
        return "PS_END";
    }
    return "UNKNOWN";
}

/**
 * @brief Data structure of process configuration.
 */
class ProcessStruct {

public:

    ProcessStruct(const ProcessConfig& __config)
    :   m_command(__config.m_command),
        m_args(__config.m_args),
        m_log_file(__config.m_log_file),
        m_to_stdout(__config.m_to_stdout),
        m_clock_rate(__config.m_clock_rate),
        m_pre_copy(__config.m_pre_copy),
        m_unfinished_line(),
        m_thread_id(),
        m_pid(-1),
        m_state(PS_INIT),
        m_pause_due_to_recv_sync(false),
        m_pause_due_to_send_sync(false),
        m_commun_simulationg(false)
    {}

public:

    // Configuration.
    std::string m_command;
    std::vector<std::string> m_args;
    std::string m_log_file;
    bool m_to_stdout;
    double m_clock_rate;
    std::string m_pre_copy;

    std::string m_unfinished_line;

    // Indentify
    int m_thread;
    std::thread::id m_thread_id;
    int m_pid;

    std::atomic<InterChiplet::InnerTimeType> m_current_time;
    std::atomic<ProcessState> m_state;

    bool m_pause_due_to_recv_sync;
    bool m_pause_due_to_send_sync;
    bool m_commun_simulationg;

};

class PopnetProcess {

public:

    std::thread::id m_thread_id;
    int m_pid;

    std::string m_command;
    std::vector<std::string> m_args;
    std::string m_log_file;
    bool m_to_stdout;
    double m_clock_rate;
    std::string m_pre_copy;

    std::string m_unfinished_line;

    std::atomic<InterChiplet::InnerTimeType> m_current_time;
    std::atomic<ProcessState> m_state;
        
    std::size_t m_file_size;

    std::atomic<InterChiplet::InnerTimeType> m_max_time;

    std::size_t m_pac_in_net;

    std::atomic_bool m_has_wrote_end_flag_;

    std::string m_trace_file;

    std::string m_delay_info;
    

    PopnetProcess(const ProcessConfig& __config)
    :   m_command(__config.m_command),
        m_args(__config.m_args),
        m_log_file(__config.m_log_file),
        m_to_stdout(__config.m_to_stdout),
        m_clock_rate(__config.m_clock_rate),
        m_pre_copy(__config.m_pre_copy),
        m_unfinished_line(),
        m_thread_id(),
        m_current_time(0),
        m_state(PS_INIT),
        m_file_size(0),
        m_max_time(0),
        m_pac_in_net(0),
        m_has_wrote_end_flag_(false),
        m_trace_file("bench.txt"),
        m_delay_info("delayInfo.txt")
    {
        std::ofstream trace_file(this->m_trace_file, std::ios::trunc);
        trace_file.flush();
        trace_file.close();
        std::ofstream delay_file(this->m_delay_info, std::ios::trunc);
        delay_file.flush();
        delay_file.close();
    }

    void write_new_rec(const NetworkBenchItem& bench_item) {
        std::ofstream trace_file(this->m_trace_file, std::ios::app);
        trace_file << bench_item.m_src_cycle << " " << bench_item.m_src_cycle << " ";
        for (auto& iter: bench_item.m_src) {
            trace_file << iter << " ";
        }
        for (auto& iter: bench_item.m_dst) {
            trace_file << iter << " ";
        }
        trace_file << bench_item.m_pac_size << " " << bench_item.m_desc;
        trace_file << std::endl;
        trace_file.close();
        this->m_pac_in_net += 2;
    }

    bool get_new_delay(std::vector<NetworkDelayItem>& res) {
        if (!fs::exists(this->m_delay_info)) {
            return false;
        }
        if (fs::file_size(this->m_delay_info) == this->m_file_size) {
            return false;
        }

        std::ifstream delay_info(this->m_delay_info);
        delay_info.seekg(this->m_file_size, std::ios::beg);
        NetworkDelayItem new_delay;
        new_delay.m_src.resize(2);
        new_delay.m_dst.resize(2);
        new_delay.m_delay_list.resize(2);
        int no_use = -1;
        while (delay_info >> new_delay.m_cycle) {
            delay_info >> new_delay.m_src[0];
            delay_info >> new_delay.m_src[1];
            delay_info >> new_delay.m_dst[0];
            delay_info >> new_delay.m_dst[1];
            delay_info >> new_delay.m_desc >> no_use;
            delay_info >> new_delay.m_delay_list[0] >> new_delay.m_delay_list[1];
            new_delay.m_delay_list[0] = new_delay.m_delay_list[0] / this->m_clock_rate;
            new_delay.m_delay_list[1] = new_delay.m_delay_list[1] / this->m_clock_rate;
            res.push_back(new_delay);
            this->m_pac_in_net--;
        }

        this->m_file_size = fs::file_size(this->m_delay_info);
        return true;
    }

    void write_end_flag() {
        if (this->m_has_wrote_end_flag_)  return;
        std::ofstream trace_file(this->m_trace_file, std::ios::app);
        trace_file << "-1" << std::endl;
        trace_file.close();
        this->m_has_wrote_end_flag_ = true;
        this->restart();
        this->m_state = ProcessState::PS_STRAIGHT_TO_END;
        spdlog::debug("End flag has been wrote, popnet will straightly run to the end.");
    }

    void pause() {
        InterChiplet::InnerTimeType tmp2 = this->m_current_time;
        if (this->m_state == ProcessState::PS_SUSPEND
            || this->m_state == ProcessState::PS_STRAIGHT_TO_END
        ) {
            return;
        }
        this->m_state = ProcessState::PS_SUSPEND;
        kill(this->m_pid, SIGSTOP);
        // spdlog::debug("Popnet suspended.");
    }

    void restart() {
        InterChiplet::InnerTimeType tmp2 = this->m_current_time;
        if (this->m_state == ProcessState::PS_RUNNING
            || this->m_state == ProcessState::PS_STRAIGHT_TO_END
            || this->m_max_time == 0
        ) {
            return;
        }
        this->m_state = ProcessState::PS_RUNNING;
        kill(this->m_pid, SIGCONT);
    }

};


# endif
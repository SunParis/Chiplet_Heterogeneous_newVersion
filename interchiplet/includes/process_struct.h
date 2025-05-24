# pragma once 

/**
 * @file process_struct.h
 * @brief Data structure of process configuration.
 * @details This file contains the data structure of process configuration and the data structure of
 *          popnet configuration.
 */

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

/**
 * @brief Enum for process state.
 */
enum ProcessState {
    PS_INIT,
    PS_RUNNING,
    
    // This state means the process is suspended and waiting for a signal to continue.
    PS_SUSPEND,
    PS_END,
    
    // This state means the process will be straightly run to the end without any pause.
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
        m_pause_due_to_send_sync(false)
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

    // Process current time
    std::atomic<InterChiplet::InnerTimeType> m_current_time;
    
    // Process state
    std::atomic<ProcessState> m_state;

    // Whether is suspended due to waiting for a SYNC after a RECV.
    bool m_pause_due_to_recv_sync;
    
    // Whether is suspended due to waiting for a SYNC after a SEND.
    bool m_pause_due_to_send_sync;

};

/**
 * @brief Data structure of popnet configuration.
 */
class PopnetProcess {

public:

    // Configuration.
    std::thread::id m_thread_id;
    int m_pid;
    std::string m_command;
    std::vector<std::string> m_args;
    std::string m_log_file;
    bool m_to_stdout;
    double m_clock_rate;
    std::string m_pre_copy;

    std::string m_unfinished_line;

    // Process current time
    std::atomic<InterChiplet::InnerTimeType> m_current_time;
    
    // Process state
    std::atomic<ProcessState> m_state;
        
    // The file size of delay info file.
    // This is used to check whether the file has been updated.
    std::size_t m_file_size;

    // The maximum time of the popnet can run to.
    std::atomic<InterChiplet::InnerTimeType> m_max_time;

    // The number of packages in the network.
    std::size_t m_pac_in_net;

    // The flag to indicate whether the end flag has been wrote.
    // If the end flag has been wrote, the popnet will straightly run to the end.
    std::atomic_bool m_has_wrote_end_flag_;

    // The file name of the trace file.
    std::string m_trace_file;

    // The file name of the delay info file.
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

    /**
     * @brief Write a new record to the trace file.
     * @param bench_item The network benchmark item to be written.
     */
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

        // `+= 2` means that after this new trace, 
        // two simulation results will be produced, and after each simulation result is produced, 
        // it will respectively `-= 1`.
        this->m_pac_in_net += 2;
    }

    /**
     * @brief Get the new delay information from the delay info file.
     * @param res The vector to store the new delay information.
     * @return True if there is new delay information, false otherwise.
     */
    bool get_new_delay(std::vector<NetworkDelayItem>& res) {
        // If the file does not exist, return false.
        if (!fs::exists(this->m_delay_info)) {
            return false;
        }
        // If the file size does not change, return false.
        // This is used to check whether the file has been updated.
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

        // Update the delayinfo file size.
        this->m_file_size = fs::file_size(this->m_delay_info);
        return true;
    }

    /**
     * @brief Write the end flag to the trace file.
     * @details The end flag is used to indicate that the simulation has ended.
     *          The end flag is a line with "-1" in the trace file.
     *          The popnet will straightly run to the end after writing the end flag.
     * @note This function will only write the end flag once.
     */
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

    /**
     * @brief Pause the popnet process.
     * @details The popnet process will be paused and wait for the signal to continue.
     */
    void pause() {
        InterChiplet::InnerTimeType tmp2 = this->m_current_time;
        if (this->m_state == ProcessState::PS_SUSPEND
            || this->m_state == ProcessState::PS_STRAIGHT_TO_END
        ) {
            return;
        }
        this->m_state = ProcessState::PS_SUSPEND;
        
        // Send SIGSTOP signal to the popnet process.
        kill(this->m_pid, SIGSTOP);
    }

    /**
     * @brief Restart the popnet process.
     * @details The popnet process will be restarted and continue to run.
     */
    void restart() {
        InterChiplet::InnerTimeType tmp2 = this->m_current_time;
        if (this->m_state == ProcessState::PS_RUNNING
            || this->m_state == ProcessState::PS_STRAIGHT_TO_END
            || this->m_max_time == 0
        ) {
            return;
        }
        this->m_state = ProcessState::PS_RUNNING;
        
        // Send SIGCONT signal to the popnet process.
        kill(this->m_pid, SIGCONT);
    }

};


# endif
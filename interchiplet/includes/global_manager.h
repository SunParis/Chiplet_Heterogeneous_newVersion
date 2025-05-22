# pragma once

# ifndef _GLOBAL_MANAGER_H_
# define _GLOBAL_MANAGER_H_ 1

# include <queue>
# include <map>
# include <functional>

# include "process_struct.h"
# include "sync_struct.h"

struct AddrTypeHash {
    std::size_t operator()(const InterChiplet::AddrType& v) const {
        std::size_t seed = v.size();
        std::hash<long> hasher;
        for (const long& i : v) {
            seed ^= hasher(i) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

typedef std::vector<std::pair<InterChiplet::SyncCommand, ProcessStruct*>> SyncProcList;

namespace InterChiplet {

class GlobalManager {

private:

    std::vector<ProcessStruct *>& proc_list;

    std::mutex m_process_op_lock;

    std::mutex m_popnet_op_lock;

    std::mutex m_bench_lock;

    std::mutex m_commun_list_lock;

    std::mutex m_delay_list_lock;

    std::mutex m_pipe_list_lock;

    SyncStruct* sync_struct;

    PopnetProcess *popnet_process = nullptr;

    std::priority_queue<NetworkBenchItem, std::vector<NetworkBenchItem>, CompareNetworkBenchItem> bench;

    std::unordered_map<InterChiplet::AddrType, std::vector<NetworkDelayItem>, AddrTypeHash> popnet_delay;

    std::unordered_map<InterChiplet::AddrType, SyncProcList, AddrTypeHash> m_recv_cmd_list;

    std::unordered_map<InterChiplet::AddrType, SyncProcList, AddrTypeHash> m_send_cmd_list;

    std::unordered_map<InterChiplet::AddrType, std::vector<InterChiplet::AddrType>, AddrTypeHash> m_send_pipe_list;

    std::unordered_map<InterChiplet::AddrType, std::vector<InterChiplet::AddrType>, AddrTypeHash> m_recv_pipe_list;

    std::pair<bool, NetworkDelayItem> get_delay_info(
        const InterChiplet::AddrType& send_addr, const InterChiplet::AddrType& recv_addr);

    void popnet_succ();

    void kill_all_proc();

    void check_new_delayinfo();

    void insert_send_pipe(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc);

    void insert_recv_pipe(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc);

    void insert_send(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc);

    bool insert_recv(const InterChiplet::SyncCommand& __cmd, NetworkDelayItem& delay_item, ProcessStruct *proc);

    InterChiplet::InnerTimeType update_min_time();

    /**
     * @brief Handle WRITE command with barrier flag.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_barrier_write_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle WRITE command with LOCK flag.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_lock_write_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle WRITE command with UNLOCK flag.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_unlock_write_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

public:
    
    std::mutex m_cmd_handler_lock;
    
    GlobalManager(PopnetProcess *popnet_process_, std::vector<ProcessStruct*>& proc_list_);

    ~GlobalManager();

    void update_proc_time(ProcessStruct *__proc_struct, InterChiplet::InnerTimeType _m_current_time, bool need_popnet_move = true);

    void update_popnet_time(InterChiplet::InnerTimeType _m_current_time);

    void update_status(ProcessStruct *__proc_struct, ProcessState __proc_st, bool due_to_recv_sync = false, bool due_to_send_sync = false, bool need_popnet_move = true);

    /**
     * @brief Handle CYCLE command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_cycle_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle PIPE command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_pipe_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle BARRIER command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_barrier_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle LOCK command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_lock_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle UNLOCK command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_unlock_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle LAUNCH command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_launch_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle WAITLAUNCH command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_waitlaunch_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle READ command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_read_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    /**
     * @brief Handle WRITE command.
     * @param __cmd Command to handle.
     * @param __proc_struct Pointer to process structure.
     */
    void handle_write_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct);

    void clear_pipes() {
        this->sync_struct->m_pipe_struct.clear();
    }

};

};

# endif

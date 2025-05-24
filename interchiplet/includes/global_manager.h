# pragma once

/**
 * @file global_manager.h
 * @brief This file contains the definition of the GlobalManager class,
 *          which is responsible for managing the global state of the system.
 * @details The GlobalManager class handles synchronization commands, process states, 
 *              and network delays. It provides methods to handle various synchronization commands
 *              such as CYCLE, PIPE, BARRIER, LOCK, UNLOCK, LAUNCH, WAITLAUNCH, READ, and WRITE.
 *          It also manages the process list and updates their states based on the commands received.
 *          The class uses mutexes to ensure thread safety when accessing shared resources.
 *          The GlobalManager class is designed to work with the InterChiplet framework,
 *              which is a simulation framework for inter-chiplet communication.
 */

# ifndef _GLOBAL_MANAGER_H_
# define _GLOBAL_MANAGER_H_ 1

# include <queue>
# include <map>
# include <functional>

# include "process_struct.h"
# include "sync_struct.h"

/**
 * @brief Hash function for InterChiplet::AddrType.
 * @details This hash function is used to hash the InterChiplet::AddrType type,
 *          which is a vector of long integers.
 *          The hash function is used in unordered_map to store and retrieve
 *          InterChiplet::AddrType objects efficiently.
 */
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

/**
 * @brief Class for managing global state and synchronization.
 * @details The GlobalManager class is responsible for managing the global state of the system,
 *          handling synchronization commands, and updating process states.
 *          It provides methods to handle various synchronization commands such as CYCLE, PIPE,
 *          BARRIER, LOCK, UNLOCK, LAUNCH, WAITLAUNCH, READ, and WRITE.
 */
class GlobalManager {

private:

    // List of processes 
    std::vector<ProcessStruct *>& proc_list;

    // Lock for process operations
    std::mutex m_process_op_lock;

    // Lock for popnet operations
    std::mutex m_popnet_op_lock;

    // Lock for network bench queue operations
    std::mutex m_bench_lock;

    // Lock for GlobalManager::m_recv_cmd_list and GlobalManager::m_send_cmd_list operations
    std::mutex m_commun_list_lock;

    // Lock for GlobalManager::popnet_delay operations
    std::mutex m_delay_list_lock;

    // Lock for GlobalManager::m_recv_pipe_list and GlobalManager::m_send_pipe_list operations
    std::mutex m_pipe_list_lock;

    // Pointer to the synchronization structure
    SyncStruct* sync_struct;

    // Pointer to the Popnet process
    PopnetProcess *popnet_process = nullptr;

    // The priority queue for network bench items which are not able to add to the trace file yet.
    std::priority_queue<NetworkBenchItem, std::vector<NetworkBenchItem>, CompareNetworkBenchItem> bench;

    // The list of network delay items
    std::unordered_map<InterChiplet::AddrType, std::vector<NetworkDelayItem>, AddrTypeHash> popnet_delay;

    // The list of READ(RECV) commands which are not able to add to be done yet.
    std::unordered_map<InterChiplet::AddrType, SyncProcList, AddrTypeHash> m_recv_cmd_list;

    // The list of WRITE(SEND) commands which are not able to add to be done yet.
    std::unordered_map<InterChiplet::AddrType, SyncProcList, AddrTypeHash> m_send_cmd_list;

    // The list of PIPE(SEND) commands which are done but the corresponding PIPE(RECV) command is not done yet.
    std::unordered_map<InterChiplet::AddrType, std::vector<InterChiplet::AddrType>, AddrTypeHash> m_send_pipe_list;

    // The list of PIPE(RECV) commands which are done but the corresponding PIPE(SEND) command is not done yet.
    std::unordered_map<InterChiplet::AddrType, std::vector<InterChiplet::AddrType>, AddrTypeHash> m_recv_pipe_list;

    /**
     * @brief Get delay information for a given send and receive address.
     * @param send_addr The address of the sender.
     * @param recv_addr The address of the receiver.
     * @return A pair containing a boolean indicating success and the delay item.
     */
    std::pair<bool, NetworkDelayItem> get_delay_info(
        const InterChiplet::AddrType& send_addr, const InterChiplet::AddrType& recv_addr);

    /**
     * @brief Let the Popnet process continue.
     * @details This function checks if the Popnet process can continue.
     * @note This function is called when the clock level is updated.
     */
    void popnet_succ();

    /**
     * @brief Kill all processes.
     * @details This function kills all processes in the process list.
     * @note This function is called when the simulation failed.
     */
    void kill_all_proc();

    /**
     * @brief Check if there is any new delayinfo.
     * @note This function is called when the clock of popnet is updated.
     */
    void check_new_delayinfo();

    /**
     * @brief Insert a PIPE(SEND) command into the send pipe list.
     * @param __cmd The command to insert.
     * @param proc Pointer to the process structure.
     */
    void insert_send_pipe(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc);

    /**
     * @brief Insert a PIPE(RECV) command into the receive pipe list.
     * @param __cmd The command to insert.
     * @param proc Pointer to the process structure.
     */
    void insert_recv_pipe(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc);

    /**
     * @brief Insert a WRITE(SEND) command into the send command list.
     * @param __cmd The command to insert.
     * @param proc Pointer to the process structure.
     */
    void insert_send(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc);

    /**
     * @brief Insert a READ(RECV) command into the receive command list.
     * @param __cmd The command to insert.
     * @param delay_item The delay item associated with the command.
     * @param proc Pointer to the process structure.
     */
    bool insert_recv(const InterChiplet::SyncCommand& __cmd, NetworkDelayItem& delay_item, ProcessStruct *proc);

    /**
     * @brief Count and return the minimum time (namely, clock level) from the process list.
     * @note This function will add delay bench from GLobalManager::bench to the trace file
     *          if the start time of the delay bench is less than the minimum time.
     */
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
    
    // Lock for command handler operations
    std::mutex m_cmd_handler_lock;
    
    /**
     * @brief Constructor for GlobalManager.
     * @param popnet_process_ Pointer to the Popnet process.
     * @param proc_list_ List of process structures.
     */
    GlobalManager(PopnetProcess *popnet_process_, std::vector<ProcessStruct*>& proc_list_);

    ~GlobalManager();

    /**
     * @brief Update the process time and status.
     * @param __proc_struct Pointer to the process structure.
     * @param _m_current_time Current time.
     * @param need_popnet_move Flag indicating if Popnet should move.
     */
    void update_proc_time(ProcessStruct *__proc_struct, InterChiplet::InnerTimeType _m_current_time, bool need_popnet_move = true);

    /**
     * @brief Update the Popnet time.
     * @param _m_current_time Current time.
     */
    void update_popnet_time(InterChiplet::InnerTimeType _m_current_time);

    /**
     * @brief Update the process status.
     * @param __proc_struct Pointer to the process structure.
     * @param __proc_st New process state.
     * @param due_to_recv_sync Flag indicating if the update is due to receive sync.
     * @param due_to_send_sync Flag indicating if the update is due to send sync.
     * @param need_popnet_move Flag indicating if Popnet should move.
     */
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

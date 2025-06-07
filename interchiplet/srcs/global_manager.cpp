
# include "global_manager.h"

const InterChiplet::InnerTimeType TimeMax = std::numeric_limits<InterChiplet::InnerTimeType>::max();

using namespace InterChiplet;

/**
 * @brief Insert a PIPE(SEND) command into the send pipe list.
 * @param __cmd The command to insert.
 * @param proc Pointer to the process structure.
 */
void GlobalManager::insert_send_pipe(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc) {
    {
        const std::lock_guard<std::mutex> lock(this->m_pipe_list_lock);
        // Check if the corresponding PIPE(RECV) command exists in the recv pipe list.
        if (this->m_recv_pipe_list[__cmd.m_src].size() != 0) {
            for (auto iter = this->m_recv_pipe_list[__cmd.m_src].begin();
                iter != this->m_recv_pipe_list[__cmd.m_src].end(); iter++
            ) {
                if ((*iter) == __cmd.m_dst) {
                    // If the PIPE(RECV) command exists, 
                    // remove it from the recv pipe list and return.
                    this->m_recv_pipe_list[__cmd.m_src].erase(iter);
                    return;
                }
            }
        }
        // If the PIPE(RECV) command does not exist, add the PIPE(SEND) command to the send pipe list.
        this->m_send_pipe_list[__cmd.m_src].push_back(__cmd.m_dst);
    }

    // PIPE(SEND) need not do this operation.
    // this->update_status(proc, PS_SUSPEND, false, true);
}

/**
 * @brief Insert a PIPE(RECV) command into the receive pipe list.
 * @param __cmd The command to insert.
 * @param proc Pointer to the process structure.
 */
void GlobalManager::insert_recv_pipe(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc) {
    {
        const std::lock_guard<std::mutex> lock(this->m_pipe_list_lock);
        // Check if the corresponding PIPE(SEND) command exists in the send pipe list.
        if (this->m_send_pipe_list[__cmd.m_src].size() != 0) {
            for (auto iter = this->m_send_pipe_list[__cmd.m_src].begin();
                iter != this->m_send_pipe_list[__cmd.m_src].end(); iter++
            ) {
                if ((*iter) == __cmd.m_dst) {
                    // If the PIPE(SEND) command exists,
                    // remove it from the send pipe list and return.
                    this->m_send_pipe_list[__cmd.m_src].erase(iter);
                    return;
                }
            }
        }
        // If the PIPE(SEND) command does not exist, add the PIPE(RECV) command to the receive pipe list.
        this->m_recv_pipe_list[__cmd.m_src].push_back(__cmd.m_dst);
    }

    // PIPE(RECV) will block the process until the corresponding PIPE(SEND) command is received.
    this->update_status(proc, PS_SUSPEND, true);
}


/**
 * @brief Get delay information for a given send and receive address.
 * @param send_addr The address of the sender.
 * @param recv_addr The address of the receiver.
 * @return A pair containing a boolean indicating success and the delay item.
 */
std::pair<bool, NetworkDelayItem>
GlobalManager::get_delay_info(
    const InterChiplet::AddrType& send_addr, 
    const InterChiplet::AddrType& recv_addr
) {
    // Check if the delay information for the given send address exists.
    for (auto iter = this->popnet_delay[send_addr].begin();
        iter != this->popnet_delay[send_addr].end(); iter++
    ) {
        if (send_addr == (*iter).m_src && recv_addr == (*iter).m_dst) {
            std::pair<bool, NetworkDelayItem> ret = std::make_pair(true, NetworkDelayItem((*iter)));
            // Send synchronize command to response WRITE command.
            this->popnet_delay[send_addr].erase(iter);
            return ret;
        }
    }
    return std::make_pair(false, NetworkDelayItem());
}

/**
 * @brief Let the Popnet process continue.
 * @details This function checks if the Popnet process can continue.
 * @note This function is called when the clock level is updated.
 */
void GlobalManager::popnet_succ() {
    const std::lock_guard<std::mutex> lock(this->m_popnet_op_lock);
    // If there is no packege in the network, pause the Popnet process.
    if (this->popnet_process->m_pac_in_net == 0) {
        this->popnet_process->pause();
        return;
    }

    // Check if the Popnet process has reached the maximum time.
    InterChiplet::InnerTimeType tmp1 = this->popnet_process->m_max_time;
    InterChiplet::InnerTimeType tmp2 = this->popnet_process->m_current_time;
    spdlog::debug("Popnet clk is now {}(max:: {}).", tmp2, tmp1);
    if (this->popnet_process->m_max_time <= this->popnet_process->m_current_time) {
        this->popnet_process->pause();
    }
    this->popnet_process->restart(); 
}

/**
 * @brief Kill all processes.
 * @details This function kills all processes in the process list.
 * @note This function is called when the simulation failed.
 */
void GlobalManager::kill_all_proc() {
    for (auto& iter: this->proc_list) {
        // Check if the process is not already terminated.
        if (iter->m_state != PS_END) {
            iter->m_state = PS_END;
            // Send SIGKILL signal to the process.
            kill(iter->m_pid, SIGKILL);
            spdlog::error("Process {} was forcibly terminated.", iter->m_pid);
        }
    }
}

/**
 * @brief Check if there is any new delayinfo.
 * @note This function is called when the clock of popnet is updated.
 */
void GlobalManager::check_new_delayinfo() {
    std::vector<NetworkDelayItem> res;
    const std::lock_guard<std::mutex> lock(this->m_delay_list_lock);
    const std::lock_guard<std::mutex> commun_lock(this->m_commun_list_lock);
    // Check if there is any new delay information.
    if (this->popnet_process->get_new_delay(res)) {
        spdlog::debug("Read {} delay info.", res.size());
        // Process each delay item.
        for (auto& delay_item: res) {
            // If the second delay info is `-1`,
            // it means the delay info only contains the "send delay". 
            if (delay_item.m_delay_list[1] == -1) {
                // Send synchronize command to response WRITE command.           
                InterChiplet::SyncCommand& cmd = this->m_send_cmd_list[delay_item.m_src].front().first;
                InterChiplet::sendSyncCmd(cmd.m_stdin_fd, static_cast<InterChiplet::TimeType>(
                    (delay_item.m_delay_list[0] + delay_item.m_cycle) * cmd.m_clock_rate));
                // Update the process time and status.
                this->update_proc_time(this->m_send_cmd_list[delay_item.m_src].front().second,
                    delay_item.m_delay_list[0] + delay_item.m_cycle, false);
                this->update_status(this->m_send_cmd_list[delay_item.m_src].front().second,
                    PS_RUNNING, false, false, false);
                this->m_send_cmd_list[delay_item.m_src].erase(this->m_send_cmd_list[delay_item.m_src].begin());
            }
            // If the second delay info is not `-1`,
            // it means the delay info contains "trans delay".
            else {
                for (auto iter = this->m_recv_cmd_list[delay_item.m_src].begin();
                    iter != this->m_recv_cmd_list[delay_item.m_src].end(); iter++
                ) {
                    // Find whether the corresponding READ(RECV) command exists in the recv command list.
                    if (delay_item.m_src == (*iter).first.m_src && delay_item.m_dst == (*iter).first.m_dst) {
                        // If the corresponding READ(RECV) command exists,
                        // count the delay time and send synchronize command.
                        delay_item.m_delay_list[1] += delay_item.m_cycle;
                        if (delay_item.m_delay_list[1] < (*iter).first.m_cycle) {
                            delay_item.m_delay_list[1] = (*iter).first.m_cycle;
                        }
                        InterChiplet::sendSyncCmd((*iter).first.m_stdin_fd, static_cast<InterChiplet::TimeType>(
                            delay_item.m_delay_list[1] * (*iter).first.m_clock_rate));
                        // Update the process time and status.
                        this->update_proc_time((*iter).second, delay_item.m_delay_list[1], false);
                        this->update_status((*iter).second, PS_RUNNING, false, false, false);
                        this->m_recv_cmd_list[delay_item.m_src].erase(iter);
                        break;
                    }
                    // If the corresponding READ(RECV) command does not exist,
                    // add the delay item to the popnet delay list without counting the delay time.
                    if (iter + 1 == this->m_recv_cmd_list[delay_item.m_src].end()) {
                        this->popnet_delay[delay_item.m_src].push_back(delay_item);
                    }
                }
            }
        }
    }
}

/**
 * @brief Insert a WRITE(SEND) command into the send command list.
 * @param __cmd The command to insert.
 * @param proc Pointer to the process structure.
 */
void GlobalManager::insert_send(const InterChiplet::SyncCommand& __cmd, ProcessStruct *proc) {
    spdlog::debug("Inert `SEND`:: {}", InterChiplet::dumpCmd(__cmd));
    {
        const std::lock_guard<std::mutex> lock(this->m_commun_list_lock);
        // Insert the command into the send command list.
        this->m_send_cmd_list[__cmd.m_src].push_back(std::make_pair(__cmd, proc));
    }
    {
        const std::lock_guard<std::mutex> lock(this->m_bench_lock);
        // Insert the command into the trace bench list.
        this->bench.push(NetworkBenchItem(__cmd.m_cycle, __cmd.m_src,
            __cmd.m_dst, __cmd.m_desc, __cmd.m_nbytes));
    }
    // Update the clock level.
    this->update_min_time();
}

/**
 * @brief Insert a READ(RECV) command into the receive command list.
 * @param __cmd The command to insert.
 * @param delay_item The delay item associated with the command.
 * @param proc Pointer to the process structure.
 */
bool GlobalManager::insert_recv(const InterChiplet::SyncCommand& __cmd, NetworkDelayItem& delay_item, ProcessStruct *proc) {
    const std::lock_guard<std::mutex> lock(this->m_delay_list_lock);
    const std::lock_guard<std::mutex> commun_lock(this->m_commun_list_lock);
    // Check if the corresponding delayinfo exists in the delay list.
    auto res = this->get_delay_info(__cmd.m_src, __cmd.m_dst);
    if (res.first) {
        // If the delayinfo exists, count the delay time.
        delay_item = NetworkDelayItem(res.second);
        delay_item.m_delay_list[1] += delay_item.m_cycle;
        if (delay_item.m_delay_list[1] < __cmd.m_cycle) {
            delay_item.m_delay_list[1] = __cmd.m_cycle;
        }
        return true;
    }
    // If the delayinfo does not exist, insert the command into the receive command list.
    this->m_recv_cmd_list[__cmd.m_src].push_back(std::make_pair(__cmd, proc));
    return false;
}

/**
 * @brief Update the process status.
 * @param __proc_struct Pointer to the process structure.
 * @param __proc_st New process state.
 * @param due_to_recv_sync Flag indicating if the update is due to receive sync.
 * @param due_to_send_sync Flag indicating if the update is due to send sync.
 * @param need_popnet_move Flag indicating if Popnet should move.
 */
void GlobalManager::update_status(ProcessStruct *__proc_struct, ProcessState __proc_st, bool due_to_recv_sync, bool due_to_send_sync, bool need_popnet_move) {
    {
        const std::lock_guard<std::mutex> lock(this->m_process_op_lock);
        __proc_struct->m_state = __proc_st;
        __proc_struct->m_pause_due_to_recv_sync = due_to_recv_sync;
        __proc_struct->m_pause_due_to_send_sync = due_to_send_sync;
        spdlog::debug("Process {} transfer state to {}.", __proc_struct->m_pid, processStateToString(__proc_st));
    }
    if (need_popnet_move) {
        spdlog::debug("Clock level updates to {}.", this->update_min_time());
        this->popnet_succ();
    }
}

/**
 * @brief Update the process time and status.
 * @param __proc_struct Pointer to the process structure.
 * @param _m_current_time Current time.
 * @param need_popnet_move Flag indicating if Popnet should move.
 */
void GlobalManager::update_proc_time(ProcessStruct* __proc_struct, InterChiplet::InnerTimeType _m_current_time, bool need_popnet_move) {
    {
        const std::lock_guard<std::mutex> lock(this->m_process_op_lock);
        __proc_struct->m_current_time = _m_current_time;
        spdlog::debug("Process {} update time to {}.", __proc_struct->m_pid, _m_current_time);
    }
    if (need_popnet_move) {
        spdlog::debug("Clock level updates to {}.", this->update_min_time());
        this->popnet_succ();
    }
}

/**
 * @brief Update the Popnet time.
 * @param _m_current_time Current time.
 */
void GlobalManager::update_popnet_time(InterChiplet::InnerTimeType _m_current_time) {
    if (_m_current_time == this->popnet_process->m_current_time) {
        return;
    }
    
    // Check if there is any new delay information.
    this->check_new_delayinfo();
    
    // Update the minimum time.
    this->update_min_time();

    const std::lock_guard<std::mutex> lock(this->m_popnet_op_lock);

    // Update the current time of the Popnet process.
    this->popnet_process->m_current_time = _m_current_time;

    // If the current time exceeds the maximum time, pause the Popnet process.
    if (this->popnet_process->m_max_time <= _m_current_time) {            
        this->popnet_process->pause();
    }
    // If the current time is less than the maximum time, restart the Popnet process.
    else {
        this->popnet_process->restart();
    }
    spdlog::debug("Popnet process moves to {}.", _m_current_time);
}

/**
 * @brief Count and return the minimum time (namely, clock level) from the process list.
 * @note This function will add delay bench from GLobalManager::bench to the trace file
 *          if the start time of the delay bench is less than the minimum time.
 */
InterChiplet::InnerTimeType GlobalManager::update_min_time() {
    InterChiplet::InnerTimeType ret = -1;
    // Count the number of processes in the END state.
    std::size_t count = 0;
    // Initialize the minimum index to the size of the process list.
    std::size_t min_idx;
    {
        const std::lock_guard<std::mutex> lock(this->m_process_op_lock);
        min_idx = this->proc_list.size();
        // Iterate through the process list to find the minimum time.
        for (std::size_t i = 0; i < this->proc_list.size(); i++) {
            // If the process is not in the END state, check its current time.
            if (this->proc_list[i]->m_state != PS_END) {
                if (!this->proc_list[i]->m_pause_due_to_recv_sync) {
                    if (ret == -1) {
                        ret = this->proc_list[i]->m_current_time;
                        min_idx = i;
                    }
                    else if (ret > this->proc_list[i]->m_current_time) {
                        ret = this->proc_list[i]->m_current_time;
                        min_idx = i;
                    }
                    else if (ret == this->proc_list[i]->m_current_time
                        && this->proc_list[i]->m_pause_due_to_send_sync
                    ) {
                        min_idx = i;
                    }
                }
                // Ignore processes that are paused due to receive sync.
                // else {
                //     // Do nothing
                // }
            }
            // Ignore processes that are in the END state.
            else {
                count++;
            }
        }

        // If all processes are in the END state, write the end flag to the Popnet process.
        if (count == this->proc_list.size()) {
            this->popnet_process->write_end_flag();
            spdlog::debug("All process fin.");
            return -1;
        }
        // If all of the running processes are paused due to recv sync,
        //  and there is no package in the network, stop all process.
        else if (ret == -1
            && this->popnet_process->m_state != PS_RUNNING
            && this->popnet_process->m_pac_in_net == 0
        ) {
            spdlog::error("All process are stoped due to `RECV`, checks to the benchmark is needed.");
            this->popnet_process->write_end_flag();
            this->kill_all_proc();
            return -1;
        }
    }

    InterChiplet::InnerTimeType old_max = this->popnet_process->m_max_time;
    
    // If the the slowest process is paused due to WRITE(SEND), set the maximum time to TimeMax.
    // This is to ensure that the simulation will not be paused due to WRITE(SEND).
    if (this->proc_list[min_idx]->m_pause_due_to_send_sync || ret == -1) {
        this->popnet_process->m_max_time = TimeMax;
    }
    else {
        this->popnet_process->m_max_time = ret;
    }
    InterChiplet::InnerTimeType new_max = this->popnet_process->m_max_time;
    if (old_max != new_max) {
        spdlog::debug("Popnet process updates max clk to {}.", new_max);
    }

    const std::lock_guard<std::mutex> lock(this->m_bench_lock);

    // To move the delay bench to the trace file,
    // if the start time of the delay bench is less than the minimum time.
    while (!this->bench.empty()) {
        if (this->bench.top().m_src_cycle > ret) {
            break;
        }
        this->popnet_process->write_new_rec(this->bench.top());
        this->bench.pop();
    }

    return ret;
}

GlobalManager::GlobalManager(PopnetProcess *popnet_process_, std::vector<ProcessStruct*>& proc_list_)
:   popnet_process(popnet_process_),
    proc_list(proc_list_),
    bench()
{
    this->sync_struct = new SyncStruct();
    // JULIET:
    // O, swear not by the moon, th’ inconstant moon,
    // That monthly changes in her circled orb,
    // Lest that thy love prove likewise variable.

    // ROMEO:
    // What shall I swear by?

    // JULIET:
    // Do not swear at all;
    // Or, if thou wilt, swear by thy gracious self,
    // Which is the god of my idolatry,
    // And I’ll believe thee.

    // ROMEO:
    // If my heart’s dear love—

    // JULIET:
    // Well, do not swear. Although I joy in thee,
    // I have no joy of this contract tonight:
    // It is too rash, too unadvised, too sudden;
    // Too like the lightning, which doth cease to be
    // Ere one can say “It lightens.” Sweet, good night!
    // This bud of love, by summer’s ripening breath,
    // May prove a beauteous flower when next we meet.
}

GlobalManager::~GlobalManager() {
    delete this->sync_struct;
    // ROMEO:
    // He jests at scars that never felt a wound.
    // (Juliet appears above at a window.)
    // But soft! What light through yonder window breaks?
    // It is the east, and Juliet is the sun.
    // Arise, fair sun, and kill the envious moon,
    // Who is already sick and pale with grief
    // That thou, her maid, art far more fair than she.
    // Be not her maid, since she is envious.
    // Her vestal livery is but sick and green,
    // And none but fools do wear it. Cast it off.

    // JULIET:
    // Ay me!

    // ROMEO:
    // She speaks. O, speak again, bright angel! For thou art
    // As glorious to this night, being o’er my head,
    // As is a winged messenger of heaven
    // Unto the white-upturned wondering eyes
    // Of mortals that fall back to gaze on him
    // When he bestrides the lazy-puffing clouds
    // And sails upon the bosom of the air.

    // JULIET:
    // O Romeo, Romeo! Wherefore art thou Romeo?
    // Deny thy father and refuse thy name,
    // Or, if thou wilt not, be but sworn my love,
    // And I’ll no longer be a Capulet.

}

/**
 * @brief Handle CYCLE command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_cycle_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    // Update global cycle.
    this->sync_struct->m_cycle_struct.update(__cmd.m_cycle);
    spdlog::debug("{}", dumpCmd(__cmd));
}

/**
 * @brief Handle PIPE command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_pipe_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    // Create Pipe file.
    std::string file_name = InterChiplet::pipeName(__cmd.m_src, __cmd.m_dst);
    if (access(file_name.c_str(), F_OK) == -1) {
        // Report error if FIFO file does not exist and mkfifo error.
        if (mkfifo(file_name.c_str(), 0664) == -1) {
            spdlog::error("{} Cannot create pipe file {}.", dumpCmd(__cmd), file_name);
        }
        // Report success.
        else {
            // Register file name in pipe set.
            this->sync_struct->m_pipe_struct.insert(file_name);
            spdlog::debug("{} Create pipe file {}.", dumpCmd(__cmd), file_name);
        }
    }
    // Reuse exist FIFO and reports.
    else {
        spdlog::debug("{} Reuse exist pipe file {}.", dumpCmd(__cmd), file_name);
    }

    // Send RESULT command.
    std::string resp_file_name = "../" + file_name;
    InterChiplet::sendResultCmd(__cmd.m_stdin_fd, {resp_file_name});

    if (__cmd.m_type == SC_RECEIVE) {
        this->insert_recv_pipe(__cmd, __proc_struct);
    }
    else {
        this->insert_send_pipe(__cmd, __proc_struct);
    }
}

/**
 * @brief Handle BARRIER command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_barrier_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    int uid = DIM_X(__cmd.m_dst);
    int count = __cmd.m_nbytes;

    // Register BARRIER command.
    this->sync_struct->m_barrier_struct.insertBarrier(uid, count, __cmd);
    // Barrier override.
    if (this->sync_struct->m_barrier_struct.overflow(uid)) {
        // Send RESULT command to each barrier.
        for (auto& item : this->sync_struct->m_barrier_struct.barrierCmd(uid)) {
            InterChiplet::sendResultCmd(item.m_stdin_fd);
        }
        this->sync_struct->m_barrier_struct.reset(uid);
        spdlog::debug("{} Register BARRIER command. Barrier overflow.", dumpCmd(__cmd));
    }
    // Wait further barrier commands.
    else {
        spdlog::debug("{} Register BARRIER command.", dumpCmd(__cmd));
    }
}

/**
 * @brief Handle LOCK command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_lock_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    // Get mutex ID.
    int uid = DIM_X(__cmd.m_dst);

    if (this->sync_struct->m_lock_struct.isLocked(uid)) {
        // If the mutex is locked, check whether the LOCK command comes from the same source.
        if (this->sync_struct->m_lock_struct.hasLastCmd(uid) &&
            (this->sync_struct->m_lock_struct.getLastCmd(uid).m_src == __cmd.m_src)) {
            // If the LOCK command comes from the same source, ignore the LOCK command.
            spdlog::debug("{} Mutex {} has been locked by the same source. Do nothing.",
                          dumpCmd(__cmd), uid);
            // Send RESULT command to response UNLOCK command.
            InterChiplet::sendResultCmd(__cmd.m_stdin_fd);
        } else {
            // Otherwise, pending the LOCK command until the mutex is unlocked.
            this->sync_struct->m_lock_struct.insertLockCmd(uid, __cmd);
            spdlog::debug("{} Register LOCK command to wait UNLOCK command.", dumpCmd(__cmd));
        }
    } else {
        // If the mutex is not locked, whether this LOCK command can be response dependes on the
        // order determined by the delay information.
        if (this->sync_struct->m_delay_list.hasLock(__cmd.m_dst)) {
            // If the order of the mutex is determined by the delay information, check whether
            // the source address of this LOCK command matches the order determined by the delay
            // information.
            if (this->sync_struct->m_delay_list.frontLockSrc(__cmd.m_dst) == __cmd.m_src) {
                // If the source address of this LOCK command matches the order determined by the
                // delay information, lock the mutex.
                this->sync_struct->m_lock_struct.lock(uid, __cmd);
                spdlog::debug("{} Lock mutex {}.", dumpCmd(__cmd), uid);
                // Send RESULT command to response the LOCK command.
                InterChiplet::sendResultCmd(__cmd.m_stdin_fd);
                // Pop one item from delay information.
                this->sync_struct->m_delay_list.popLock(__cmd.m_dst);
            } else {
                // If the source address of this LOCK command does not match the order determined
                // by the delay informtion, pending this LOCK command until the correct order.
                this->sync_struct->m_lock_struct.insertLockCmd(uid, __cmd);
                spdlog::debug("{} Register LOCK command to wait correct order.", dumpCmd(__cmd));
            }
        } else {
            // If the order of the mutex is not determined by the delay information, lock the mutex.
            this->sync_struct->m_lock_struct.lock(uid, __cmd);
            spdlog::debug("{} Lock mutex {}.", dumpCmd(__cmd), uid);
            // Send RESULT command to response the LOCK command.
            InterChiplet::sendResultCmd(__cmd.m_stdin_fd);
        }
    }
}

/**
 * @brief Handle UNLOCK command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_unlock_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    // Get mutex ID.
    int uid = DIM_X(__cmd.m_dst);

    if (this->sync_struct->m_lock_struct.isLocked(uid)) {
        // If the mutex is locked, unlock the mutex first.
        this->sync_struct->m_lock_struct.unlock(uid, __cmd);
        spdlog::debug("{} Unlock mutex {}.", dumpCmd(__cmd), uid);
        // Send RESULT command to response UNLOCK command.
        InterChiplet::sendResultCmd(__cmd.m_stdin_fd);

        // Lock the mutex by pending LOCK command.
        if (this->sync_struct->m_delay_list.hasLock(__cmd.m_dst)) {
            // If the order of the mutex is determined by the delay information, check whether
            // there is any pending LOCK command matching the order determined by the delay
            // information.
            InterChiplet::AddrType order_src =
                this->sync_struct->m_delay_list.frontLockSrc(__cmd.m_dst);
            if (this->sync_struct->m_lock_struct.hasLockCmd(uid, order_src)) {
                // If there is one pending LOCK command matching the order determined by the delay
                // information. Lock the mutex by this pending LOCK command.
                InterChiplet::SyncCommand lock_cmd =
                    this->sync_struct->m_lock_struct.popLockCmd(uid, order_src);
                this->sync_struct->m_lock_struct.lock(uid, lock_cmd);
                spdlog::debug("\t{} Lock mutex {}.", dumpCmd(lock_cmd), uid);
                // Send RESULT command to response the pending LOCK command.
                InterChiplet::sendResultCmd(lock_cmd.m_stdin_fd);
                // Pop one item from the delay information.
                this->sync_struct->m_delay_list.popLock(__cmd.m_dst);
            }
        } else {
            // If the order of the mutex is not determined by the delay information, check whether
            // there is any pending LOCK command.
            if (this->sync_struct->m_lock_struct.hasLockCmd(uid)) {
                // If there is any pending LOCK command, lock the mutex by the first pending LOCK
                // command.
                InterChiplet::SyncCommand lock_cmd = this->sync_struct->m_lock_struct.popLockCmd(uid);
                this->sync_struct->m_lock_struct.lock(uid, lock_cmd);
                spdlog::debug("\t{} Lock mutex {}.", dumpCmd(lock_cmd), uid);
                // Send RESULT command to response the pending LOCK command.
                InterChiplet::sendResultCmd(lock_cmd.m_stdin_fd);
            }
        }
    } else {
        // If the mutex is unlocked, ignore the UNLOCK command.
        spdlog::debug("{} Mutex {} has not locked. Do nothing.", dumpCmd(__cmd), uid);
        // Send RESULT command to response UNLOCK command.
        InterChiplet::sendResultCmd(__cmd.m_stdin_fd);
    }
}

/**
 * @brief Handle LAUNCH command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_launch_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    // Check launch order and remove item.
    if (this->sync_struct->m_delay_list.hasLaunch(__cmd.m_dst)) {
        if (this->sync_struct->m_delay_list.frontLaunchSrc(__cmd.m_dst) != __cmd.m_src) {
            this->sync_struct->m_launch_struct.insertLaunch(__cmd);
            spdlog::debug("{} Register LAUNCH command to pair with WAITLAUNCH command.",
                          dumpCmd(__cmd));
            return;
        }
    }

    // Check for unconfirmed waitlaunch command.
    bool has_waitlaunch_cmd = this->sync_struct->m_launch_struct.hasMatchWaitlaunch(__cmd);
    InterChiplet::SyncCommand waitlaunch_cmd =
        this->sync_struct->m_launch_struct.popMatchWaitlaunch(__cmd);

    // If there is not waitlaunch command, waitlaunch command.
    if (!has_waitlaunch_cmd) {
        this->sync_struct->m_launch_struct.insertLaunch(__cmd);
        spdlog::debug("{} Register LAUNCH command to pair with WAITLAUNCH command.",
                      dumpCmd(__cmd));
    }
    // If there is waitlaunch command, response launch and waitlaunch command.
    else {
        this->sync_struct->m_delay_list.popLaunch(__cmd.m_dst);
        spdlog::debug("{} Pair with WAITLAUNCH command.", dumpCmd(__cmd));
        // Send SYNC to response LAUNCH command.
        InterChiplet::sendResultCmd(__cmd.m_stdin_fd);
        // Send LAUNCH to response WAITLAUNCH command.
        InterChiplet::sendResultCmd(waitlaunch_cmd.m_stdin_fd,
                                    {DIM_X(__cmd.m_src), DIM_Y(__cmd.m_src)});
    }
}

/**
 * @brief Handle WAITLAUNCH command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_waitlaunch_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    InterChiplet::SyncCommand cmd = __cmd;

    // Check launch order and remove item.
    if (this->sync_struct->m_delay_list.hasLaunch(cmd.m_dst)) {
        cmd.m_src = this->sync_struct->m_delay_list.frontLaunchSrc(cmd.m_dst);
    }

    // Check for unconfirmed waitlaunch command.
    bool has_launch_cmd = this->sync_struct->m_launch_struct.hasMatchLaunch(cmd);
    InterChiplet::SyncCommand launch_cmd = this->sync_struct->m_launch_struct.popMatchLaunch(cmd);

    // If there is not waitlaunch command, waitlaunch command.
    if (!has_launch_cmd) {
        this->sync_struct->m_launch_struct.insertWaitlaunch(__cmd);
        spdlog::debug("{} Register WAITLAUNCH command to pair with LAUNCH command.", dumpCmd(cmd));
    } else {
        this->sync_struct->m_delay_list.popLaunch(cmd.m_dst);
        spdlog::debug("{} Pair with LAUNCH command from {},{} to {},{}.", dumpCmd(cmd),
                      DIM_X(launch_cmd.m_src), DIM_Y(launch_cmd.m_src), DIM_X(launch_cmd.m_dst),
                      DIM_Y(launch_cmd.m_dst));
        // Send RESULT to response LAUNCH command.
        InterChiplet::sendResultCmd(launch_cmd.m_stdin_fd);
        // Send RESULT to response WAITLAUNCH command.
        InterChiplet::sendResultCmd(cmd.m_stdin_fd,
                                    {DIM_X(launch_cmd.m_src), DIM_Y(launch_cmd.m_src)});
    }
}

/**
 * @brief Handle READ command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_read_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    // Check for paired write command.
    NetworkDelayItem delay_item;
    if (this->insert_recv(__cmd, delay_item, __proc_struct)) {
        spdlog::debug("{} Pair with WRITE command. Transation ends at [{},{}] cycle.",
                      dumpCmd(__cmd), static_cast<InterChiplet::TimeType>(delay_item.m_delay_list[0]),
                      static_cast<InterChiplet::TimeType>(delay_item.m_delay_list[1]));
        // Send synchronize command to response READ command.
        InterChiplet::sendSyncCmd(__cmd.m_stdin_fd, static_cast<InterChiplet::TimeType>(
                                                        delay_item.m_delay_list[1] * __cmd.m_clock_rate));
        spdlog::debug("Process {} update time to {}.", __proc_struct->m_pid, delay_item.m_delay_list[1]);
    }
    else {
        spdlog::debug("{} Register READ command to pair with WRITE command.", dumpCmd(__cmd));
        this->update_status(__proc_struct, PS_SUSPEND, true);
    }
}

/**
 * @brief Handle WRITE command with barrier flag.
 * @param __cmd Command to handle.
 * @param this->sync_struct Pointer to global synchronize structure.
 */
void GlobalManager::handle_barrier_write_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    int uid = DIM_X(__cmd.m_dst);
    int count = __cmd.m_desc & 0xFFFF;

    // Insert event to benchmark list.
    NetworkBenchItem bench_item(__cmd);
    this->sync_struct->m_bench_list.insert(bench_item);

    // Register BARRIER command.
    this->sync_struct->m_barrier_timing_struct.insertBarrier(uid, count, __cmd);
    // Barrier override.
    if (this->sync_struct->m_barrier_timing_struct.overflow(uid)) {
        // Get barrier overflow time.
        InterChiplet::InnerTimeType barrier_cycle = this->sync_struct->m_delay_list.getBarrierCycle(
            this->sync_struct->m_barrier_timing_struct.barrierCmd(uid));
        spdlog::debug("{} Barrier overflow at {} cycle.", dumpCmd(__cmd),
                      static_cast<InterChiplet::TimeType>(barrier_cycle));

        // Generate a command as read command.
        InterChiplet::SyncCommand sync_cmd = __cmd;
        sync_cmd.m_cycle = barrier_cycle;

        // Send synchronization command to all barrier items.
        for (auto& item : this->sync_struct->m_barrier_timing_struct.barrierCmd(uid)) {
            CmdDelayPair end_cycle = this->sync_struct->m_delay_list.getEndCycle(item, sync_cmd);
            spdlog::debug("\t{} Transaction ends at {} cycle.", dumpCmd(item),
                          static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle)));
            // Send synchronization comand to response WRITE command.
            InterChiplet::sendSyncCmd(
                item.m_stdin_fd,
                static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle) * item.m_clock_rate));
        }
        this->sync_struct->m_barrier_timing_struct.reset(uid);
    }
    // Wait further barrier commands.
    else {
        spdlog::debug("{} Register WRITE command with BARRIER flag.", dumpCmd(__cmd));
    }
}

/**
 * @brief Handle WRITE command with LOCK flag.
 * @param __cmd Command to handle.
 * @param this->sync_struct Pointer to global synchronize structure.
 */
void GlobalManager::handle_lock_write_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    // Get mutex ID.
    int uid = DIM_X(__cmd.m_dst);

    // Insert event to benchmark list.
    NetworkBenchItem bench_item(__cmd);
    this->sync_struct->m_bench_list.insert(bench_item);

    if (this->sync_struct->m_lock_timing_struct.isLocked(uid)) {
        // If the mutex is locked, check whether the WRITE(LOCK) command comes from the same source.
        if (this->sync_struct->m_lock_timing_struct.hasLastCmd(uid) &&
            (this->sync_struct->m_lock_timing_struct.getLastCmd(uid).m_src == __cmd.m_src)) {
            // If the WRITE(LOCK) command comes from the same source, ignore the LOCK command.
            // Response this WRITE(LOCK) command immediately when it is received by destination.
            CmdDelayPair end_cycle = this->sync_struct->m_delay_list.getEndCycle(__cmd, __cmd);
            spdlog::debug("{} Transaction ends at {} cycle.", dumpCmd(__cmd),
                          static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle)));
            // Send SYNC comand with end cycle to response WRITE command.
            InterChiplet::sendSyncCmd(
                __cmd.m_stdin_fd,
                static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle) * __cmd.m_clock_rate));
        } else {
            // Otherwise, pending the LOCK command until the mutex is unlocked.
            this->sync_struct->m_lock_timing_struct.insertLockCmd(uid, __cmd);
            spdlog::debug("{} Register WRITE command with LOCK flag.", dumpCmd(__cmd));
        }
    } else {
        // The order of mutex is handled by LOCK/UNLOCK command. WRITE(LOCK) command can lock the
        // mutex directly.

        // Get the last command. If there is no last command, use this WRITE(LOCK) command as the
        // last command.
        InterChiplet::SyncCommand last_cmd = __cmd;
        if (this->sync_struct->m_lock_timing_struct.hasLastCmd(uid)) {
            last_cmd = this->sync_struct->m_lock_timing_struct.getLastCmd(uid);
        }
        // Response this WRITE(LOCK) command after it is received by the destination and the
        // destination finished the last command.
        CmdDelayPair end_cycle = this->sync_struct->m_delay_list.getEndCycle(__cmd, last_cmd);

        // Calculate the end cycle of mutex.
        InterChiplet::SyncCommand sync_cmd = __cmd;
        sync_cmd.m_cycle = DST_DELAY(end_cycle);

        // Lock the mutex
        this->sync_struct->m_lock_timing_struct.lock(uid, sync_cmd);
        spdlog::debug("{} Transaction ends at {} cycle.", dumpCmd(__cmd),
                      static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle)));
        // Send SYNC comand with end cycle to response WRITE command.
        InterChiplet::sendSyncCmd(__cmd.m_stdin_fd, static_cast<InterChiplet::TimeType>(
                                                        SRC_DELAY(end_cycle) * __cmd.m_clock_rate));
    }
}

/**
 * @brief Handle WRITE command with UNLOCK flag.
 * @param __cmd Command to handle.
 * @param this->sync_struct Pointer to global synchronize structure.
 */
void GlobalManager::handle_unlock_write_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    // Get mutex ID.
    int uid = DIM_X(__cmd.m_dst);

    // Insert event to benchmark list.
    NetworkBenchItem bench_item(__cmd);
    this->sync_struct->m_bench_list.insert(bench_item);

    if (this->sync_struct->m_lock_timing_struct.isLocked(uid)) {
        // If the mutex is locked, unlock the mutex first.

        // Get the last command. If there is no last command, use this WRITE(UNLOCK) command as the
        // last command.
        InterChiplet::SyncCommand last_cmd = __cmd;
        if (this->sync_struct->m_lock_timing_struct.hasLastCmd(uid)) {
            last_cmd = this->sync_struct->m_lock_timing_struct.getLastCmd(uid);
        }
        // Response this WRITE(UNLOCK) command after it is received by the destination and the
        // destination finished the last command.
        CmdDelayPair end_cycle = this->sync_struct->m_delay_list.getEndCycle(__cmd, last_cmd);

        // Calculate the end cycle of mutex.
        InterChiplet::SyncCommand sync_cmd = __cmd;
        sync_cmd.m_cycle = DST_DELAY(end_cycle);

        // unlock the mutex.
        this->sync_struct->m_lock_timing_struct.unlock(uid, sync_cmd);
        spdlog::debug("{} Transaction ends at {} cycle.", dumpCmd(__cmd),
                      static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle)));
        // Send SYNC comand with end cycle to response WRITE command.
        InterChiplet::sendSyncCmd(__cmd.m_stdin_fd, static_cast<InterChiplet::TimeType>(
                                                        SRC_DELAY(end_cycle) * __cmd.m_clock_rate));

        // Lock the mutex by pending LOCK command.
        // The order of the mutex is handled by LOCK/UNLOCK commands. WRITE(UNLOCK) only need to
        // handle the reorder of one UNLOCK command with pending LOCK commands.
        // Check whether there is any pending LOCK command.
        if (this->sync_struct->m_lock_timing_struct.hasLockCmd(uid)) {
            // If there is any pending LOCK command, lock the mutex by the first pending LOCK
            // command.
            InterChiplet::SyncCommand lock_cmd =
                this->sync_struct->m_lock_timing_struct.popLockCmd(uid);
            // Response this WRITE(LOCK) command after it is received by the destination and the
            // destination finished the WRITE(UNLOCK) command.
            CmdDelayPair end_cycle = this->sync_struct->m_delay_list.getEndCycle(lock_cmd, sync_cmd);

            // Calculate the end cycle of mutex.
            InterChiplet::SyncCommand sync_cmd = lock_cmd;
            sync_cmd.m_cycle = DST_DELAY(end_cycle);

            // lock the mutex.
            this->sync_struct->m_lock_timing_struct.lock(uid, sync_cmd);
            spdlog::debug("\t{} Transaction ends at {} cycle.", dumpCmd(lock_cmd),
                          static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle)));
            // Send SYNC comand with end cycle to response pending LOCK command.
            InterChiplet::sendSyncCmd(
                lock_cmd.m_stdin_fd,
                static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle) * lock_cmd.m_clock_rate));
        }
    } else {
        // If the mutex is unlocked, ignore the UNLOCK command.
        // Response this WRITE(UNLOCK) command immediately when it is received by destination.
        CmdDelayPair end_cycle = this->sync_struct->m_delay_list.getEndCycle(__cmd, __cmd);
        spdlog::debug("{} Transaction ends at {} cycle.", dumpCmd(__cmd),
                      static_cast<InterChiplet::TimeType>(SRC_DELAY(end_cycle)));
        // Send SYNC comand with end cycle to response WRITE command.
        InterChiplet::sendSyncCmd(__cmd.m_stdin_fd, static_cast<InterChiplet::TimeType>(
                                                        SRC_DELAY(end_cycle) * __cmd.m_clock_rate));
    }
}

/**
 * @brief Handle WRITE command.
 * @param __cmd Command to handle.
 * @param __proc_struct Pointer to process structure.
 */
void GlobalManager::handle_write_cmd(const InterChiplet::SyncCommand& __cmd, ProcessStruct *__proc_struct) {
    if (__cmd.m_desc & InterChiplet::SPD_BARRIER) {
        // Special handle WRITE cmmand after BARRIER. WRITE(BARRIER)
        return handle_barrier_write_cmd(__cmd, __proc_struct);
    } else if (__cmd.m_desc & InterChiplet::SPD_LOCK) {
        // Special handle WRITE cmmand after LOCK. WRITE(LOCK)
        return handle_lock_write_cmd(__cmd, __proc_struct);
    } else if (__cmd.m_desc & InterChiplet::SPD_UNLOCK) {
        // Special handle WRITE cmmand after UNLOCK. WRITE(UNLOCK)
        return handle_unlock_write_cmd(__cmd, __proc_struct);
    }

    // Check for paired read command.
    this->insert_send(__cmd, __proc_struct);
    this->update_status(__proc_struct, PS_SUSPEND, false, true);
    spdlog::debug("{} Register WRITE command to wait for popnet simulation.", dumpCmd(__cmd));
}


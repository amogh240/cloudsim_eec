//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>

#include "Interfaces.h"

// ============================================================================
// Algorithm selection: change this #define to switch between algorithms
//   1 = E-Eco (consolidation with overload avoidance)
//   2 = DVFS-Aware Best Fit Decreasing
//   3 = SLA-Tiered Greedy with Periodic Consolidation
//   4 = Round-Robin Spread with Adaptive Power Management
// ============================================================================
#define ALGORITHM 1

// Machine tracking record
struct MachineRecord {
    MachineId_t id;
    CPUType_t cpu;
    unsigned num_cpus;
    unsigned memory_size;
    bool gpus;
    MachineState_t current_s_state;
    CPUPerformance_t current_p_state;
    vector<unsigned> mips;
    vector<unsigned> p_states_power;
    vector<unsigned> s_states_power;
    bool is_waking_up;           // true while transitioning to S0
    unsigned active_task_count;
    unsigned memory_used;
    vector<VMId_t> attached_vms;
};

class Scheduler {
public:
    Scheduler()                 {}
    void Init();
    void MigrationComplete(Time_t time, VMId_t vm_id);
    void NewTask(Time_t now, TaskId_t task_id);
    void PeriodicCheck(Time_t now);
    void Shutdown(Time_t now);
    void TaskComplete(Time_t now, TaskId_t task_id);
    void HandleSLAWarning(Time_t time, TaskId_t task_id);
    void HandleStateChange(Time_t time, MachineId_t machine_id);
    void HandleMemoryWarning(Time_t time, MachineId_t machine_id);

private:
    // ----- Core data structures -----
    vector<VMId_t> vms;
    vector<MachineId_t> machines;

    // Machine tracking
    vector<MachineRecord> machine_records;
    unsigned total_machines;

    // VM tracking
    unordered_map<VMId_t, MachineId_t> vm_to_machine;
    unordered_set<VMId_t> migrating_vms;

    // Task tracking
    unordered_map<TaskId_t, VMId_t> task_to_vm;

    // Per CPU-type + VM-type pools of ready VMs
    // Key: (CPUType_t * 4 + VMType_t) -> list of VMIds with no tasks
    unordered_map<unsigned, vector<VMId_t>> vm_pool;

    // Tasks waiting for a machine to wake up
    // Key: MachineId_t -> list of (task_id, vm_id)
    unordered_map<MachineId_t, vector<pair<TaskId_t, VMId_t>>> pending_tasks;

    // Periodic check counter
    unsigned periodic_count;

    // Algorithm 4 state
    unsigned rr_index;

    // ----- Helper methods -----
    unsigned VMPoolKey(CPUType_t cpu, VMType_t vm);
    VMId_t GetOrCreateVM(CPUType_t cpu, VMType_t vm);
    MachineId_t FindBestMachine_Consolidate(CPUType_t cpu, unsigned mem_needed, bool gpu_pref);
    MachineId_t FindBestMachine_BFD(CPUType_t cpu, unsigned mem_needed, bool gpu_pref);
    MachineId_t FindBestMachine_Tiered(CPUType_t cpu, unsigned mem_needed, bool gpu_pref, SLAType_t sla);
    MachineId_t FindBestMachine_RoundRobin(CPUType_t cpu, unsigned mem_needed, bool gpu_pref);
    MachineId_t WakeUpBestMachine(CPUType_t cpu);
    void AttachVMToMachine(VMId_t vm_id, MachineId_t machine_id);
    void TryPowerDownMachine(MachineId_t machine_id);
    void UpdatePState(MachineId_t machine_id);
    void ConsolidateCheck();
    double GetMachineUtilization(MachineId_t machine_id);
    Priority_t SLAToPriority(SLAType_t sla);
};

#endif /* Scheduler_hpp */

//
//  Scheduler.cpp
//  CloudSim
//
//  CS 378 Energy-Efficient Computing — 4-Algorithm Scheduler
//  Algorithms:
//    1 = E-Eco (consolidation + overload avoidance)  [Beloglazov & Buyya inspired]
//    2 = DVFS-Aware Best Fit Decreasing
//    3 = SLA-Tiered Greedy with Periodic Consolidation
//    4 = Round-Robin Spread with Adaptive Power Management
//
//  Select via #define ALGORITHM in Scheduler.hpp
//

#include "Scheduler.hpp"

// ============================================================================
// Helper utilities
// ============================================================================

unsigned Scheduler::VMPoolKey(CPUType_t cpu, VMType_t vm) {
    return (unsigned)cpu * 4 + (unsigned)vm;
}

Priority_t Scheduler::SLAToPriority(SLAType_t sla) {
    switch (sla) {
        case SLA0: return HIGH_PRIORITY;
        case SLA1: return HIGH_PRIORITY;
        case SLA2: return MID_PRIORITY;
        case SLA3: return LOW_PRIORITY;
    }
    return MID_PRIORITY;
}

// Get or create a VM matching the required CPU type and VM type.
// Tries the pool first; if empty, creates a new one.
VMId_t Scheduler::GetOrCreateVM(CPUType_t cpu, VMType_t vm) {
    unsigned key = VMPoolKey(cpu, vm);
    auto &pool = vm_pool[key];
    if (!pool.empty()) {
        VMId_t vm_id = pool.back();
        pool.pop_back();
        return vm_id;
    }
    // Create a fresh VM
    VMId_t vm_id = VM_Create(vm, cpu);
    vms.push_back(vm_id);
    return vm_id;
}

// Attach a VM to a machine, updating all tracking structures
void Scheduler::AttachVMToMachine(VMId_t vm_id, MachineId_t machine_id) {
    MachineInfo_t dbg = Machine_GetInfo(machine_id);
    SimOutput("DEBUG AttachVM: vm=" + to_string(vm_id) + " machine=" + to_string(machine_id) +
              " s_state=" + to_string(dbg.s_state) + " local_s=" + to_string(machine_records[machine_id].current_s_state) +
              " waking=" + to_string(machine_records[machine_id].is_waking_up) +
              " transitioning=" + to_string(machine_records[machine_id].is_transitioning), 0);
    if (dbg.s_state != S0) {
        SimOutput("WARN AttachVM: Machine " + to_string(machine_id) + " not at S0, skipping attach", 0);
        return;
    }
    VM_Attach(vm_id, machine_id);
    vm_to_machine[vm_id] = machine_id;
    machine_records[machine_id].attached_vms.push_back(vm_id);
    machine_records[machine_id].memory_used += VM_MEMORY_OVERHEAD;
}

// Approximate machine utilization as ratio of active tasks to total cores
double Scheduler::GetMachineUtilization(MachineId_t machine_id) {
    MachineInfo_t info = Machine_GetInfo(machine_id);
    if (info.num_cpus == 0) return 0.0;
    return (double)info.active_tasks / (double)info.num_cpus;
}

// ============================================================================
// Machine power management helpers
// ============================================================================

void Scheduler::TryPowerDownMachine(MachineId_t machine_id) {
    MachineRecord &rec = machine_records[machine_id];
    if (rec.current_s_state != S0 && rec.current_s_state != S0i1) return; // already sleeping
    if (rec.is_waking_up) return;
    if (migration_targets.count(machine_id)) return; // has incoming migrations

    MachineInfo_t info = Machine_GetInfo(machine_id);
    if (info.active_tasks > 0 || info.active_vms > 0) return; // still has work

    // Shut down all VMs on this machine first
    auto vms_copy = rec.attached_vms;  // copy because we modify during loop
    for (auto vm_id : vms_copy) {
        try {
            VMInfo_t vi = VM_GetInfo(vm_id);
            if (vi.active_tasks.empty()) {
                VM_Shutdown(vm_id);
                // Remove from tracking
                auto it = find(rec.attached_vms.begin(), rec.attached_vms.end(), vm_id);
                if (it != rec.attached_vms.end()) rec.attached_vms.erase(it);
                vm_to_machine.erase(vm_id);
                // Remove from vms list
                auto it2 = find(vms.begin(), vms.end(), vm_id);
                if (it2 != vms.end()) vms.erase(it2);
            }
        } catch (...) {
            // VM may already be inactive — remove from local tracking
            auto it = find(rec.attached_vms.begin(), rec.attached_vms.end(), vm_id);
            if (it != rec.attached_vms.end()) rec.attached_vms.erase(it);
            vm_to_machine.erase(vm_id);
            auto it2 = find(vms.begin(), vms.end(), vm_id);
            if (it2 != vms.end()) vms.erase(it2);
        }
    }

    // Power down the machine
    info = Machine_GetInfo(machine_id);
    if (info.active_vms == 0) {
        Machine_SetState(machine_id, S5);
        rec.current_s_state = S5;
        rec.is_transitioning = true;
        SimOutput("PowerDown: Machine " + to_string(machine_id) + " -> S5", 2);
    }
}

// Wake up the best sleeping machine of the required CPU type
MachineId_t Scheduler::WakeUpBestMachine(CPUType_t cpu) {
    // First check if there's already one waking up
    for (unsigned i = 0; i < total_machines; i++) {
        MachineRecord &rec = machine_records[i];
        if (rec.cpu == cpu && rec.is_waking_up) {
            return rec.id; // Already waking up one of this type
        }
    }

    // Find a sleeping machine of the right type — prefer lightest sleep
    MachineId_t best = (MachineId_t)-1;
    int best_s = S5 + 1;
    for (unsigned i = 0; i < total_machines; i++) {
        MachineRecord &rec = machine_records[i];
        if (rec.cpu == cpu && rec.current_s_state != S0 && rec.current_s_state != S0i1 && !rec.is_waking_up && !rec.is_transitioning) {
            if ((int)rec.current_s_state < best_s) {
                best_s = (int)rec.current_s_state;
                best = rec.id;
            }
        }
    }

    if (best != (MachineId_t)-1) {
        Machine_SetState(best, S0);
        machine_records[best].is_waking_up = true;
        machine_records[best].is_transitioning = true;
        SimOutput("WakeUp: Machine " + to_string(best) + " from S" + to_string(best_s) + " -> S0", 2);
    }
    return best;
}

// Update P-state based on current load (used by Algorithms 1, 2, 3)
void Scheduler::UpdatePState(MachineId_t machine_id) {
    MachineRecord &rec = machine_records[machine_id];
    if (rec.current_s_state != S0) return;

    MachineInfo_t info = Machine_GetInfo(machine_id);
    double util = (info.num_cpus > 0) ? (double)info.active_tasks / (double)info.num_cpus : 0.0;

    CPUPerformance_t target;

#if ALGORITHM == 1
    // E-Eco: conservative — keep at P0 if any SLA0/SLA1 tasks
    // Check if any critical SLA tasks on this machine
    bool has_critical = false;
    for (auto vm_id : rec.attached_vms) {
        VMInfo_t vi = VM_GetInfo(vm_id);
        for (auto tid : vi.active_tasks) {
            SLAType_t sla = RequiredSLA(tid);
            if (sla == SLA0 || sla == SLA1) { has_critical = true; break; }
        }
        if (has_critical) break;
    }
    if (has_critical || util > 0.6) {
        target = P0;
    } else if (util > 0.3) {
        target = P1;
    } else if (util > 0.1) {
        target = P2;
    } else {
        target = P3;
    }
#elif ALGORITHM == 2
    // DVFS-Aware BFD: aggressive DVFS based on utilization
    bool has_sla01 = false;
    for (auto vm_id : rec.attached_vms) {
        VMInfo_t vi = VM_GetInfo(vm_id);
        for (auto tid : vi.active_tasks) {
            SLAType_t sla = RequiredSLA(tid);
            if (sla == SLA0 || sla == SLA1) { has_sla01 = true; break; }
        }
        if (has_sla01) break;
    }
    if (has_sla01) {
        target = P0;
    } else if (util > 0.75) {
        target = P0;
    } else if (util > 0.50) {
        target = P1;
    } else if (util > 0.25) {
        target = P2;
    } else {
        target = P3;
    }
#elif ALGORITHM == 3
    // SLA-Tiered: P-state depends on tier
    bool has_sla0 = false, has_sla1 = false;
    for (auto vm_id : rec.attached_vms) {
        VMInfo_t vi = VM_GetInfo(vm_id);
        for (auto tid : vi.active_tasks) {
            SLAType_t sla = RequiredSLA(tid);
            if (sla == SLA0) has_sla0 = true;
            if (sla == SLA1) has_sla1 = true;
        }
    }
    if (has_sla0) {
        target = P0;
    } else if (has_sla1) {
        target = P1;
    } else if (util > 0.5) {
        target = P1;
    } else {
        target = P2;
    }
#else
    // Algorithm 4: fixed moderate P-state
    if (util > 0.5) {
        target = P0;
    } else {
        target = P1;
    }
#endif

    if (target != rec.current_p_state) {
        Machine_SetCorePerformance(machine_id, 0, target);
        rec.current_p_state = target;
    }
}

// ============================================================================
// Machine selection strategies
// ============================================================================

// Algorithm 1: Best-Fit — pick the MOST loaded active machine that still has room
MachineId_t Scheduler::FindBestMachine_Consolidate(CPUType_t cpu, unsigned mem_needed, bool gpu_pref) {
    MachineId_t best = (MachineId_t)-1;
    double best_util = -1.0;

    for (unsigned i = 0; i < total_machines; i++) {
        MachineRecord &rec = machine_records[i];
        if (rec.cpu != cpu) continue;
        if (rec.current_s_state != S0) continue;
        if (rec.is_waking_up) continue;

        MachineInfo_t info = Machine_GetInfo(rec.id);
        unsigned free_mem = (info.memory_size > info.memory_used) ? (info.memory_size - info.memory_used) : 0;
        if (free_mem < mem_needed + VM_MEMORY_OVERHEAD) continue;

        double util = (info.num_cpus > 0) ? (double)info.active_tasks / (double)info.num_cpus : 0.0;

        // Avoid overloaded machines (>85% utilization)
        if (util > 0.85) continue;

        // Prefer GPU machines for GPU tasks
        double score = util;
        if (gpu_pref && rec.gpus) score += 0.01;

        if (score > best_util) {
            best_util = score;
            best = rec.id;
        }
    }
    return best;
}

// Algorithm 2: Best-Fit Decreasing — same as consolidate but no overload check
MachineId_t Scheduler::FindBestMachine_BFD(CPUType_t cpu, unsigned mem_needed, bool gpu_pref) {
    MachineId_t best = (MachineId_t)-1;
    double best_util = -1.0;

    for (unsigned i = 0; i < total_machines; i++) {
        MachineRecord &rec = machine_records[i];
        if (rec.cpu != cpu) continue;
        if (rec.current_s_state != S0) continue;
        if (rec.is_waking_up) continue;

        MachineInfo_t info = Machine_GetInfo(rec.id);
        unsigned free_mem = (info.memory_size > info.memory_used) ? (info.memory_size - info.memory_used) : 0;
        if (free_mem < mem_needed + VM_MEMORY_OVERHEAD) continue;

        double util = (info.num_cpus > 0) ? (double)info.active_tasks / (double)info.num_cpus : 0.0;

        // Pack densely but allow up to 95%
        if (util > 0.95) continue;

        double score = util;
        if (gpu_pref && rec.gpus) score += 0.01;

        if (score > best_util) {
            best_util = score;
            best = rec.id;
        }
    }
    return best;
}

// Algorithm 3: Tiered — machines are logically partitioned by SLA tier
MachineId_t Scheduler::FindBestMachine_Tiered(CPUType_t cpu, unsigned mem_needed, bool gpu_pref, SLAType_t sla) {
    MachineId_t best = (MachineId_t)-1;
    double best_score = -1.0;

    for (unsigned i = 0; i < total_machines; i++) {
        MachineRecord &rec = machine_records[i];
        if (rec.cpu != cpu) continue;
        if (rec.current_s_state != S0) continue;
        if (rec.is_waking_up) continue;

        MachineInfo_t info = Machine_GetInfo(rec.id);
        unsigned free_mem = (info.memory_size > info.memory_used) ? (info.memory_size - info.memory_used) : 0;
        if (free_mem < mem_needed + VM_MEMORY_OVERHEAD) continue;

        double util = (info.num_cpus > 0) ? (double)info.active_tasks / (double)info.num_cpus : 0.0;

        // Check the "tier" of this machine — what SLAs currently run on it
        SLAType_t worst_sla = SLA0;
        for (auto vm_id : rec.attached_vms) {
            VMInfo_t vi = VM_GetInfo(vm_id);
            for (auto tid : vi.active_tasks) {
                SLAType_t ts = RequiredSLA(tid);
                if (ts > worst_sla) worst_sla = ts;
            }
        }

        // Prefer machines already serving the same SLA tier
        double score = 0.0;
        if (info.active_tasks == 0) {
            score = 0.1; // empty machine — last resort
        } else if (worst_sla == sla) {
            score = 1.0 + util; // same tier — highly preferred, pack densely
        } else if (worst_sla > sla) {
            score = 0.5 + util; // lower tier machine — acceptable
        } else {
            // This machine has higher-SLA tasks than our task — avoid polluting
            if (sla == SLA3) {
                score = 0.05; // strongly avoid putting SLA3 on SLA0 machines
            } else {
                score = 0.3 + util;
            }
        }

        // Max utilization guard depends on SLA
        double max_util;
        switch (sla) {
            case SLA0: max_util = 0.70; break;
            case SLA1: max_util = 0.80; break;
            case SLA2: max_util = 0.85; break;
            case SLA3: max_util = 0.95; break;
        }
        if (util > max_util) continue;

        if (gpu_pref && rec.gpus) score += 0.02;

        if (score > best_score) {
            best_score = score;
            best = rec.id;
        }
    }
    return best;
}

// Algorithm 4: Round-robin among active compatible machines
MachineId_t Scheduler::FindBestMachine_RoundRobin(CPUType_t cpu, unsigned mem_needed, bool gpu_pref) {
    unsigned start = rr_index;
    for (unsigned attempts = 0; attempts < total_machines; attempts++) {
        unsigned idx = (start + attempts) % total_machines;
        MachineRecord &rec = machine_records[idx];
        if (rec.cpu != cpu) continue;
        if (rec.current_s_state != S0) continue;
        if (rec.is_waking_up) continue;

        MachineInfo_t info = Machine_GetInfo(rec.id);
        unsigned free_mem = (info.memory_size > info.memory_used) ? (info.memory_size - info.memory_used) : 0;
        if (free_mem < mem_needed + VM_MEMORY_OVERHEAD) continue;

        double util = (info.num_cpus > 0) ? (double)info.active_tasks / (double)info.num_cpus : 0.0;
        if (util > 0.90) continue;

        rr_index = (idx + 1) % total_machines;
        return rec.id;
    }
    return (MachineId_t)-1;
}

// ============================================================================
// Consolidation check (used by algorithms 1 and 3)
// ============================================================================

void Scheduler::ConsolidateCheck() {
    // Track which machines are sources/targets in this consolidation pass
    unordered_set<unsigned> sources_this_pass;

    // First pass: power down idle machines (no VMs, no incoming migrations)
    for (unsigned i = 0; i < total_machines; i++) {
        MachineRecord &rec = machine_records[i];
        if (rec.current_s_state != S0) continue;
        if (rec.is_waking_up || rec.is_transitioning) continue;
        if (migration_targets.count(rec.id)) continue; // has incoming migration

        MachineInfo_t info = Machine_GetInfo(rec.id);
        if (info.active_tasks == 0 && info.active_vms == 0) {
            TryPowerDownMachine(rec.id);
        }
    }

    // Second pass: consolidate underloaded machines
    for (unsigned i = 0; i < total_machines; i++) {
        MachineRecord &rec = machine_records[i];
        if (rec.current_s_state != S0) continue;
        if (rec.is_waking_up || rec.is_transitioning) continue;

        MachineInfo_t info = Machine_GetInfo(rec.id);
        double util = (info.num_cpus > 0) ? (double)info.active_tasks / (double)info.num_cpus : 0.0;

        // Only consolidate if very underloaded and has tasks
        if (util > 0.20 || info.active_tasks == 0) continue;

        // Try to migrate VMs off this machine
        auto attached = rec.attached_vms;
        for (auto vm_id : attached) {
            if (migrating_vms.count(vm_id)) continue;
            VMInfo_t vi = VM_GetInfo(vm_id);
            if (vi.active_tasks.empty()) continue;

            // Find a target machine
            unsigned vm_mem = 0;
            for (auto t : vi.active_tasks) {
                vm_mem += GetTaskMemory(t);
            }

            MachineId_t target = (MachineId_t)-1;
            double best_util = -1.0;
            for (unsigned j = 0; j < total_machines; j++) {
                if (j == i) continue;
                // Don't target a machine that was a source this pass (prevents cross-migration)
                if (sources_this_pass.count(j)) continue;
                MachineRecord &trec = machine_records[j];
                if (trec.cpu != rec.cpu) continue;
                if (trec.current_s_state != S0) continue;
                if (trec.is_waking_up || trec.is_transitioning) continue;

                MachineInfo_t tinfo = Machine_GetInfo(trec.id);
                // Double-check simulator's actual state
                if (tinfo.s_state != S0) continue;

                unsigned free_mem = (tinfo.memory_size > tinfo.memory_used) ? (tinfo.memory_size - tinfo.memory_used) : 0;
                if (free_mem < vm_mem + VM_MEMORY_OVERHEAD) continue;

                double tu = (tinfo.num_cpus > 0) ? (double)(tinfo.active_tasks + (unsigned)vi.active_tasks.size()) / (double)tinfo.num_cpus : 0.0;
                if (tu > 0.85) continue;

                if (tu > best_util) {
                    best_util = tu;
                    target = trec.id;
                }
            }

            if (target != (MachineId_t)-1) {
                VM_Migrate(vm_id, target);
                migrating_vms.insert(vm_id);
                migration_targets.insert(target);
                sources_this_pass.insert(i);
                SimOutput("Consolidate: Migrating VM " + to_string(vm_id) + " from machine " + to_string(rec.id) + " to " + to_string(target), 2);
                break; // Only one migration at a time per machine
            }
        }
    }
}

// ============================================================================
// Scheduler::Init()
// ============================================================================

void Scheduler::Init() {
    total_machines = Machine_GetTotal();
    periodic_count = 0;
    rr_index = 0;

    SimOutput("Scheduler::Init(): Total machines = " + to_string(total_machines), 1);

    // Build machine records
    machine_records.resize(total_machines);
    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        MachineRecord &rec = machine_records[i];
        rec.id = MachineId_t(i);
        rec.cpu = info.cpu;
        rec.num_cpus = info.num_cpus;
        rec.memory_size = info.memory_size;
        rec.gpus = info.gpus;
        rec.current_s_state = S0;
        rec.current_p_state = P0;
        rec.mips = info.performance;
        rec.p_states_power = info.p_states;
        rec.s_states_power = info.s_states;
        rec.is_waking_up = false;
        rec.is_transitioning = false;
        rec.active_task_count = 0;
        rec.memory_used = 0;
        machines.push_back(MachineId_t(i));
    }

    // Count machines per CPU type
    unordered_map<unsigned, unsigned> cpu_count;
    for (unsigned i = 0; i < total_machines; i++) {
        cpu_count[(unsigned)machine_records[i].cpu]++;
    }

#if ALGORITHM == 1
    // E-Eco: Start with all machines active, each with a LINUX VM.
    // Idle machines will be powered down by PeriodicCheck once the simulation stabilizes.
    {
        for (unsigned i = 0; i < total_machines; i++) {
            VMId_t vm_id = VM_Create(LINUX, machine_records[i].cpu);
            vms.push_back(vm_id);
            AttachVMToMachine(vm_id, MachineId_t(i));
        }
    }
#elif ALGORITHM == 2
    // DVFS-BFD: Start with all machines active, start at P1 for energy savings.
    // Idle machines will be powered down by PeriodicCheck.
    {
        for (unsigned i = 0; i < total_machines; i++) {
            VMId_t vm_id = VM_Create(LINUX, machine_records[i].cpu);
            vms.push_back(vm_id);
            AttachVMToMachine(vm_id, MachineId_t(i));
            Machine_SetCorePerformance(MachineId_t(i), 0, P1);
            machine_records[i].current_p_state = P1;
        }
    }
#elif ALGORITHM == 3
    // SLA-Tiered: Start with all machines active.
    // Idle machines will be powered down by PeriodicCheck.
    {
        for (unsigned i = 0; i < total_machines; i++) {
            VMId_t vm_id = VM_Create(LINUX, machine_records[i].cpu);
            vms.push_back(vm_id);
            AttachVMToMachine(vm_id, MachineId_t(i));
        }
    }
#else
    // Algorithm 4: Round-Robin — keep all machines active
    {
        for (unsigned i = 0; i < total_machines; i++) {
            VMId_t vm_id = VM_Create(LINUX, machine_records[i].cpu);
            vms.push_back(vm_id);
            AttachVMToMachine(vm_id, MachineId_t(i));
            // Set to P1 for modest savings
            Machine_SetCorePerformance(MachineId_t(i), 0, P1);
            machine_records[i].current_p_state = P1;
        }
    }
#endif

    SimOutput("Scheduler::Init(): Initialization complete", 1);
}

// ============================================================================
// Scheduler::NewTask()
// ============================================================================

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t task = GetTaskInfo(task_id);
    CPUType_t cpu = task.required_cpu;
    VMType_t vm_type = task.required_vm;
    unsigned mem = task.required_memory;
    SLAType_t sla = task.required_sla;
    bool gpu = task.gpu_capable;
    Priority_t priority = SLAToPriority(sla);

    // Find the best machine according to the current algorithm
    MachineId_t target_machine = (MachineId_t)-1;

#if ALGORITHM == 1
    target_machine = FindBestMachine_Consolidate(cpu, mem, gpu);
#elif ALGORITHM == 2
    target_machine = FindBestMachine_BFD(cpu, mem, gpu);
#elif ALGORITHM == 3
    target_machine = FindBestMachine_Tiered(cpu, mem, gpu, sla);
#else
    target_machine = FindBestMachine_RoundRobin(cpu, mem, gpu);
#endif

    if (target_machine == (MachineId_t)-1) {
        // No suitable active machine — wake one up
        MachineId_t waking = WakeUpBestMachine(cpu);
        if (waking != (MachineId_t)-1) {
            // Create VM and queue the task for when machine is ready
            VMId_t vm_id = VM_Create(vm_type, cpu);
            vms.push_back(vm_id);
            pending_tasks[waking].push_back({task_id, vm_id});
            SimOutput("NewTask: Task " + to_string(task_id) + " queued for machine " + to_string(waking), 2);
            return;
        }

        // Fallback: find ANY active machine of the right CPU type, even if loaded
        for (unsigned i = 0; i < total_machines; i++) {
            MachineRecord &rec = machine_records[i];
            if (rec.cpu == cpu && rec.current_s_state == S0 && !rec.is_waking_up) {
                target_machine = rec.id;
                break;
            }
        }

        if (target_machine == (MachineId_t)-1) {
            // Absolute last resort: queue to any waking machine of right type
            for (unsigned i = 0; i < total_machines; i++) {
                if (machine_records[i].cpu == cpu && machine_records[i].is_waking_up) {
                    VMId_t vm_id = VM_Create(vm_type, cpu);
                    vms.push_back(vm_id);
                    pending_tasks[machine_records[i].id].push_back({task_id, vm_id});
                    return;
                }
            }
            SimOutput("NewTask: ERROR — no machine available for task " + to_string(task_id), 0);
            return;
        }
    }

    // Find an existing compatible VM on this machine, or create a new one
    VMId_t chosen_vm = (VMId_t)-1;
    for (auto vm_id : machine_records[target_machine].attached_vms) {
        // Check if this VM is the right type and isn't migrating
        if (migrating_vms.count(vm_id)) continue;
        VMInfo_t vi = VM_GetInfo(vm_id);
        if (vi.vm_type == vm_type && vi.cpu == cpu) {
            chosen_vm = vm_id;
            break;
        }
    }

    if (chosen_vm == (VMId_t)-1) {
        // Create a new VM and attach it
        chosen_vm = VM_Create(vm_type, cpu);
        vms.push_back(chosen_vm);
        AttachVMToMachine(chosen_vm, target_machine);
    }

    VM_AddTask(chosen_vm, task_id, priority);
    task_to_vm[task_id] = chosen_vm;
    machine_records[target_machine].active_task_count++;

    // Update P-state after adding a new task
    UpdatePState(target_machine);

    SimOutput("NewTask: Task " + to_string(task_id) + " -> VM " + to_string(chosen_vm) +
              " on machine " + to_string(target_machine), 3);
}

// ============================================================================
// Scheduler::TaskComplete()
// ============================================================================

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("TaskComplete: Task " + to_string(task_id) + " finished at " + to_string(now), 3);

    // Find which VM and machine this task was on
    auto it = task_to_vm.find(task_id);
    if (it == task_to_vm.end()) return;
    VMId_t vm_id = it->second;
    task_to_vm.erase(it);

    auto mit = vm_to_machine.find(vm_id);
    if (mit == vm_to_machine.end()) return;
    MachineId_t machine_id = mit->second;

    if (machine_records[machine_id].active_task_count > 0)
        machine_records[machine_id].active_task_count--;

    // Update P-state for this machine
    UpdatePState(machine_id);

    // Check if machine can be powered down
#if ALGORITHM == 1 || ALGORITHM == 3
    MachineInfo_t info = Machine_GetInfo(machine_id);
    if (info.active_tasks == 0) {
        // Check all VMs — if all empty, power down
        bool all_empty = true;
        for (auto vid : machine_records[machine_id].attached_vms) {
            VMInfo_t vi = VM_GetInfo(vid);
            if (!vi.active_tasks.empty()) { all_empty = false; break; }
        }
        if (all_empty) {
            TryPowerDownMachine(machine_id);
        }
    }
#elif ALGORITHM == 4
    // In round-robin, step down through sleep states for idle machines
    MachineInfo_t info = Machine_GetInfo(machine_id);
    if (info.active_tasks == 0) {
        // Don't immediately power down — let PeriodicCheck handle gradual sleep
    }
#endif
}

// ============================================================================
// Scheduler::PeriodicCheck()
// ============================================================================

void Scheduler::PeriodicCheck(Time_t now) {
    periodic_count++;

#if ALGORITHM == 1
    // E-Eco: Run consolidation every 20 checks
    if (periodic_count % 20 == 0) {
        ConsolidateCheck();
    }
    // Update P-states for all active machines every 10 checks
    if (periodic_count % 10 == 0) {
        for (unsigned i = 0; i < total_machines; i++) {
            if (machine_records[i].current_s_state == S0 && !machine_records[i].is_waking_up) {
                UpdatePState(MachineId_t(i));
            }
        }
    }
#elif ALGORITHM == 2
    // DVFS-BFD: Just update P-states periodically
    if (periodic_count % 10 == 0) {
        for (unsigned i = 0; i < total_machines; i++) {
            if (machine_records[i].current_s_state == S0 && !machine_records[i].is_waking_up) {
                UpdatePState(MachineId_t(i));
            }
        }
        // Also power down empty machines
        for (unsigned i = 0; i < total_machines; i++) {
            if (machine_records[i].current_s_state == S0 && !machine_records[i].is_waking_up) {
                MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
                if (info.active_tasks == 0 && info.active_vms > 0) {
                    bool all_empty = true;
                    for (auto vid : machine_records[i].attached_vms) {
                        VMInfo_t vi = VM_GetInfo(vid);
                        if (!vi.active_tasks.empty()) { all_empty = false; break; }
                    }
                    if (all_empty) {
                        TryPowerDownMachine(MachineId_t(i));
                    }
                }
            }
        }
    }
#elif ALGORITHM == 3
    // SLA-Tiered: Consolidation every 30 checks (less aggressive)
    if (periodic_count % 30 == 0) {
        ConsolidateCheck();
    }
    if (periodic_count % 15 == 0) {
        for (unsigned i = 0; i < total_machines; i++) {
            if (machine_records[i].current_s_state == S0 && !machine_records[i].is_waking_up) {
                UpdatePState(MachineId_t(i));
            }
        }
    }
#else
    // Algorithm 4: Gradually sleep idle machines
    if (periodic_count % 25 == 0) {
        for (unsigned i = 0; i < total_machines; i++) {
            MachineRecord &rec = machine_records[i];
            if (rec.current_s_state != S0) continue;
            if (rec.is_waking_up) continue;

            MachineInfo_t info = Machine_GetInfo(rec.id);
            if (info.active_tasks == 0) {
                bool all_empty = true;
                for (auto vid : rec.attached_vms) {
                    VMInfo_t vi = VM_GetInfo(vid);
                    if (!vi.active_tasks.empty()) { all_empty = false; break; }
                }
                if (all_empty) {
                    TryPowerDownMachine(rec.id);
                }
            }
        }
    }
#endif
}

// ============================================================================
// Scheduler::HandleSLAWarning()
// ============================================================================

void Scheduler::HandleSLAWarning(Time_t time, TaskId_t task_id) {
    SimOutput("SLAWarning: Task " + to_string(task_id) + " at time " + to_string(time), 1);

    // Boost priority of the at-risk task
    TaskInfo_t task = GetTaskInfo(task_id);
    if (task.priority != HIGH_PRIORITY) {
        SetTaskPriority(task_id, HIGH_PRIORITY);
    }

    // Ensure the machine is at full performance
    auto it = task_to_vm.find(task_id);
    if (it != task_to_vm.end()) {
        auto mit = vm_to_machine.find(it->second);
        if (mit != vm_to_machine.end()) {
            MachineId_t mid = mit->second;
            if (machine_records[mid].current_p_state != P0) {
                Machine_SetCorePerformance(mid, 0, P0);
                machine_records[mid].current_p_state = P0;
            }
        }
    }
}

// ============================================================================
// Scheduler::MigrationComplete()
// ============================================================================

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    SimOutput("MigrationComplete: VM " + to_string(vm_id) + " at " + to_string(time), 2);
    migrating_vms.erase(vm_id);

    // Update vm_to_machine mapping
    VMInfo_t vi = VM_GetInfo(vm_id);
    MachineId_t new_machine = vi.machine_id;
    migration_targets.erase(new_machine);

    // Remove from old machine record
    MachineId_t old_machine = vm_to_machine[vm_id];
    auto &old_vms = machine_records[old_machine].attached_vms;
    old_vms.erase(remove(old_vms.begin(), old_vms.end(), vm_id), old_vms.end());

    // Add to new machine record
    vm_to_machine[vm_id] = new_machine;
    machine_records[new_machine].attached_vms.push_back(vm_id);

    // Check if old machine can be powered down
    MachineInfo_t old_info = Machine_GetInfo(old_machine);
    if (old_info.active_tasks == 0 && old_info.active_vms == 0) {
        TryPowerDownMachine(old_machine);
    }
}

// ============================================================================
// Scheduler::HandleStateChange()
// ============================================================================

void Scheduler::HandleStateChange(Time_t time, MachineId_t machine_id) {
    MachineInfo_t info = Machine_GetInfo(machine_id);
    machine_records[machine_id].current_s_state = info.s_state;
    machine_records[machine_id].is_transitioning = false;

    if (info.s_state == S0) {
        machine_records[machine_id].is_waking_up = false;
        SimOutput("StateChange: Machine " + to_string(machine_id) + " is now S0 (ready)", 2);

        // Process any pending tasks
        auto it = pending_tasks.find(machine_id);
        if (it != pending_tasks.end()) {
            for (auto &[task_id, vm_id] : it->second) {
                // Check if the task is still valid (not completed)
                TaskInfo_t tinfo = GetTaskInfo(task_id);
                if (tinfo.completed) continue;

                AttachVMToMachine(vm_id, machine_id);
                VM_AddTask(vm_id, task_id, SLAToPriority(tinfo.required_sla));
                task_to_vm[task_id] = vm_id;
                machine_records[machine_id].active_task_count++;
            }
            pending_tasks.erase(it);
        }

        UpdatePState(machine_id);
    }
}

// ============================================================================
// Scheduler::HandleMemoryWarning()
// ============================================================================

void Scheduler::HandleMemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning: Machine " + to_string(machine_id) + " at time " + to_string(time), 1);

    // Try to migrate a VM off this machine
    MachineRecord &rec = machine_records[machine_id];
    for (auto vm_id : rec.attached_vms) {
        if (migrating_vms.count(vm_id)) continue;
        VMInfo_t vi = VM_GetInfo(vm_id);
        if (vi.active_tasks.empty()) continue;

        // Find the VM with the most memory usage (or just the first one)
        // Try to migrate to a machine with more free memory
        for (unsigned j = 0; j < total_machines; j++) {
            if (j == (unsigned)machine_id) continue;
            MachineRecord &trec = machine_records[j];
            if (trec.cpu != rec.cpu) continue;
            if (trec.current_s_state != S0) continue;
            if (trec.is_waking_up) continue;

            MachineInfo_t tinfo = Machine_GetInfo(trec.id);
            if (tinfo.memory_size - tinfo.memory_used > rec.memory_size / 4) {
                MachineInfo_t tdbg = Machine_GetInfo(trec.id);
                if (tdbg.s_state != S0) {
                    SimOutput("WARN MemoryMigrate: target machine " + to_string(trec.id) + " not S0, skipping", 0);
                    continue;
                }
                VM_Migrate(vm_id, trec.id);
                migrating_vms.insert(vm_id);
                migration_targets.insert(trec.id);
                SimOutput("MemoryMigrate: VM " + to_string(vm_id) + " -> machine " + to_string(trec.id), 2);
                return;
            }
        }
    }
}

// ============================================================================
// Scheduler::Shutdown()
// ============================================================================

void Scheduler::Shutdown(Time_t time) {
    SimOutput("Scheduler::Shutdown at time " + to_string(time), 1);

    // Shut down all remaining VMs — iterate over a copy since VM_Shutdown may modify state
    auto vms_copy = vms;
    for (auto vm_id : vms_copy) {
        try {
            VMInfo_t vi = VM_GetInfo(vm_id);
            // Only shut down if the VM is still attached and has no tasks
            if (vi.active_tasks.empty()) {
                VM_Shutdown(vm_id);
            }
        } catch (const runtime_error &) {
            // VM was already shut down or invalid — ignore
        } catch (...) {
            // Catch all other exceptions
        }
    }
    vms.clear();
    vm_to_machine.clear();

    SimOutput("Scheduler::Shutdown complete", 1);
}

// ============================================================================
// Public interface (called by simulator)
// ============================================================================

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning(): Machine " + to_string(machine_id) + " at time " + to_string(time), 0);
    Scheduler.HandleMemoryWarning(time, machine_id);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    SimOutput("MigrationDone(): VM " + to_string(vm_id) + " at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    SimOutput("SchedulerCheck(): called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    cout << "============================================" << endl;
    cout << "     SIMULATION RESULTS (Algorithm " << ALGORITHM << ")" << endl;
    cout << "============================================" << endl;
    cout << "SLA violation report" << endl;
    cout << "  SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "  SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "  SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "  SLA3: (best effort)" << endl;
    cout << "Total Energy: " << Machine_GetClusterEnergy() << " KW-Hour" << endl;
    cout << "Simulation time: " << double(time) / 1000000 << " seconds" << endl;
    cout << "============================================" << endl;
    SimOutput("SimulationComplete(): at time " + to_string(time), 4);

    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    Scheduler.HandleSLAWarning(time, task_id);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    Scheduler.HandleStateChange(time, machine_id);
}

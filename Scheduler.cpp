//
//  Scheduler.cpp
//  CloudSim
//
//  Greedy Energy: at each task arrival, pick the compatible machine
//  that adds the least marginal energy cost. Prefer machines already
//  busy (incremental cost of one more task is low) and use DVFS to
//  keep lightly-loaded machines at lower P-states.
//

#include <cstdint>
#include "Scheduler.hpp"

void Scheduler::Init() {
    total_machines = Machine_GetTotal();
    rr_index = 0;

    SimOutput("Scheduler::Init(): Total machines = " + to_string(total_machines), 3);

    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        machines.push_back(MachineId_t(i));

        VMId_t vm = VM_Create(LINUX, info.cpu);
        vms.push_back(vm);
        VM_Attach(vm, MachineId_t(i));
        vm_to_machine[vm] = MachineId_t(i);
    }

    // start everything at lowest P-state -- we'll ramp up as tasks arrive
    for (unsigned i = 0; i < total_machines; i++) {
        Machine_SetCorePerformance(MachineId_t(i), 0, P3);
    }

    SimOutput("Scheduler::Init(): Done", 1);
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    CPUType_t required_cpu = RequiredCPUType(task_id);
    SLAType_t sla = RequiredSLA(task_id);

    Priority_t priority;
    if (sla <= SLA1) priority = HIGH_PRIORITY;
    else if (sla == SLA2) priority = MID_PRIORITY;
    else priority = LOW_PRIORITY;

    // greedy: pick the compatible machine where adding this task costs
    // the least additional energy. A machine already running N tasks
    // is already paying its base S-state power, so the marginal cost
    // is just the per-core dynamic power at its current P-state.
    // An idle machine would need to ramp up from scratch (high cost).
    // So we score by: (p_state power) / (active_tasks + 1)
    // Lower score = cheaper to add a task here.

    MachineId_t best = (MachineId_t)-1;
    double best_cost = 1e18;

    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.s_state != S0) continue;
        if (info.cpu != required_cpu) continue;

        // marginal energy cost estimate:
        // if machine is idle, adding a task means we pay full S0 + core power
        // if machine is busy, core power is already being paid, marginal = small
        double base_power = info.s_states[0]; // S0 power
        double core_power = info.p_states[0]; // P0 core power (worst case)

        double cost;
        if (info.active_tasks == 0) {
            // going from idle to active -- expensive
            cost = base_power + core_power;
        } else {
            // already active -- marginal cost is just one more core
            cost = core_power / (info.active_tasks + 1.0);
        }

        if (cost < best_cost) {
            best_cost = cost;
            best = MachineId_t(i);
        }
    }

    if (best == (MachineId_t)-1) {
        // fallback: any compatible machine
        for (unsigned i = 0; i < total_machines; i++) {
            MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
            if (info.cpu == required_cpu && info.s_state == S0) {
                best = MachineId_t(i);
                break;
            }
        }
    }

    if (best != (MachineId_t)-1) {
        VM_AddTask(vms[best], task_id, priority);
        task_to_vm[task_id] = vms[best];

        // after placing, set P-state based on new utilization
        MachineInfo_t info = Machine_GetInfo(best);
        double util = (double)info.active_tasks / info.num_cpus;

        // for critical SLA tasks, always use P0
        if (sla <= SLA1) {
            Machine_SetCorePerformance(best, 0, P0);
        } else if (util > 0.7) {
            Machine_SetCorePerformance(best, 0, P0);
        } else if (util > 0.4) {
            Machine_SetCorePerformance(best, 0, P1);
        } else if (util > 0.2) {
            Machine_SetCorePerformance(best, 0, P2);
        } else {
            Machine_SetCorePerformance(best, 0, P3);
        }
    }
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    task_to_vm.erase(task_id);

    // scale down P-states on machines that got lighter
    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.s_state != S0) continue;
        double util = (info.num_cpus > 0) ? (double)info.active_tasks / info.num_cpus : 0.0;

        if (util > 0.7)
            Machine_SetCorePerformance(MachineId_t(i), 0, P0);
        else if (util > 0.4)
            Machine_SetCorePerformance(MachineId_t(i), 0, P1);
        else if (util > 0.1)
            Machine_SetCorePerformance(MachineId_t(i), 0, P2);
        else
            Machine_SetCorePerformance(MachineId_t(i), 0, P3);
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // re-evaluate DVFS across all machines
    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.s_state != S0) continue;
        double util = (info.num_cpus > 0) ? (double)info.active_tasks / info.num_cpus : 0.0;

        if (util > 0.7)
            Machine_SetCorePerformance(MachineId_t(i), 0, P0);
        else if (util > 0.4)
            Machine_SetCorePerformance(MachineId_t(i), 0, P1);
        else if (util > 0.1)
            Machine_SetCorePerformance(MachineId_t(i), 0, P2);
        else
            Machine_SetCorePerformance(MachineId_t(i), 0, P3);
    }
}

void Scheduler::HandleSLAWarning(Time_t time, TaskId_t task_id) {
    SetTaskPriority(task_id, HIGH_PRIORITY);

    auto it = task_to_vm.find(task_id);
    if (it != task_to_vm.end()) {
        auto mit = vm_to_machine.find(it->second);
        if (mit != vm_to_machine.end()) {
            Machine_SetCorePerformance(mit->second, 0, P0);
        }
    }
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
}

void Scheduler::HandleStateChange(Time_t time, MachineId_t machine_id) {
}

void Scheduler::HandleMemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning on machine " + to_string(machine_id), 0);
}

void Scheduler::Shutdown(Time_t time) {
    for (auto &vm : vms) {
        VM_Shutdown(vm);
    }
}

// public interface

static Scheduler scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): starting up", 4);
    scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    scheduler.HandleMemoryWarning(time, machine_id);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << " KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time) / 1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
    scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    scheduler.HandleSLAWarning(time, task_id);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    scheduler.HandleStateChange(time, machine_id);
}

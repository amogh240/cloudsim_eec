 //
//  Scheduler.cpp
//  CloudSim
//

#include <cstdint>
#include "Scheduler.hpp"

void Scheduler::Init() {
    total_machines = Machine_GetTotal();
    rr_index = 0;

    SimOutput("Scheduler::Init(): Total machines = " + to_string(total_machines), 3);

    // start all machines active with a VM attached
    // consolidation happens reactively as machines go idle
    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        machines.push_back(MachineId_t(i));

        VMId_t vm = VM_Create(LINUX, info.cpu);
        vms.push_back(vm);
        VM_Attach(vm, MachineId_t(i));
        vm_to_machine[vm] = MachineId_t(i);
        machine_has_vm.push_back(true);
    }

    SimOutput("Scheduler::Init(): Done", 1);
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    CPUType_t required_cpu = RequiredCPUType(task_id);
    SLAType_t sla = RequiredSLA(task_id);
    unsigned mem = GetTaskMemory(task_id);

    Priority_t priority;
    if (sla <= SLA1) priority = HIGH_PRIORITY;
    else if (sla == SLA2) priority = MID_PRIORITY;
    else priority = LOW_PRIORITY;

    // find the most-loaded active compatible machine that still has room
    MachineId_t best = (MachineId_t)-1;
    int best_tasks = -1;

    for (unsigned i = 0; i < total_machines; i++) {
        if (!machine_has_vm[i]) continue;
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.s_state != S0) continue;
        if (info.cpu != required_cpu) continue;
        if (info.active_tasks >= info.num_cpus) continue;
        if (info.memory_size - info.memory_used < mem + VM_MEMORY_OVERHEAD) continue;

        if ((int)info.active_tasks > best_tasks) {
            best_tasks = info.active_tasks;
            best = MachineId_t(i);
        }
    }

    // no room -- wake up a sleeping machine of the right type
    if (best == (MachineId_t)-1) {
        for (unsigned i = 0; i < total_machines; i++) {
            MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
            if (info.cpu != required_cpu) continue;
            if (info.s_state == S0) continue;

            Machine_SetState(MachineId_t(i), S0);
            VMId_t vm = VM_Create(LINUX, info.cpu);
            vms[i] = vm;
            VM_Attach(vm, MachineId_t(i));
            vm_to_machine[vm] = MachineId_t(i);
            machine_has_vm[i] = true;
            best = MachineId_t(i);
            break;
        }
    }

    // last fallback: overload any active compatible machine
    if (best == (MachineId_t)-1) {
        for (unsigned i = 0; i < total_machines; i++) {
            if (!machine_has_vm[i]) continue;
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
    } else {
        SimOutput("NewTask: no machine for task " + to_string(task_id), 0);
    }
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    task_to_vm.erase(task_id);

    // power down idle machines, but keep at least 1 per CPU type
    unordered_map<unsigned, unsigned> active_count;
    for (unsigned i = 0; i < total_machines; i++) {
        if (!machine_has_vm[i]) continue;
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.s_state == S0)
            active_count[(unsigned)info.cpu]++;
    }

    for (unsigned i = 0; i < total_machines; i++) {
        if (!machine_has_vm[i]) continue;
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.s_state != S0 || info.active_tasks > 0) continue;
        if (active_count[(unsigned)info.cpu] <= 1) continue;

        VM_Shutdown(vms[i]);
        vms[i] = (VMId_t)-1;
        machine_has_vm[i] = false;
        Machine_SetState(MachineId_t(i), S5);
        active_count[(unsigned)info.cpu]--;
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // same idle sweep, keep at least 1 per CPU type
    unordered_map<unsigned, unsigned> active_count;
    for (unsigned i = 0; i < total_machines; i++) {
        if (!machine_has_vm[i]) continue;
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.s_state == S0)
            active_count[(unsigned)info.cpu]++;
    }

    for (unsigned i = 0; i < total_machines; i++) {
        if (!machine_has_vm[i]) continue;
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.s_state != S0 || info.active_tasks > 0) continue;
        if (active_count[(unsigned)info.cpu] <= 1) continue;

        VM_Shutdown(vms[i]);
        vms[i] = (VMId_t)-1;
        machine_has_vm[i] = false;
        Machine_SetState(MachineId_t(i), S5);
        active_count[(unsigned)info.cpu]--;
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
    for (unsigned i = 0; i < vms.size(); i++) {
        if (vms[i] != (VMId_t)-1 && machine_has_vm[i]) {
            VM_Shutdown(vms[i]);
        }
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

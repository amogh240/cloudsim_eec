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

    // set up one VM per machine, attach it
    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        machines.push_back(MachineId_t(i));

        VMId_t vm = VM_Create(LINUX, info.cpu);
        vms.push_back(vm);
        VM_Attach(vm, MachineId_t(i));
        vm_to_machine[vm] = MachineId_t(i);
    }


    // SLA-Tiered: keep all machines on, first half runs at P0 for critical tasks,
    // second half at P2 for low-priority
    for (unsigned i = 0; i < total_machines; i++) {
        if (i < total_machines / 2)
            Machine_SetCorePerformance(MachineId_t(i), 0, P0);
        else
            Machine_SetCorePerformance(MachineId_t(i), 0, P2);
    }



    SimOutput("Scheduler::Init(): Done", 1);
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    CPUType_t required_cpu = RequiredCPUType(task_id);
    VMType_t required_vm = RequiredVMType(task_id);
    SLAType_t sla = RequiredSLA(task_id);
    bool gpu = IsTaskGPUCapable(task_id);
    unsigned mem = GetTaskMemory(task_id);

    Priority_t priority;
    if (sla <= SLA1) priority = HIGH_PRIORITY;
    else if (sla == SLA2) priority = MID_PRIORITY;
    else priority = LOW_PRIORITY;

// SLA-Tiered: critical tasks go to first half, low-priority to second half

    MachineId_t target = (MachineId_t)-1;
    unsigned start, end;

    if (sla <= SLA1) {
        start = 0;
        end = total_machines / 2;
    } else {
        start = total_machines / 2;
        end = total_machines;
    }

    // find least-loaded compatible machine in our tier
    int fewest = 999999;
    for (unsigned i = start; i < end; i++) {
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
        if (info.cpu != required_cpu) continue;
        if (info.s_state != S0) continue;
        if ((int)info.active_tasks < fewest) {
            fewest = info.active_tasks;
            target = MachineId_t(i);
        }
    }

    // fallback: search the other tier
    if (target == (MachineId_t)-1) {
        for (unsigned i = 0; i < total_machines; i++) {
            MachineInfo_t info = Machine_GetInfo(MachineId_t(i));
            if (info.cpu != required_cpu || info.s_state != S0) continue;
            if ((int)info.active_tasks < fewest) {
                fewest = info.active_tasks;
                target = MachineId_t(i);
            }
        }
    }

    if (target != (MachineId_t)-1) {
        VM_AddTask(vms[target], task_id, priority);
        task_to_vm[task_id] = vms[target];
    }
}



void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    task_to_vm.erase(task_id);

}

void Scheduler::PeriodicCheck(Time_t now) {

}

void Scheduler::HandleSLAWarning(Time_t time, TaskId_t task_id) {
    // boost the task priority and crank up the machine
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
    // nothing complex for now
}

void Scheduler::HandleStateChange(Time_t time, MachineId_t machine_id) {
    // machine finished transitioning, nothing to do
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

    scheduler.Shutdown(time);
}


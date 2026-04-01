//
//  Scheduler.hpp
//  CloudSim
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <vector>
#include <unordered_map>
#include <algorithm>

#include "Interfaces.h"


class Scheduler {
public:
    Scheduler() {}
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
    vector<VMId_t> vms;
    vector<MachineId_t> machines;
    unsigned total_machines;
     vector<bool> machine_has_vm;  

    // track which VM each task is on, and which machine each VM is on
    unordered_map<TaskId_t, VMId_t> task_to_vm;
    unordered_map<VMId_t, MachineId_t> vm_to_machine;

    unordered_map<MachineId_t, vector<pair<TaskId_t, Priority_t>>> pending_tasks;


    // for round-robin (algo 4)
    unsigned rr_index;
};

#endif

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
    vector<VMId_t> vms;              // vms[i] = VM on machine i
    vector<MachineId_t> machines;
    vector<bool> machine_has_vm;     // whether machine i has a live VM
    unsigned total_machines;

    unordered_map<TaskId_t, VMId_t> task_to_vm;
    unordered_map<VMId_t, MachineId_t> vm_to_machine;

    unsigned rr_index;
};

#endif

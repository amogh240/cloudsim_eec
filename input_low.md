# Low-Intensity Workload Test
# Tests the scheduler's ability to save energy by powering down idle machines
# Long inter-arrival times, small tasks

machine class:
{
        Number of machines: 16
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [1000, 800, 600, 400]
        GPUs: yes
}
machine class:
{
        Number of machines: 16
        CPU type: ARM
        Number of cores: 8
        Memory: 8192
        S-States: [80, 60, 60, 50, 25, 5, 0]
        P-States: [6, 4, 3, 2]
        C-States: [6, 2, 1, 0]
        MIPS: [600, 450, 300, 150]
        GPUs: no
}
# Very sparse web requests
task class:
{
        Start time: 100000
        End time: 2000000
        Inter arrival: 50000
        Expected runtime: 800000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA3
        CPU type: X86
        Task type: WEB
        Seed: 444444
}
# Occasional ARM tasks
task class:
{
        Start time: 200000
        End time: 1800000
        Inter arrival: 80000
        Expected runtime: 1000000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA3
        CPU type: ARM
        Task type: WEB
        Seed: 555555
}

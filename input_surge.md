# Workload Surge Test
# Tests SLA compliance under high task arrival rates
# Short inter-arrival with many tasks to stress the scheduler

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
        Number of machines: 8
        CPU type: ARM
        Number of cores: 16
        Memory: 32768
        S-States: [100, 80, 80, 60, 30, 8, 0]
        P-States: [8, 6, 4, 3]
        C-States: [8, 2, 1, 0]
        MIPS: [800, 600, 400, 200]
        GPUs: no
}
# Burst of short web requests — very fast arrival
task class:
{
        Start time: 10000
        End time: 300000
        Inter arrival: 1500
        Expected runtime: 500000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 111111
}
# Burst of compute tasks overlapping with web burst
task class:
{
        Start time: 50000
        End time: 400000
        Inter arrival: 3000
        Expected runtime: 3000000
        Memory: 16
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA1
        CPU type: X86
        Task type: AI
        Seed: 222222
}
# ARM tasks during the surge
task class:
{
        Start time: 100000
        End time: 500000
        Inter arrival: 4000
        Expected runtime: 1500000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: ARM
        Task type: STREAM
        Seed: 333333
}

# Mixed SLA Test
# All 4 SLA levels on the same machine type
# Tests SLA-aware scheduling and priority management

machine class:
{
        Number of machines: 20
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [1000, 800, 600, 400]
        GPUs: yes
}
# SLA0 — tight deadline, must hit 95%
task class:
{
        Start time: 10000
        End time: 600000
        Inter arrival: 8000
        Expected runtime: 1500000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 610610
}
# SLA1 — moderate deadline, 90%
task class:
{
        Start time: 10000
        End time: 600000
        Inter arrival: 6000
        Expected runtime: 2000000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 620620
}
# SLA2 — relaxed deadline, 80%
task class:
{
        Start time: 10000
        End time: 600000
        Inter arrival: 5000
        Expected runtime: 3000000
        Memory: 16
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA2
        CPU type: X86
        Task type: AI
        Seed: 630630
}
# SLA3 — best effort, good for energy savings
task class:
{
        Start time: 10000
        End time: 600000
        Inter arrival: 4000
        Expected runtime: 4000000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA3
        CPU type: X86
        Task type: STREAM
        Seed: 640640
}

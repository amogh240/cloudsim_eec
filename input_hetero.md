# Heterogeneous Environment Test
# Multiple CPU types, VM types, GPU tasks
# Tests the scheduler's ability to handle diverse workloads

machine class:
{
        Number of machines: 8
        CPU type: X86
        Number of cores: 8
        Memory: 32768
        S-States: [150, 120, 120, 90, 50, 12, 0]
        P-States: [15, 10, 7, 5]
        C-States: [15, 4, 1, 0]
        MIPS: [1200, 900, 600, 300]
        GPUs: yes
}
machine class:
{
        Number of machines: 8
        CPU type: ARM
        Number of cores: 16
        Memory: 16384
        S-States: [100, 80, 80, 60, 30, 8, 0]
        P-States: [8, 6, 4, 3]
        C-States: [8, 2, 1, 0]
        MIPS: [800, 600, 400, 200]
        GPUs: no
}
machine class:
{
        Number of machines: 4
        CPU type: POWER
        Number of cores: 4
        Memory: 65536
        S-States: [200, 160, 160, 120, 60, 15, 0]
        P-States: [20, 14, 10, 6]
        C-States: [20, 5, 2, 0]
        MIPS: [1500, 1100, 750, 400]
        GPUs: yes
}
machine class:
{
        Number of machines: 4
        CPU type: RISCV
        Number of cores: 32
        Memory: 8192
        S-States: [60, 50, 50, 40, 20, 5, 0]
        P-States: [4, 3, 2, 1]
        C-States: [4, 1, 0, 0]
        MIPS: [500, 375, 250, 125]
        GPUs: no
}
# X86 GPU-enabled AI training
task class:
{
        Start time: 10000
        End time: 600000
        Inter arrival: 8000
        Expected runtime: 5000000
        Memory: 32
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA0
        CPU type: X86
        Task type: AI
        Seed: 100100
}
# ARM web requests
task class:
{
        Start time: 20000
        End time: 800000
        Inter arrival: 5000
        Expected runtime: 1000000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: ARM
        Task type: WEB
        Seed: 200200
}
# POWER HPC workloads (AIX VMs)
task class:
{
        Start time: 50000
        End time: 700000
        Inter arrival: 15000
        Expected runtime: 8000000
        Memory: 64
        VM type: AIX
        GPU enabled: yes
        SLA type: SLA2
        CPU type: POWER
        Task type: HPC
        Seed: 300300
}
# RISCV lightweight streaming
task class:
{
        Start time: 100000
        End time: 900000
        Inter arrival: 10000
        Expected runtime: 2000000
        Memory: 2
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA3
        CPU type: RISCV
        Task type: STREAM
        Seed: 400400
}
# X86 Windows crypto tasks
task class:
{
        Start time: 30000
        End time: 500000
        Inter arrival: 12000
        Expected runtime: 1500000
        Memory: 8
        VM type: WIN
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: CRYPTO
        Seed: 500500
}

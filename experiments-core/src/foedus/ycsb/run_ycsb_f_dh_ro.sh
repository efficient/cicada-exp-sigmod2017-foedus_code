# This is the "Experiment 1"
loggers_per_node=1
volatile_pool_size=16
snapshot_pool_size=1
reducer_buffer_size=1
duration_micro=10000000
thread_per_node=14
numa_nodes=16
log_buffer_mb=512
machine_name="DH"
machine_shortname="dh"
fork_workers=true
null_log_device=true
high_priority=false # To set this to true, you must add "yourname - rtprio 99" to limits.conf

. run_ycsb_f_ro_common.sh

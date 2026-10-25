struct rwtest_stats {
    int read_ops;
    int write_ops;
    int counter;
    int max_readers;
    int max_writers;
    int violations;
    int late_readers;
    int waiting_writers;
    int done_writers;
};
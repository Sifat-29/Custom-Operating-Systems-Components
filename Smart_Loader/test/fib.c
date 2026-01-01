/* Calculates 10th fib number. Uses Stack. */
int _start() {
    int n = 40;
    int t1 = 0, t2 = 1;
    int nextTerm = t1 + t2;

    for (int i = 3; i <= n; ++i) {
        t1 = t2;
        t2 = nextTerm;
        nextTerm = t1 + t2;
    }
    
    // Should return 34
    return nextTerm;
}
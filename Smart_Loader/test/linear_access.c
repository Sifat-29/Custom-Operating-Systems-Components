#define ARR_SIZE 4096 * 10 // 10 Pages worth of int (roughly)
// Note: 4096 ints is actually 4 pages (4 bytes * 1024), so 40960 ints is 40 pages.
// Let's make it explicitly large enough to force evictions.

int big_array[5000]; // 5000 * 4 bytes = ~20KB = ~5 pages

int _start() {
    // Write to array (trigger Page Faults + Allocations)
    for (int i = 0; i < 5000; i++) {
        big_array[i] = i;
    }

    // Read verification
    int sum = 0;
    for (int i = 0; i < 5000; i++) {
        sum += big_array[i];
    }
    
    return sum; // Just to use the variable
}
#define SIZE 8192 // ~8 Pages
int array[SIZE];

// Simple Pseudo-random number generator since we don't have rand()
unsigned long next = 1;
int my_rand(void) {
    next = next * 1103515245 + 12345;
    return (unsigned int)(next/65536) % 32768;
}

int _start() {
    // Initialize
    for(int i=0; i<SIZE; i++) array[i] = 1;

    int acc = 0;
    // Random Read/Write
    for(int k=0; k<10000; k++) {
        int idx = my_rand() % SIZE;
        acc += array[idx];
        array[idx] = acc;
    }

    return 0;
}
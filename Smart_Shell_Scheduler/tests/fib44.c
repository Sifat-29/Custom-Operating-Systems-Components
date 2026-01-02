// real 3.49
// user 3.33

#include<stdio.h>
#include "dummy_main.h"

int fib(int n) {
  if(n<2) return n;
  else return fib(n-1)+fib(n-2);
}

int main() {
	int val = fib(44);
  printf("value is %d\n", val);
	return 0;
}

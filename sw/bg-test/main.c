#include <stdio.h>

int sum(int end);

int x = 5;
int y;

int main() {
	printf("Hello World from core!\n");

	int a = 6;
	int b = 7;
	a = a + b + x + y;
	a = sum(a);

	volatile int aa = 0;

	return a;
}

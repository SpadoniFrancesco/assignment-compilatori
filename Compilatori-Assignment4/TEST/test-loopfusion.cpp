#include <iostream>

void test_fusion(int *A, int *B, int N) {
    // Primo ciclo (Loop 0)
    for (int i = 0; i < N; i++) {
        A[i] = 0;
    }

    // Secondo ciclo (Loop 1)
    for (int j = 0; j < N; j++) {
        B[j] = 0;
    }
}

int main() {
    int A[1000];
    int B[1000];
    test_fusion(A, B, 1000);
    return 0;
}
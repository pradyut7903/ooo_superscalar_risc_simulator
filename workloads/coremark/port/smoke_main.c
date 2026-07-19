/* Minimal freestanding C smoke — no loop, one stack temp. */
int main(void) {
    volatile int x = 42;
    return x;
}

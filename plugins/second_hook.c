#define SEGMENT_NAME nulcorepivot
#define SEGMENT_SIZE 0x1000
#define HOOK_SIZE 0x4

__attribute__((used))
static int second_hook(int x) {
    return x + 2;
}

#include <stdio.h>
#include <stdint.h>

typedef struct A{
        uint32_t a1;
        uint32_t b1;
        uint32_t c1;
        uint32_t d1;
    }A;

    typedef struct B{
        uint32_t a2;
        uint32_t b2;
        uint32_t c2;
        uint32_t d2;
    } B;
typedef struct TestStruct {
    uint8_t dir;
    union{
        A a;
        B b;
    };
} TestStruct;

int main() {
    TestStruct t;
    t.dir=0;
    t.a1 = 1;
    t.b2 = 2;
    t.c1 = 3;
    t.d1 = 4;
    printf("%i\n", t.a2);
    printf("%i\n", t.b2);
    printf("%i\n", t.c2);
    printf("%i\n", t.d2);
    return 0;
}

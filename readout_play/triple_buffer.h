#include <unistd.h>
#include <inttypes.h>

#define BUFFER_SIZE (1024*1024) // 1 MB
extern int BUFFER_SWAP_THRESHOLD;

typedef uint8_t dbyte;
typedef struct TripleBuffer {
    dbyte* buffers[3];
    int buff_idxs[3];
    int buff_lens[3];
    int read_finished;
    int write_finished;
    int state;
} TripleBuffer;

TripleBuffer rw_buffers;
int last_swap_was_dirty;
uint32_t resp;

// What I plan  for this is to assume sample are laid out contiguously
// except for a few jumps between different buffers. So kinda "piece-wise
// contiguous". The "locations" will point to the start of each contiguous
// chunk, and the lengths indicate how many samples are in each chunk

void shift_buffers();
void initialize_buffers();
int find_read_position(int *bufnum, int *idx, int *len);
void find_write_position(dbyte** position, int* len);
int register_writes(const int nwrites);
int register_reads(const int nwrites);
int swap_ready();
int pop32(uint32_t* val);

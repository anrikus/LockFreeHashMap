#include <iostream>
#include <cstdint>
#include <bitset>
#include <atomic>
#include <string>
#include <list>

using namespace std;

struct cell{
    atomic<uint32_t> seq;
    bitset<10> bits;
};

struct MRLock{
    cell* buffer;
    uint32_t mask;
    atomic<uint32_t> head;
    atomic<uint32_t> tail;
};

//input l: reference to the lock
//input size: suggested buffer size
void init(MRLock& l, uint32_t siz){
    l.buffer = new cell[siz];
    l.mask = siz - 1;
    l.head.store(0, memory_order_relaxed);
    l.tail.store(0, memory_order_relaxed);
    //initialize bits to all 1s
    for(uint32_t i =0; i<siz; i++){
        l.buffer[i].bits.set();
        l.buffer[i].seq.store(i, memory_order_relaxed);
    }
}

void uninit(MRLock& l){
    delete[] l.buffer;
}

uint32_t lock(MRLock& l, bitset<10> r){
    cell* c;
    uint32_t pos;
    for(;;){
        pos = l.tail.load(memory_order_relaxed);
        c = &l.buffer[pos & l.mask];
        uint32_t seq = c->seq.load(memory_order_relaxed);
        int32_t dif = (int32_t)seq - (int32_t)pos;
        if(dif == 0){
            if(l.tail.compare_exchange_weak(pos, pos + 1, memory_order_relaxed))
                break;
            }
        }
    c->bits = r;
    c->seq.store(pos + 1, memory_order_release);
    uint32_t spin = l.head;
    while (spin != pos){
        if (pos - l.buffer[spin & l.mask].seq > l.mask || !(l.buffer[spin & l.mask].bits & r).to_ulong())
            spin++;
    }
    return pos;
}

//input l: reference to MRLock
//input h: the lock handle
void unlock(MRLock& l, uint32_t h){
    l.buffer[h & l.mask].bits.reset();
    uint32_t pos = l.head.load(memory_order_relaxed);
    while (l.buffer[pos & l.mask].bits == 0){
        cell* c = &l.buffer[pos & l.mask];
        uint32_t seq = c->seq.load(memory_order_acquire);
        int32_t dif = (int32_t) seq - (int32_t)(pos + 1);
        if (dif == 0){
            if (l.head.compare_exchange_weak(pos, pos + 1, memory_order_relaxed)){
                c->bits.set();
                c->seq.store(pos + l.mask + 1, memory_order_release);
            }
        }
        pos = l.head.load(memory_order_relaxed);
    }
} 

//driver code
int main()
{
    //Create a MRLock object

    MRLock mrlock;

    //Initialize it with 65000 queue length

    init(mrlock, 10000);

    // Create a bitset object denoting the resources needed

    string r_bitstring = "0100000000";
    bitset<10> r_bitset(r_bitstring);

    //Acquire the lock

    uint32_t handle = lock(mrlock, r_bitset);

    /*Carry out operations*/

    //Release the lock

    unlock(mrlock, handle);

    return(0);
}

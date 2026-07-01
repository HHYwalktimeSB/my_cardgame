#ifndef MY_UTILITY_H_
#define MY_UTILITY_H_

#include<atomic>
#include<utility>

template<typename T>
class lockfree_queue{
    protected:
    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };
    Cell* buffer;
    size_t capacity;
    size_t mask;
    std::atomic<size_t> enqpos;
    std::atomic<size_t> deqpos;

    public:
    lockfree_queue(size_t size):
    capacity(size),mask(size - 1),
          enqpos(0),deqpos(0)
    {
        if ((size & (size - 1)) != 0) {
            throw std::invalid_argument("size must be power of two");
        }
        buffer = static_cast<Cell*>(operator new(sizeof(Cell)* capacity) );
        for (size_t i = 0; i < capacity; ++i) {
            new (&buffer[i].sequence) std::atomic<size_t>(i);
            //buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    ~lockfree_queue(){
        //simply assume no thread working on the quene
        for(size_t pos = deqpos.load(std::memory_order_relaxed), end = enqpos.load(std::memory_order_relaxed);
            pos < end ; ++pos)
                buffer[pos & mask].data.~T();

        operator delete(buffer);
    }
    std::optional<T> pop() {
        Cell* cell;
        size_t pos, seq;

        while (true) {
            pos = deqpos.load(std::memory_order_relaxed);
            cell = &buffer[pos & mask];
            seq = cell->sequence.load(std::memory_order_acquire);

            if (seq == pos + 1) {
                if (deqpos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))break;
            } else if (seq < pos + 1) {
                return std::nullopt; //empty quene
            }
        }

        T result = std::move(cell->data);
        cell->data.~T();
        cell->sequence.store(pos + capacity, std::memory_order_release);

        return result;
    }
    template<class... ArgT>
    bool emplace(ArgT&&... args){
        Cell* cell;
        size_t pos, seq;

        while(true){
            pos = enqpos.load(std::memory_order_relaxed);
            cell = &buffer[pos & mask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);

            if(seq == pos)
                if(enqpos.compare_exchange_weak(pos, pos + 1))break;
            else if(seq < pos){
                return false; //quene filled
            }
        }
        new (&cell->data) T(std::forward<ArgT>(args)...);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }
    size_t get_capacity()const { return capacity; }
    bool empty()const {return enqpos.load(std::memory_order_relaxed) == deqpos.load(std::memory_order_relaxed); }
    bool push(const T& value){
        return emplace(value);
    }
    bool push(T&& value){
        return emplace (std::move(value));
    }
    size_t size()const{
        return enqpos.load(std::memory_order_relaxed) - deqpos.load(std::memory_order_relaxed);
    }
};

/*
enum class HashmapInsertRes{InsertSuccess, InsertFail_NoPos, InsertFail_KeyExist};

template<typename KeyType, typename ValueType>
class lockfree_hashmap{
    protected:
    enum {sEmpty, sOccupied, sWorking, sDeleted, sReading};
    size_t capacity;
    struct Bucket{
        std::atomic<uint8_t> state;
        KeyType key;
        ValueType value;
    };
    Bucket* buckets;
    std::hash<KeyType> hasher;
    inline HashmapInsertRes find_slot_lf(const KeyType& key, size_t& _index_r){
        size_t index = hasher(key) % capacity;
        for (size_t i = 0; i < capacity; ++i) {
        Bucket& bucket = buckets[index];
        uint8_t state = bucket.state.load(std::memory_order_acquire);

        if (state == sOccupied && bucket.key == key) {
            return HashmapInsertRes::InsertFail_KeyExist;
        }
        if (state == sEmpty || state == sDeleted) {
            uint8_t expected = state;
            if (bucket.state.compare_exchange_strong(expected, sWorking, std::memory_order_acquire))
            {
                _index_r = index;
                return HashmapInsertRes::InsertSuccess;
            }
        }
        index = (index + 1) % capacity;
        }
        return HashmapInsertRes::InsertFail_NoPos;
    }

    public:
    lockfree_hashmap(size_t size):capacity(size){
        buckets = (Bucket*)operator new(sizeof(Bucket)* size);
        for(size_t i = 0; i < size; ++i)
            new (&buckets[i].state) (sEmpty);
    }

    HashmapInsertRes insert(const KeyType & key, const ValueType& value){
        size_t index;
        auto r = find_slot_lf(key, index);
        if(r == HashmapInsertRes::InsertSuccess){
            new (&buckets[index].key) KeyType(key);
            new (&buckets[index].value) ValueType(value);
            buckets[index].state.store(sOccupied, std::memory_order_release);
        }
        return r;
    }
    HashmapInsertRes insert(const KeyType & key, ValueType&& value){
        size_t index;
        auto r = find_slot_lf(key, index);
        if(r == HashmapInsertRes::InsertSuccess){
            new (&buckets[index].key) KeyType(key);
            new (&buckets[index].value) ValueType(std::move(value));
            buckets[index].state.store(sOccupied, std::memory_order_release);
        }
        return r;
    }
    
    std::optional<ValueType> find(const KeyType& key) {
    size_t index = hasher(key) % capacity;

    for (size_t i = 0; i < capacity; ++i) {
        Bucket& bucket = buckets[index];
        uint8_t state = bucket.state.load(std::memory_order_acquire);

        if (state == sEmpty) {
            return std::nullopt;
        }
        if (state == sOccupied && bucket.key == key) {
            return bucket.value;
        }
        index = (index + 1) % capacity;
    }

    return std::nullopt;
    }

    std::optional<ValueType> find_and_remove(const KeyType& key) {
    size_t index = hasher(key) % capacity;

    for (size_t i = 0; i < capacity; ++i) {
        Bucket& bucket = buckets[index];
        uint8_t state = bucket.state.load(std::memory_order_acquire);

        if (state == sEmpty) {
            return std::nullopt;
        }
        if (state == sOccupied && bucket.key == key) {
            if(bucket.state.compare_exchange_strong(state, sWorking, std::memory_order_acquire)){
                bucket.key.~KeyType();
                ValueType ret(std::move(bucket.value));
                bucket.value.~ValueType();
                bucket.state.store(sDeleted, std::memory_order_release);
                return ret;
            }
            else return std::nullopt;
        }
        index = (index + 1) % capacity;
    }

    return std::nullopt;
    }

    bool remove(const KeyType& key){
        size_t index = hasher(key) % capacity;

    for (size_t i = 0; i < capacity; ++i) {
        Bucket& bucket = buckets[index];
        uint8_t state = bucket.state.load(std::memory_order_acquire);

        if (state == sEmpty) {
            return false;
        }
        if (state == sOccupied && bucket.key == key) {
            if(bucket.state.compare_exchange_strong(state, sWorking, std::memory_order_acquire)){
                bucket.key.~KeyType();
                bucket.value.~ValueType();
                bucket.state.store(sDeleted, std::memory_order_release);
                return true;
            }
            else return false;
        }
        index = (index + 1) % capacity;
    }
    return false;
    }

    ~lockfree_hashmap(){
        //assume no thread working on hash map 
        for (size_t i = 0; i < capacity; ++i) {
            Bucket& bucket = buckets[i];
            if(bucket.state == sOccupied){
                bucket.key.~KeyType();
                bucket.value.~ValueType();
            }
        }
        operator delete(buckets);
    }
};
*/
#endif
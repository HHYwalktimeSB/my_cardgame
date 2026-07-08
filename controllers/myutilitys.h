#ifndef MY_UTILITY_H_
#define MY_UTILITY_H_

#include<atomic>
#include<utility>
#include<map>
#include<bits/shared_ptr.h>
#include<optional>

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

template<typename KeyType, typename ValueType>
class my_hashmap{
    private:
    size_t capacity;
    using bucket_ty = std::map<KeyType, ValueType>;

    class Bucket_struct{
        private:
        Bucket_struct(const Bucket_struct& o):reference_count(0),val(o.val) { }
        public:
        std::atomic<int> reference_count;
        bucket_ty val;
        Bucket_struct():reference_count(0){
        }
        ~Bucket_struct()=default;
        static Bucket_struct* allocate(){return new Bucket_struct;}
        Bucket_struct* make_copy(){
            return new Bucket_struct(*this);
        }
        class bucket_refclass{
            Bucket_struct* ref;
            bucket_refclass(Bucket_struct* _p):ref(_p){
            }
            public:
            bucket_refclass(const bucket_refclass& _p):ref(_p.ref){
                ref->reference_count.fetch_add(1);
            }
            bucket_refclass copy(){
                Bucket_struct* pstruct = ref->make_copy();
                return bucket_refclass(pstruct);
            }
            ~bucket_refclass(){
                if(ref->reference_count.fetch_sub(1) == 1)
                    delete ref;
            }
            inline void retain(){
                ref->reference_count.fetch_add(1);
            }
            const bucket_ty* operator->()const {return &(ref->val); }
            bucket_ty* operator->() {return &(ref->val); }
            friend class Bucket_struct::atomic_bucket_ref;
        };
        class atomic_bucket_ref{
            std::atomic<Bucket_struct*> ref;
            public:
            atomic_bucket_ref(Bucket_struct* _p):ref(_p){
                auto ptr = ref.load();
                ptr->reference_count.fetch_add(1);
            }
            ~atomic_bucket_ref(){
                auto ptr = ref.load();
                if(ptr->reference_count.fetch_add(1)==1)
                    delete ptr;
            }
            bool cas(bucket_refclass& old, const bucket_refclass& _new)
            {
                Bucket_struct* tmp = old.ref;
                if(ref.compare_exchange_strong(tmp, _new.ref)){
                    _new.ref->reference_count.fetch_add(1);
                    old.~bucket_refclass();//in this case we decrease reference count of old
                    return true;
                }
                else{
                    tmp->reference_count.fetch_add(1);
                    old.~bucket_refclass();
                    old.ref = tmp;
                    return false;
                }
            }
            inline const bucket_refclass load(){
                Bucket_struct& tmp = *ref.load();
                tmp.reference_count.fetch_add(1);
                return bucket_refclass(&tmp);
            }
        };
    };
    using bucket_reference = typename Bucket_struct::bucket_refclass;
    using atomic_bucket_reference = typename Bucket_struct::atomic_bucket_ref;
   atomic_bucket_reference* buckets;
    public:
    my_hashmap(size_t count_buckets){
        capacity = count_buckets;
        buckets = (atomic_bucket_reference*) operator new(sizeof(atomic_bucket_reference) * capacity);
        for(size_t i=0;i<count_buckets;++i)new(&buckets[i]) atomic_bucket_reference(Bucket_struct::allocate());
    }
    my_hashmap(const my_hashmap&)=delete;
    ~my_hashmap(){
        delete[] buckets;
    }
    std::optional<ValueType> find(const KeyType& key){
        size_t index = hasher(key) % capacity;
        bucket_reference data(buckets[index].load());
        auto it = data->find(key);
        if(it != data->end())return it->second;
        return std::nullopt;
    }

    bool insert(const KeyType & key, const ValueType& val){
        std::pair<KeyType, ValueType> elem(key,val);
        size_t index = hasher(key) % capacity;
        bucket_reference old(buckets[index].load());
        do{
            auto new_data = old.copy();
            if(!new_data->insert(elem).second)return false;
            if(buckets[index].cas(old, new_data))break;
        }while(1);
        return true;
    }

    bool remove(const KeyType& key){
        size_t index = hasher(key) % capacity;
        bucket_reference old(buckets[index].load());
        //auto new_data = old.copy();
        do{
            auto new_data = old.copy();
            auto it = new_data->find(key);
            if(it==new_data->end())return false;
            new_data->erase(it);
            if(buckets[index].cas(old, new_data))break;
        }while(1);
        return true;
    }

    std::hash<KeyType> hasher;
};

#endif
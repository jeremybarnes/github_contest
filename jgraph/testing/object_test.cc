/* object_test.cc
   Jeremy Barnes, 21 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Test for the set functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jgraph/attribute.h"
#include "jgraph/attribute_basic_types.h"
#include "utils/string_functions.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <boost/thread.hpp>
#include <boost/thread/barrier.hpp>
#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>
#include "arch/cmp_xchg.h"
#include "arch/threads.h"
#include "arch/exception_handler.h"

using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;

struct Transaction;

/// Global variable giving the number of committed transactions since the
/// beginning of the program
size_t current_epoch = 0;

/// Global variable giving the earliest epoch for which there is a snapshot
size_t earliest_epoch = 0;

/// Current transaction for this thread
__thread Transaction * current_trans = 0;


namespace JML_HASH_NS {

template<typename T>
struct hash<T *> {
    size_t operator () (const T * ptr) const
    {
        return reinterpret_cast<size_t>(ptr);
    }

};

} // namespace HASH_NS

struct RWSpinlock {
};

template<typename T>
struct Circular_Buffer {
    Circular_Buffer(int initial_capacity = 0)
        : vals_(0), start_(0), size_(0), capacity_(0)
    {
        if (initial_capacity != 0) reserve(initial_capacity);
    }

    void swap(Circular_Buffer & other)
    {
        std::swap(vals_, other.vals_);
        std::swap(start_, other.start_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

    Circular_Buffer(const Circular_Buffer & other)
        : vals_(0), start_(0), size_(0), capacity_(0)
    {
        reserve(other.size());
        for (unsigned i = 0;  i < other.size();  ++i)
            vals_[i] = other[i];
    }

    Circular_Buffer & operator = (const Circular_Buffer & other)
    {
        Circular_Buffer new_me(other);
        swap(new_me);
        return *this;
    }

    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

    ~Circular_Buffer()
    {
        destroy();
    }

    void destroy()
    {
        delete[] vals_;
        vals_ = 0;
        start_ = size_ = capacity_ = 0;
    }

    void clear()
    {
        start_ = size_ = 0;
    }

    const T & operator [](int index) const
    {
        if (size_ == 0)
            throw Exception("Circular_Buffer: empty array");
        if (index < -size_ || index >= size_)
            throw Exception("Circular_Buffer: invalid size");
        return *element_at(index);
    }

    T & operator [](int index)
    {
        const Circular_Buffer * cthis = this;
        return const_cast<T &>(cthis->operator [] (index));
    }

    void reserve(int new_capacity)
    {
        cerr << "reserve: capacity = " << capacity_ << " new_capacity = "
             << new_capacity << endl;

        if (new_capacity <= capacity_) return;
        if (capacity_ == 0)
            capacity_ = new_capacity;
        else capacity_ = std::max(capacity_ * 2, new_capacity);

        T * new_vals = new T[capacity_];
        int nfirst_half = std::min(capacity_ - start_, size_);
        std::copy(vals_ + start_, vals_ + start_ + nfirst_half,
                  new_vals);
        int nsecond_half = std::max(0, size_ - nfirst_half);
        std::copy(vals_ + start_ - nsecond_half, vals_ + start_,
                  new_vals + nfirst_half);

        delete[] vals_;

        vals_ = new_vals;
    }

    const T & front() const
    {
        if (empty())
            throw Exception("front() with empty circular array");
        return vals_[start_];
    }

    T & front()
    {
        if (empty())
            throw Exception("front() with empty circular array");
        return vals_[start_];
    }

    const T & back() const
    {
        if (empty())
            throw Exception("back() with empty circular array");
        return *element_at(size_ - 1);
    }

    T & back()
    {
        if (empty())
            throw Exception("back() with empty circular array");
        return *element_at(size_ - 1);
    }

    void push_back(const T & val)
    {
        if (size_ == capacity_) reserve(std::max(1, capacity_ * 2));
        *element_at(size_) = val;
        ++size_;
    }

    void push_front(const T & val)
    {
        if (size_ == capacity_) reserve(std::max(1, capacity_ * 2));
        *element_at(-1) = val;
        if (start_ == 0) start_ = capacity_ - 1;
        else --start_;
    }

    void pop_back()
    {
        if (empty())
            throw Exception("pop_back with empty circular array");
        --size_;
    }

    void pop_front()
    {
        if (empty())
            throw Exception("pop_front with empty circular array");
        ++start_;
        if (start_ == capacity_) start_ = 0;
    }

private:
    T * vals_;
    int start_;
    int size_;
    int capacity_;

    T * element_at(int index)
    {
        if (capacity_ == 0)
            throw Exception("empty circular buffer");

        //cerr << "element_at: index " << index << " start_ " << start_
        //     << " capacity_ " << capacity_ << " offset "
        //     << ((start_ + index) % capacity_) << endl;

        int offset = (start_ + index) % capacity_;
        if (offset < 0) offset += size_;

        return vals_ + offset;
    }
    
    const T * element_at(int index) const
    {
        if (capacity_ == 0)
            throw Exception("empty circular buffer");

        //cerr << "element_at: index " << index << " start_ " << start_
        //     << " capacity_ " << capacity_ << " offset "
        //     << ((start_ + index) % capacity_) << endl;

        int offset = (start_ + index) % capacity_;
        if (offset < 0) offset += size_;

        return vals_ + offset;
    }
};

BOOST_AUTO_TEST_CASE( circular_buffer_test )
{
    Circular_Buffer<int> buf;
    BOOST_CHECK(buf.empty());
    BOOST_CHECK_EQUAL(buf.capacity(), 0);
    {
        JML_TRACE_EXCEPTIONS(false);
        BOOST_CHECK_THROW(buf.front(), Exception);
        BOOST_CHECK_THROW(buf.back(), Exception);
        BOOST_CHECK_THROW(buf[0], Exception);
    }

    buf.push_back(1);
    BOOST_CHECK_EQUAL(buf.size(), 1);
    BOOST_CHECK(buf.capacity() >= 1);
    BOOST_CHECK_EQUAL(buf[0], 1);
    BOOST_CHECK_EQUAL(buf[-1], 1);
    BOOST_CHECK_EQUAL(buf.front(), 1);
    BOOST_CHECK_EQUAL(buf.back(), 1);
    {
        JML_TRACE_EXCEPTIONS(false);
        BOOST_CHECK_THROW(buf[-2], Exception);
        BOOST_CHECK_THROW(buf[1], Exception);
    }

    buf.push_back(2);
    BOOST_CHECK_EQUAL(buf.size(), 2);
    BOOST_CHECK(buf.capacity() >= 2);
    BOOST_CHECK_EQUAL(buf[0], 1);
    BOOST_CHECK_EQUAL(buf[1], 2);
    BOOST_CHECK_EQUAL(buf[-1], 2);
    BOOST_CHECK_EQUAL(buf[-2], 1);
    BOOST_CHECK_EQUAL(buf.front(), 1);
    BOOST_CHECK_EQUAL(buf.back(), 2);

    {
        JML_TRACE_EXCEPTIONS(false);
        BOOST_CHECK_THROW(buf[-3], Exception);
        BOOST_CHECK_THROW(buf[2], Exception);
    }

    Circular_Buffer<int> buf2;
    buf2 = buf;


}

/// Object that records the history of committed values.  Only the values
/// needed for active transactions are kept.
template<typename T>
struct History {
    History()
        : entries(0)
    {
    }

    History(const T & initial)
        : entries(1)
    {
        entries.push_back(Entry());
        entries[0].epoch = current_epoch;
        entries[0].value = initial;
    }

    History(const History & other)
    {
        Guard guard(other.lock);
        entries = other.entries;
    }

    /// Once it goes into the history, it's immutable
    const T & latest_value() const
    {
        Guard guard(lock);

        if (entries.empty())
            throw Exception("attempt to obtain value for object that never "
                            "existed");

        return entries.back().value;
    }

    /// Return the value for the given epoch
    const T & value_at_epoch(size_t epoch) const
    {
        Guard guard(lock);

        if (entries.empty())
            throw Exception("attempt to obtain value for object that never "
                            "existed");

        for (int i = entries.size() - 1;  i >= 0;  --i)
            if (entries[i].epoch <= epoch) return entries[i].value;

        throw Exception("attempt to obtain value for expired epoch");
    }

    /// Update the current value at a new epoch.  Returns true if it
    /// succeeded.  If the value has changed since the old epoch, it will
    /// not succeed.
    bool set_current_value(size_t old_epoch, size_t new_epoch,
                           const T & new_value)
    {
        Guard guard(lock);

        if (entries.empty())
            throw Exception("set_current_value with no entries");
        if (entries.front().epoch > old_epoch)
            return false;  // something updated before us
        
        entries.push_back(Entry(new_epoch, new_value));

        return true;
    }

    /// Erase the current value.  Will fail if the object was modified since
    /// the given epoch.
    void erase(size_t old_epoch)
    {
        if (entries.empty())
            throw Exception("entries was empty");

        if (entries.back().epoch != old_epoch)
            throw Exception("erasing the wrong entry");

        entries.pop_back();
    }

    /// Clean out any history entries that are associated with the given
    /// epoch and not the next epoch.  Works atomically.  Returns true if
    /// the object itself can now be reused.
    bool prune(size_t curr_epoch, size_t next_epoch);
    
    //private:
    struct Entry {
        Entry()
            : epoch(0)
        {
        }

        Entry(size_t epoch, const T & value)
            : epoch(epoch), value(value)
        {
        }

        size_t epoch;
        T value;
    };

    Circular_Buffer<Entry> entries;

    //RWSpinlock lock;
    mutable Lock lock;
};

/// This is an actual object.  Contains metadata and value history of an
/// object.
struct Object {

    // Lock the current value into memory, so that no other transaction is
    // allowed to modify it
    //virtual void lock_value() const = 0;

    virtual bool commit(size_t old_epoch, size_t new_epoch, void * data) = 0;
    virtual void rollback(size_t new_epoch, void * data) = 0;
};

/// A snapshot provides a view of all objects that is frozen at the moment
/// the shapshot was created.  Provides a read-only view.
///
/// Note that long-lived snapshots might be created (in order to take
/// hot backups or for replication).  We need to be efficient in order to
/// do so.

struct Snapshot {
    Snapshot()
        : epoch(current_epoch)
    {
    }

    size_t epoch;  ///< Epoch at which snapshot was taken
};

struct Local_Snapshot {
};

/// For the moment, only one commit can happen at a time
Lock commit_lock;

/// A sandbox provides a place where writes don't affect the underlying
/// objects.  These writes can then be committed, with 
struct Sandbox {
    struct Entry {
        Entry() : val(0), size(0)
        {
        }

        void * val;
        size_t size;
    };

    typedef hash_map<Object *, Entry> Local_Values;
    Local_Values local_values;

    template<typename T>
    T * local_value(Object * obj)
    {
        Local_Values::const_iterator it = local_values.find(obj);
        if (it == local_values.end()) return 0;
        return reinterpret_cast<T *>(it->second.val);
    }

    template<typename T>
    T * local_value(Object * obj, const T & initial_value)
    {
        bool inserted;
        Local_Values::iterator it;
        boost::tie(it, inserted)
            = local_values.insert(make_pair(obj, Entry()));
        if (inserted) {
            it->second.val = new T(initial_value);
            it->second.size = sizeof(T);
        }
        return reinterpret_cast<T *>(it->second.val);
    }

    template<typename T>
    const T * local_value(const Object * obj)
    {
        return local_value<T>(const_cast<Object *>(obj));
    }

    template<typename T>
    const T * local_value(const Object * obj, const T & initial_value)
    {
        return local_value(const_cast<Object *>(obj), initial_value);
    }

    bool commit(size_t old_epoch)
    {
        Guard guard(commit_lock);

        size_t new_epoch = current_epoch + 1;

        bool result = true;

        Local_Values::iterator
            it = local_values.begin(),
            end = local_values.end();

        // Commit everything
        for (; result && it != end;  ++it)
            result = it->first->commit(old_epoch, new_epoch, it->second.val);

        if (result) {
            // Success: we are in a new epoch
            current_epoch = new_epoch;
        }
        else {
            // Rollback if there was a problem
            end = it;
            it = local_values.begin();
            for (end = it, it = local_values.begin();  it != end;  ++it)
                it->first->rollback(new_epoch, it->second.val);
        }

        return result;
    }
};

/// A transaction is both a snapshot and a sandbox.
struct Transaction : public Snapshot, public Sandbox {

    bool commit()
    {
        return Sandbox::commit(epoch);
    }
};

struct Local_Transaction : public Transaction {
    Local_Transaction()
    {
        old_trans = current_trans;
        current_trans = this;
    }

    ~Local_Transaction()
    {
        current_trans = old_trans;
    }

    Transaction * old_trans;
};

void no_transaction_exception(const Object * obj)
{
    throw Exception("not in a transaction");
}

template<typename T>
struct Value : public Object {
    Value()
    {
    }

    Value(const T & val)
        : history(val)
    {
    }

    // Client interface.  Just two methods to get at the current value.
    T & mutate()
    {
        if (!current_trans) no_transaction_exception(this);
        T * local = current_trans->local_value<T>(this);

        if (!local) {
            T value = history.value_at_epoch(current_trans->epoch);
            local = current_trans->local_value<int>(this, value);

            if (!local)
                throw Exception("mutate(): no local was created");
        }

        return *local;
    }

    void write(const T & val)
    {
        mutate() = val;
    }
    
    const int & read() const
    {
        if (!current_trans) return history.latest_value();
        else return history.value_at_epoch(current_trans->epoch);
    }

    //private:
    // Implement object interface

    History<T> history;

    virtual bool commit(size_t old_epoch, size_t new_epoch, void * data)
    {
        return history.set_current_value(old_epoch, new_epoch,
                                         *reinterpret_cast<T *>(data));
    }

    virtual void rollback(size_t new_epoch, void * data)
    {
        history.erase(new_epoch);
    }

    // Question: should the local value storage for transactions go in here
    // as well?
    // Pros: allows a global view that can find conflicts
    // Cons: May cause lots of extra memory allocations
};


// What about list of objects?
struct List : public Object {
};

void object_test_thread(Value<int> & var, int iter, boost::barrier & barrier)
{
    // Wait for all threads to start up before we continue
    barrier.wait();

    for (unsigned i = 0;  i < iter;  ++i) {
        // Keep going until we succeed
        int old_val = var.read();

        {
            Local_Transaction trans;
            cerr << "transaction at epoch " << trans.epoch << endl;
            do {
                int & val = var.mutate();
                BOOST_CHECK_EQUAL(val % 2, 0);
                val += 1;
                BOOST_CHECK_EQUAL(val % 2, 1);
                val += 1;
                BOOST_CHECK_EQUAL(val % 2, 0);
                
                cerr << "trying commit iter " << i << " val = " << val << endl;
            } while (!trans.commit());

            BOOST_CHECK_EQUAL(var.read() % 2, 0);
        }

        BOOST_CHECK(var.read() > old_val);
    }

}

void run_object_test()
{
    Value<int> val(0);
    int niter = 10;
    int nthreads = 1;
    boost::barrier barrier(nthreads);
    boost::thread_group tg;
    for (unsigned i = 0;  i < nthreads;  ++i)
        tg.create_thread(boost::bind(&object_test_thread, boost::ref(val),
                                     niter,
                                     boost::ref(barrier)));
    
    tg.join_all();

    cerr << "current_epoch: " << current_epoch << endl;
    for (unsigned i = 0;  i < val.history.entries.size();  ++i)
        cerr << "value at epoch " << val.history.entries[i].epoch << ": "
             << val.history.entries[i].value << endl;

    BOOST_CHECK_EQUAL(val.read(), niter * nthreads * 2);
}


BOOST_AUTO_TEST_CASE( test1 )
{
    run_object_test();
}

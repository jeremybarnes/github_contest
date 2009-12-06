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
#include <set>

using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;



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
        //cerr << "reserve: capacity = " << capacity_ << " new_capacity = "
        //     << new_capacity << endl;

        if (new_capacity <= capacity_) return;
        new_capacity = std::max(capacity_ * 2, new_capacity);

        T * new_vals = new T[new_capacity];

        int nfirst_half = std::min(capacity_ - start_, size_);
        int nsecond_half = std::max(0, size_ - nfirst_half);

        //cerr << "start_ = " << start_ << " size_ = " << size_ << endl;
        //cerr << "nfirst_half = " << nfirst_half << endl;
        //cerr << "nsecond_half = " << nsecond_half << endl;

        std::copy(vals_ + start_, vals_ + start_ + nfirst_half,
                  new_vals);
        std::copy(vals_ + start_ - nsecond_half, vals_ + start_,
                  new_vals + nfirst_half);

        delete[] vals_;

        vals_ = new_vals;
        capacity_ = new_capacity;
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
        --size_;

        // Point back to the start if empty
        if (start_ == capacity_) start_ = 0;
    }

    void erase_element(int el)
    {
        cerr << "erase_element: el = " << el << " size = " << size()
             << endl;

        if (el >= size() || el < -(int)size())
            throw Exception("erase_element(): invalid value");
        if (el < 0) el += size_;

        if (capacity_ == 0)
            throw Exception("empty circular buffer");

        int offset = (start_ + el) % capacity_;

        cerr << "offset = " << offset << " start_ = " << start_
             << " size_ = " << size_ << " capacity_ = " << capacity_
             << endl;

        // TODO: could be done more efficiently

        if (el == 0) {
            pop_front();
            return;
        }
        else if (el == size() - 1) {
            pop_back();
            return;
        }
        else if (offset < start_) {
            // slide everything back
            std::copy(vals_ + offset + 1, vals_ + size_, vals_ + offset);
            --size_;
        }
        else {
            for (int i = offset;  i > 0;  --i)
                vals_[i] = vals_[i - 1];
            --size_;
            ++start_;
        }
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


struct Transaction;
struct Snapshot;
struct Object;

/// Global variable giving the number of committed transactions since the
/// beginning of the program
size_t current_epoch = 1;

/// Global variable giving the earliest epoch for which there is a snapshot
size_t earliest_epoch = 1;

/// Current transaction for this thread
__thread Transaction * current_trans = 0;


/* WHEN WE DESTROY A SNAPSHOT, we can clean up the entries upon which only
   this snapshot depends.

   How to keep track of which snapshot a history enrtry depends upon:
   - With no transaction active, there should only be one single entry in
     each history
   - Each snapshot has a list of entries that can be cleaned up after it
     dies

 */

/// Information about transactions in progress
struct Snapshot_Info {
    Lock lock;

    struct Entry {
        set<Snapshot *> snapshots;
        vector<pair<Object *, size_t> > cleanups;
    };

    typedef map<size_t, Entry> Entries;
    Entries entries;

    void register_snapshot(Snapshot * snapshot, size_t epoch);

    void remove_snapshot(Snapshot * snapshot);

    void register_cleanup(Object * obj, size_t epoch_to_cleanup);

    void dump();

} snapshot_info;


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

    size_t size() const { return entries.size(); }

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
        if (entries.back().epoch > old_epoch)
            return false;  // something updated before us
        
        entries.push_back(Entry(new_epoch, new_value));

        return true;
    }

    void cleanup_old_value(Object * obj)
    {
        Guard guard(lock);

        if (entries.size() < 2) return;  // nothing to clean up

        // Do the common case where the previous one is no longer needed
        //while (entries.front().epoch < earliest_epoch && entries.size() > 1)
        //    entries.pop_front();

        //if (entries.size() < 2) return;

        // The second last entry needs to be cleaned up by the last snapshot
        snapshot_info.register_cleanup(obj, entries[-2].epoch);
    }

    /// Erase the entry that was speculatively added
    void rollback(size_t old_epoch)
    {
        Guard guard(lock);

        if (entries.empty())
            throw Exception("entries was empty");

        if (entries.back().epoch != old_epoch)
            throw Exception("erasing the wrong entry");

        entries.pop_back();
    }

    /// Clean up the entry for an unneeded epoch
    void cleanup(size_t unneeded_epoch)
    {
        Guard guard(lock);

        for (unsigned i = 0;  i < entries.size();  ++i) {
            if (entries[i].epoch == unneeded_epoch) {
                entries.erase_element(i);
                return;
            }
        }
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

    // Get the commit ready and check that everything can go ahead, but
    // don't actually perform the commit
    virtual bool setup(size_t old_epoch, size_t new_epoch, void * data) = 0;

    // Confirm a setup commit, making it permanent
    virtual void commit(size_t new_epoch) throw () = 0;

    // Roll back a setup commit
    virtual void rollback(size_t new_epoch, void * data) throw () = 0;

    // Clean up information from an unused epoch
    virtual void cleanup(size_t unused_epoch) throw () = 0;
    
    virtual void dump(int indent = 0) const
    {
    }

    virtual std::string print_local_value(void * val) const
    {
        return format("%08p", val);
    }

};

/// A snapshot provides a view of all objects that is frozen at the moment
/// the shapshot was created.  Provides a read-only view.
///
/// Note that long-lived snapshots might be created (in order to take
/// hot backups or for replication).  We need to be efficient in order to
/// do so.

struct Snapshot {
    Snapshot()
        : epoch_(current_epoch)
    {
        snapshot_info.register_snapshot(this, epoch_);
    }

    ~Snapshot()
    {
        snapshot_info.remove_snapshot(this);
    }

    void restart()
    {
        size_t new_epoch = current_epoch;
        if (new_epoch != epoch_) {
            snapshot_info.remove_snapshot(this);
            epoch_ = new_epoch;
        }
    }

    size_t epoch() const { return epoch_; }

private:
    size_t epoch_;  ///< Epoch at which snapshot was taken
};

/* Obsolete Version Cleanups

   The goal of this code is to make sure that each version of each object
   gets cleaned up exactly once, at the point when the last snapshot that
   references the version is removed.

   One way to do this is to make sure that each version is either:
   a) the newest version of the object, or
   b) on a list of versions to clean up somewhere, or
   c) cleaned up

   Here, we describe how we maintain and shuffle these lists.

   Snapshot to Version Mapping
   ---------------------------

   Each version will have one or more snapshots that sees it (the exception is
   the newest version of an object, which may not have any snapshots that see
   it).

   versions    snapshots
   --------    ---------
        v0
                  s10
                  s15

       v20        s20
                  s30
                  s40

       v50
                  s70

       v80
                  s90
                  s600

   In this diagram, we have 4 versions of the object (v0, v20, v50 and v80)
   and 6 snapshots.  A version is visible to all snapshots that have an
   epoch >= the version number but < the next version number.  So v0 is
   visible to s10 and s15; v20 is visible to s20, s30 and s40; v40 is visible
   to s70 and v80 is visible to s90 and s600.

   We need to make sure that the version is cleaned up when the *last*
   snapshot that refers to it is destroyed.

   The way that we do this is as follows.  We assume that a later snapshot
   will live longer than an earlier one, and so we put the version to destroy
   on the list for the latest snapshot.  So we have the following lists of
   objects to clean up:

   versions    snapshots    tocleanup
   --------    ---------    ---------
        v0
                  s10
                  s15       v0

       v20        s20
                  s30
                  s40       v20

       v50
                  s70       v50

       v80
                  s90
                  s600
   
   Note that v80, as the most recent value, is not on any free list.
   When snapshot 20 is destroyed, there is nothing to clean up and so it
   simply is removed.  Same story for snapshot 30; now when snapshot 40 is
   destroyed it will clean up v20.

   However, there is no guarantee that the order of creation of the snapshots
   will be the reverse order of destruction.  Let's consider what happens
   if snapshot 40 finishes before snapshot 30 and snapshot 20.  In this case,
   it is not correct to clean up v20 as s20 and s30 still refer to it.  Instead,
   it needs to be moved to the cleanup list for s30.  We know that the version
   is still referenced because the epoch for the version (20) is less than or
   equal to the epoch for the previous snapshot (30).

   As a result, we simply move it to the cleanup list for s30.

   versions    snapshots    tocleanup      deleted
   --------    ---------    ---------      -------
        v0
                  s10
                  s15       v0

       v20        s20
                  s30       v20
                                           s40       

       v50
                  s70       v50

       v80
                  s90
                  s600
   
   Thus, the invariant is that a version will always be on the cleanup list of
   the latest snapshot that references it.
   
   When we cleanup, we look at the previous snapshot.  If the epoch of that
   snapshot is >= the epoch for our version, then we move it to the free
   list of that snapshot.  Otherwise, we clean it up.

   Finally, when we create a new version, we need to arrange for the previous
   most recent version to go onto a free list.  Consider a new version of the
   object on epoch 900:

   versions    snapshots    tocleanup      deleted
   --------    ---------    ---------      -------
        v0
                  s10
                  s15       v0

       v20        s20
                  s30       v20
                                           s40       

       v50
                  s70       v50

       v80
                  s90
                  s600      v80 <-- added
      v900
   
*/

void
Snapshot_Info::
register_snapshot(Snapshot * snapshot, size_t epoch)
{
    Guard guard(lock);
    entries[epoch].snapshots.insert(snapshot);
}

void
Snapshot_Info::
remove_snapshot(Snapshot * snapshot)
{
    Guard guard(lock);

    Entries::iterator it = entries.find(snapshot->epoch());
    if (it == entries.end())
        throw Exception("snapshot not found");

    Entry & entry = it->second;
    if (!entry.snapshots.count(snapshot))
        throw Exception("snapshots out of sync");
    
    entry.snapshots.erase(snapshot);
    
    if (entry.snapshots.empty()) {
        /* Find where the previous snapshot is; any that can't be deleted
           here will need to be moved to that list */
        Entry * prev_snapshot = 0;
        size_t prev_epoch = 0;

        if (it != entries.begin()) {
            Entries::iterator jt = it;
            --jt;

            prev_snapshot = &jt->second;
            prev_epoch = jt->first;
        }

        cerr << "prev_epoch = " << prev_epoch << endl;
        cerr << "prev_snapshot = " << prev_snapshot << endl;

        /* Check if there's another snapshot that still needs it */
        for (unsigned i = 0;  i < entry.cleanups.size();  ++i) {
            Object * obj = entry.cleanups[i].first;
            size_t epoch = entry.cleanups[i].second;

            cerr << "epoch = " << epoch << endl;

            /* Is the object needed by the previous snapshot? */
            if (prev_epoch >= epoch) // still needed by prev snapshot
                prev_snapshot->cleanups.push_back(make_pair(obj, epoch));
            else obj->cleanup(epoch);  // not needed anymore
        }
        entries.erase(it);
    }
}

void
Snapshot_Info::
register_cleanup(Object * obj, size_t epoch_to_cleanup)
{
    Guard guard(lock);

    if (entries.empty())
        throw Exception("register_cleanup with no snapshots");

    Entries::iterator it = boost::prior(entries.end());
    it->second.cleanups.push_back(make_pair(obj, epoch_to_cleanup));
}

void
Snapshot_Info::
dump()
{
    cerr << "global state: " << endl;
    cerr << "  current_epoch: " << current_epoch << endl;
    cerr << "  earliest_epoch: " << earliest_epoch << endl;
    cerr << "  current_trans: " << current_trans << endl;
    cerr << "  snapshot epochs: " << entries.size() << endl;
    int i = 0;
    for (map<size_t, Entry>::const_iterator
             it = entries.begin(), end = entries.end();
         it != end;  ++it, ++i) {
        const Entry & entry = it->second;
        cerr << "  " << i << " at epoch " << it->first << endl;
        cerr << "    " << entry.snapshots.size() << " snapshots"
             << endl;
        int j = 0;
        for (set<Snapshot *>::const_iterator
                 jt = entry.snapshots.begin(), jend = entry.snapshots.end();
             jt != jend;  ++jt, ++j)
            cerr << "      " << j << " " << *jt << " epoch "
                 << (*jt)->epoch() << endl;
        cerr << "    " << entry.cleanups.size() << " cleanups" << endl;
        for (unsigned j = 0;  j < entry.cleanups.size();  ++j)
            cerr << "      " << j << ": object " << entry.cleanups[j].first
                 << " with version " << entry.cleanups[j].second << endl;
    }
}

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
            result = it->first->setup(old_epoch, new_epoch, it->second.val);

        if (result) {
            // Success: we are in a new epoch
            for (it = local_values.begin(); it != end;  ++it)
                it->first->commit(new_epoch);
            
            current_epoch = new_epoch;
        }
        else {
            // Rollback any that were set up if there was a problem
            end = it;
            it = local_values.begin();
            for (end = it, it = local_values.begin();  it != end;  ++it)
                it->first->rollback(new_epoch, it->second.val);
        }

        // TODO: for failed transactions, we'd do better to keep the
        // structure to avoid reallocations
        local_values.clear();
        
        return result;
    }

    void dump(int indent = 0) const
    {
        string s(indent, ' ');
        cerr << "sandbox: " << local_values.size() << " local values"
             << endl;
        int i = 0;
        for (Local_Values::const_iterator
                 it = local_values.begin(), end = local_values.end();
             it != end;  ++it, ++i) {
            cerr << s << "  " << i << " at " << it->first << ": value "
                 << it->first->print_local_value(it->second.val)
                 << endl;
        }
    }
};

/// A transaction is both a snapshot and a sandbox.
struct Transaction : public Snapshot, public Sandbox {

    bool commit()
    {
        bool result = Sandbox::commit(epoch());
        if (!result) restart();
        return result;
    }

    void dump(int indent = 0)
    {
        string s(indent, ' ');
        cerr << s << "snapshot: epoch " << epoch() << endl;
        cerr << s << "sandbox" << endl;
        Sandbox::dump(indent);
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
            T value = history.value_at_epoch(current_trans->epoch());
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
    
    const T & read() const
    {
        if (!current_trans) return history.value_at_epoch(current_epoch);
        
        const T * val = current_trans->local_value<T>(this);

        if (val) return *val;
        return history.value_at_epoch(current_trans->epoch());
    }

    //private:
    // Implement object interface

    History<T> history;

    virtual bool setup(size_t old_epoch, size_t new_epoch, void * data)
    {
        return history.set_current_value(old_epoch, new_epoch,
                                         *reinterpret_cast<T *>(data));
    }

    virtual void commit(size_t new_epoch) throw ()
    {
        // Now that it's definitive, we can clean up any old values
        history.cleanup_old_value(this);
    }

    virtual void rollback(size_t new_epoch, void * data) throw ()
    {
        history.rollback(new_epoch);
    }

    virtual void cleanup(size_t unused_epoch) throw ()
    {
        history.cleanup(unused_epoch);
    }

    virtual void dump(int indent = 0) const
    {
        string s(indent, ' ');
        cerr << s << "object at " << this << " with " << history.size()
             << " values" << endl;
        for (unsigned i = 0;  i < history.size();  ++i) {
            cerr << s << "  " << i << ": epoch " << history.entries[i].epoch
                 << " value " << history.entries[i].value << endl;
        }
    }

    virtual std::string print_local_value(void * val) const
    {
        return ostream_format(*reinterpret_cast<T *>(val));
    }

    // Question: should the local value storage for transactions go in here
    // as well?
    // Pros: allows a global view that can find conflicts
    // Cons: May cause lots of extra memory allocations
};


// What about list of objects?
struct List : public Object {
};

BOOST_AUTO_TEST_CASE( test0 )
{
    // Check basic invariants
    BOOST_CHECK_EQUAL(current_trans, (Transaction *)0);
    BOOST_CHECK_EQUAL(snapshot_info.entries.size(), 0);

    size_t starting_epoch = current_epoch;

    Value<int> myval(6);

    BOOST_CHECK_EQUAL(snapshot_info.entries.size(), 0);
    BOOST_CHECK_EQUAL(myval.history.size(), 1);
    BOOST_CHECK_EQUAL(myval.read(), 6);
    
    {
        // Should throw an exception when we mutate out of a transaction
        JML_TRACE_EXCEPTIONS(false);
        BOOST_CHECK_THROW(myval.mutate(), Exception);
    }

    // Check strong exception safety
    BOOST_CHECK_EQUAL(myval.history.size(), 1);
    BOOST_CHECK_EQUAL(myval.read(), 6);
    
    // Create a transaction
    {
        Local_Transaction trans1;
        
        BOOST_CHECK_EQUAL(myval.history.size(), 1);
        BOOST_CHECK_EQUAL(myval.read(), 6);
        
        BOOST_CHECK_EQUAL(snapshot_info.entries.size(), 1);

        // Check that the correct value is copied over
        BOOST_CHECK_EQUAL(myval.mutate(), 6);

        // Check that read and mutate point to the same thing
        BOOST_CHECK_EQUAL(&myval.read(), &myval.mutate());

        // Check that we can increment it
        BOOST_CHECK_EQUAL(++myval.mutate(), 7);

        // Check that it was recorded
        BOOST_CHECK_EQUAL(trans1.local_values.size(), 1);

        // Finish the transaction without committing it
    }

    BOOST_CHECK_EQUAL(myval.history.size(), 1);
    BOOST_CHECK_EQUAL(myval.read(), 6);
    BOOST_CHECK_EQUAL(snapshot_info.entries.size(), 0);
    BOOST_CHECK_EQUAL(current_epoch, starting_epoch);
}


void object_test_thread(Value<int> & var, int iter, boost::barrier & barrier)
{
    // Wait for all threads to start up before we continue
    barrier.wait();

    int errors = 0;

    for (unsigned i = 0;  i < iter;  ++i) {
        // Keep going until we succeed
        int old_val = var.read();
        cerr << endl << "=======================" << endl;
        cerr << "i = " << i << " old_val = " << old_val << endl;

        {
            {
                cerr << "-------------" << endl << "state before trans"
                     << endl;
                snapshot_info.dump();
                var.dump();
                cerr << "-------------" << endl;

                Local_Transaction trans;

                cerr << "-------------" << endl << "state after trans"
                     << endl;
                snapshot_info.dump();
                var.dump();
                trans.dump();
                cerr << "-------------" << endl;

                int old_val2 = var.read();
                //cerr << "transaction at epoch " << trans.epoch << endl;

                cerr << "-------------" << endl << "state before read"
                     << endl;
                snapshot_info.dump();
                var.dump();
                trans.dump();
                cerr << "-------------" << endl;



                do {
                    int & val = var.mutate();

                    cerr << "old_val2 = " << old_val2 << endl;
                    cerr << "&val = " << &val << " val = " << val << endl;
                    cerr << "&var.read() = " << &var.read() << endl;
                    cerr << "&var.mutate() = " << &var.mutate() << endl;

                    cerr << "-------------" << endl << "state after read"
                         << endl;
                    snapshot_info.dump();
                    var.dump();
                    trans.dump();
                    cerr << "-------------" << endl;
                    
                    if (val % 2 != 0) {
                        cerr << "val should be even: " << val << endl;
                        ++errors;
                    }
                    
                    //BOOST_CHECK_EQUAL(val % 2, 0);
                    
                    val += 1;
                    if (val % 2 != 1) {
                        cerr << "val should be odd: " << val << endl;
                        ++errors;
                    }
                    
                    //BOOST_CHECK_EQUAL(val % 2, 1);
                    
                    val += 1;
                    if (val % 2 != 0) {
                        cerr << "val should be even 2: " << val << endl;
                        ++errors;
                    }
                    //BOOST_CHECK_EQUAL(val % 2, 0);
                    
                    //cerr << "trying commit iter " << i << " val = "
                    //     << val << endl;

                    cerr << "-------------" << endl << "state before commit"
                         << endl;
                    snapshot_info.dump();
                    var.dump();
                    trans.dump();
                    cerr << "-------------" << endl;

                } while (!trans.commit());

                cerr << "-------------" << endl << "state after commit"
                     << endl;
                snapshot_info.dump();
                var.dump();
                trans.dump();
                cerr << "-------------" << endl;

                cerr << "var.history.size() = " << var.history.entries.size()
                     << endl;
            
                if (var.read() % 2 != 0) {
                    ++errors;
                    cerr << "val should be even after trans: " << var.read()
                         << endl;
                }
            }

            cerr << "-------------" << endl << "state after trans destroyed"
                 << endl;
            snapshot_info.dump();
            var.dump();
            cerr << "-------------" << endl;
            
            if (var.read() % 2 != 0) {
                ++errors;
                cerr << "val should be even after trans: " << var.read()
                     << endl;
            }
            
            //BOOST_CHECK_EQUAL(var.read() % 2, 0);
        }

        if (var.read() <= old_val) {
            ++errors;
            cerr << "no progress made: " << old_val << " >= " << var.read()
                 << endl;
        }
        //BOOST_CHECK(var.read() > old_val);
    }

    static Lock lock;
    Guard guard(lock);

    BOOST_CHECK_EQUAL(errors, 0);
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

    cerr << "val.history.entries.size() = " << val.history.entries.size()
         << endl;

    cerr << "current_epoch = " << current_epoch << endl;

#if 0
    cerr << "current_epoch: " << current_epoch << endl;
    for (unsigned i = 0;  i < val.history.entries.size();  ++i)
        cerr << "value at epoch " << val.history.entries[i].epoch << ": "
             << val.history.entries[i].value << endl;
#endif

    BOOST_CHECK_EQUAL(val.read(), niter * nthreads * 2);
}


BOOST_AUTO_TEST_CASE( test1 )
{
    run_object_test();
}

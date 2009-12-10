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
#include "utils/vector_utils.h"
#include "utils/pair_utils.h"
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
#include "utils/circular_buffer.h"
#include "arch/timers.h"
#include "ace/Mutex.h"


using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;


struct Transaction;
struct Snapshot;

/// This is an actual object.  Contains metadata and value history of an
/// object.
struct Object {

    Object()
    {
        cleanup_list.reserve(50000);
    }

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
    virtual void cleanup(size_t unused_epoch) = 0;
    
    virtual void dump(std::ostream & stream = std::cerr, int indent = 0) const
    {
    }

    virtual void dump_unlocked(std::ostream & stream = std::cerr, int indent = 0) const
    {
    }

    virtual std::string print_local_value(void * val) const
    {
        return format("%08p", val);
    }

    vector<vector<pair<int, int> > > cleanup_list;
};

/// Global variable giving the number of committed transactions since the
/// beginning of the program
volatile size_t current_epoch_ = 1;

size_t get_current_epoch()
{
    return current_epoch_;
}

void set_current_epoch(size_t val)
{
    current_epoch_ = val;
}

/// Global variable giving the earliest epoch for which there is a snapshot
size_t earliest_epoch_ = 1;

ACE_Mutex earliest_epoch_lock;

void set_earliest_epoch(size_t val)
{
    ACE_Guard<ACE_Mutex> guard(earliest_epoch_lock);
    if (val <= earliest_epoch_) {
        cerr << "val = " << val << endl;
        cerr << "earliest_epoch = " << earliest_epoch_ << endl;
        throw Exception("earliest epoch was not increasing");
    }
    earliest_epoch_ = val;
}

size_t get_earliest_epoch()
{
    ACE_Guard<ACE_Mutex> guard(earliest_epoch_lock);
    return earliest_epoch_;
}

/// Current transaction for this thread
__thread Transaction * current_trans = 0;

/// For the moment, only one commit can happen at a time
ACE_Mutex commit_lock;


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
    mutable ACE_Mutex lock;

    struct Entry {
        set<Snapshot *> snapshots;
        vector<pair<Object *, size_t> > cleanups;
    };

    typedef map<size_t, Entry> Entries;
    Entries entries;

    // Register the snapshot for the current epoch.  Returns the number of
    // the epoch it was registered under.
    size_t register_snapshot(Snapshot * snapshot);

    void remove_snapshot(Snapshot * snapshot);

    void register_cleanup(Object * obj, size_t epoch_to_cleanup,
                          size_t new_latest_epoch);

    void dump(std::ostream & stream = std::cerr);

    void dump_unlocked(std::ostream & stream = std::cerr);

    void validate() const
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        validate_unlocked();
    }

    void validate_unlocked() const;

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
        entries.push_back(new Entry(get_current_epoch(), initial));
    }

    History(const History & other)
    {
        entries = other.entries;
    }

    ~History()
    {
        for (unsigned i = 0;  i < entries.size();  ++i)
            delete entries[i];
    }

    size_t size() const
    {
        return entries.size();
    }

    T most_recent_value(const Object * obj) const
    {
        if (entries.empty())
            throw Exception("attempt to obtain value for object that never "
                            "existed");
        return value_at_epoch(get_current_epoch(), obj);
    }

    /// Return the value for the given epoch
    const T & value_at_epoch(size_t epoch, const Object * obj) const;

    /// Update the current value at a new epoch.  Returns true if it
    /// succeeded.  If the value has changed since the old epoch, it will
    /// not succeed.
    bool set_current_value(size_t old_epoch, size_t new_epoch,
                           const T & new_value)
    {
        if (entries.empty())
            throw Exception("set_current_value with no entries");
        if (entries.back()->epoch > old_epoch)
            return false;  // something updated before us
        
        Entry * new_entry = new Entry(new_epoch, new_value);
        try {
            entries.push_back(new_entry);
        }
        catch (...) {
            delete new_entry;
            throw;
        }

        return true;
    }

    void cleanup_old_value(Object * obj)
    {
        if (entries.size() < 2) return;  // nothing to clean up

        //cerr << "entries.size() = " << entries.size() << endl;

        // Do the common case where the previous one is no longer needed
        //while (entries.front().epoch < earliest_epoch && entries.size() > 1)
        //    entries.pop_front();

        //if (entries.size() < 2) return;

        // The second last entry needs to be cleaned up by the last snapshot
        size_t epoch = entries[-2]->epoch;

        snapshot_info.register_cleanup(obj, epoch, entries[-1]->epoch);
    }

    /// Erase the entry that was speculatively added
    void rollback(size_t old_epoch)
    {
        if (entries.empty())
            throw Exception("entries was empty");

        if (entries.back()->epoch != old_epoch)
            throw Exception("erasing the wrong entry");

        delete entries.back();
        entries.pop_back();
    }

    /// Clean up the entry for an unneeded epoch
    void cleanup(size_t unneeded_epoch, const Object * obj)
    {
        if (entries.size() <= 1)
            throw Exception("cleaning up with < 2 values");

        // TODO: optimize
        for (unsigned i = 0, sz = entries.size();  i < sz;  ++i) {
            if (entries[i]->epoch == unneeded_epoch) {

                size_t my_earliest_epoch = get_earliest_epoch();
                if (i == 0 && entries[1]->epoch > my_earliest_epoch) {
                    cerr << "*** DESTROYING EARLIEST EPOCH FOR OBJECT "
                         << obj << endl;
                    cerr << "  unneeded_epoch = " << unneeded_epoch << endl;
                    cerr << "  epochs = " << obj->cleanup_list.at(unneeded_epoch) << endl;
                    cerr << "  earliest_epoch = " << my_earliest_epoch << endl;
                    cerr << "  OBJECT SHOULD BE DESTROYED AT EPOCH "
                         << my_earliest_epoch << endl;
                    snapshot_info.dump();
                    obj->dump_unlocked();
                    throw Exception("destroying earliest epoch");
                }

                //validate();
                delete entries[i];
                entries.erase_element(i);
                //validate();
                return;
            }
        }

        cerr << "----------- cleaning up didn't exist ---------" << endl;
        obj->dump_unlocked();
        cerr << "unneeded_epoch = " << unneeded_epoch << endl;
        cerr << "----------- end cleaning up didn't exist ---------" << endl;

        throw Exception("attempt to clean up something that didn't exist");
    }

    void dump(std::ostream & stream = std::cerr, int indent = 0) const
    {
        string s(indent, ' ');
        stream << s << "history with " << size()
               << " values" << endl;
        for (unsigned i = 0;  i < size();  ++i) {
            stream << s << "  " << i << ": epoch " << entries[i]->epoch;
            stream << " addr " << entries[i];
            stream << " value " << entries[i]->value;
            stream << endl;
        }
    }

private:
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

    typedef Circular_Buffer<Entry *> Entries;
    Entries entries;

    void validate() const
    {
        ssize_t e = 0;  // epoch we are up to

        for (unsigned i = 0;  i < entries.size();  ++i) {
            size_t e2 = entries[i]->epoch;
            if (e2 > get_current_epoch() + 1) {
                cerr << "e = " << e << " e2 = " << e2 << endl;
                dump();
                cerr << "invalid current epoch" << endl;
                throw Exception("invalid current epoch");
            }
            if (e2 <= e) {
                cerr << "e = " << e << " e2 = " << e2 << endl;
                dump();
                cerr << "invalid epoch order" << endl;
                throw Exception("invalid epoch order");
            }
            e = e2;
        }
    }

    template<typename TT> friend class Value;
};

/// A snapshot provides a view of all objects that is frozen at the moment
/// the shapshot was created.  Provides a read-only view.
///
/// Note that long-lived snapshots might be created (in order to take
/// hot backups or for replication).  We need to be efficient in order to
/// do so.

enum Status {
    UNINITIALIZED,
    INITIALIZED,
    RESTARTING,
    RESTARTING0,
    RESTARTING0A,
    RESTARTING0B,
    RESTARTING2,
    RESTARTED,
    COMMITTING,
    COMMITTED,
    FAILED
};

std::ostream & operator << (std::ostream & stream, const Status & status)
{
    switch (status) {
    case UNINITIALIZED: return stream << "UNINITIALIZED";
    case INITIALIZED:   return stream << "INITIALIZED";
    case RESTARTING:    return stream << "RESTARTING";
    case RESTARTING0:   return stream << "RESTARTING0";
    case RESTARTING0A:  return stream << "RESTARTING0A";
    case RESTARTING0B:  return stream << "RESTARTING0B";
    case RESTARTING2:   return stream << "RESTARTING2";
    case RESTARTED:     return stream << "RESTARTED";
    case COMMITTING:    return stream << "COMMITTING";
    case COMMITTED:     return stream << "COMMITTED";
    case FAILED:        return stream << "FAILED";
    default:            return stream << format("Status(%d)", status);
    }
}

struct Snapshot : boost::noncopyable {
    Snapshot()
        : retries_(0), status(UNINITIALIZED)
    {
        register_me();
    }

    ~Snapshot()
    {
        snapshot_info.remove_snapshot(this);
    }

    void restart()
    {
        status = RESTARTING;
        ++retries_;
        //size_t new_epoch = current_epoch;
        //if (new_epoch != epoch_) {
            snapshot_info.remove_snapshot(this);
        status = RESTARTING2;
            register_me();
        //}
    }

    void register_me()
    {
        epoch_ = snapshot_info.register_snapshot(this);
        if (status == UNINITIALIZED)
            status = INITIALIZED;
        else if (status == RESTARTING)
            status = RESTARTED;
    }

    size_t epoch() const { return epoch_; }

    int retries() const { return retries_; }

private:
    friend class Snapshot_Info;
    size_t epoch_;  ///< Epoch at which snapshot was taken
    int retries_;

public:
    Status status;
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

template<typename Key, typename Value, class Hash>
class Lightweight_Hash_Iterator
    : public boost::iterator_facade<Lightweight_Hash_Iterator<Key, Value, Hash>,
                                    std::pair<const Key, Value>,
                                    boost::bidirectional_traversal_tag> {

    typedef boost::iterator_facade<Lightweight_Hash_Iterator<Key, Value, Hash>,
                                   std::pair<const Key, Value>,
                                   boost::bidirectional_traversal_tag> Base;
public:    
    Lightweight_Hash_Iterator()
        : hash(0), index(0)
    {
    }

    Lightweight_Hash_Iterator(Hash * hash, int index)
        : hash(hash), index(index)
    {
        if (index != hash->capacity_)
            advance_to_valid();
    }

    template<typename K2, typename V2, typename H2>
    Lightweight_Hash_Iterator(const Lightweight_Hash_Iterator<K2, V2, H2> & other)
        : hash(other.hash), index(other.index)
    {
    }

private:
    // Index in hash of current entry.  It is allowed to point to any
    // valid bucket of the underlying hash, OR to one-past-the-end of the
    // capacity, which means at the end.
    Hash * hash;
    int index;

    friend class boost::iterator_core_access;
    
    template<typename K2, typename V2, typename H2>
    bool equal(const Lightweight_Hash_Iterator<K2, V2, H2> & other) const
    {
        if (hash != other.hash)
            throw Exception("comparing incompatible iterators");
        return index == other.index;
    }
    
    std::pair<const Key, Value> & dereference() const
    {
        if (!hash)
            throw Exception("dereferencing null iterator");
        if (index < 0 || index > hash->capacity_)
            throw Exception("dereferencing invalid iterator");
        if (!hash->vals_[index].first)
            throw Exception("dereferencing invalid iterator bucket");
        
        return reinterpret_cast<std::pair<const Key, Value> &>(hash->vals_[index]);
    }
    
    void increment()
    {
        if (index == hash->capacity_)
            throw Exception("increment past the end");
        ++index;
        advance_to_valid();
    }
    
    void decrement()
    {
        --index;
        backup_to_valid();
    }

    void advance_to_valid()
    {
        if (index < 0 || index >= hash->capacity_)
            throw Exception("advance_to_valid: already at end");

        // Scan through until we find a valid bucket
        while (index < hash->capacity_ && !hash->vals_[index].first)
            ++index;
    }

    void backup_to_valid()
    {
        if (index < 0 || index >= hash->capacity_)
            throw Exception("backup_to_valid: already outside range");
        
        // Scan through until we find a valid bucket
        while (index >= 0 && !hash->vals_[index].first)
            --index;
        
        if (index < 0)
            throw Exception("backup_to_valid: none found");
    }

    template<typename K2, typename V2, typename H2>
    friend class Lightweight_Hash_Iterator;
};

template<typename Key, typename Value, class Hash = std::hash<Key>,
         class Allocator = std::allocator<std::pair<Key, Value> > >
struct Lightweight_Hash {

    typedef Lightweight_Hash_Iterator<Key, const Value, const Lightweight_Hash>
    const_iterator;
    typedef Lightweight_Hash_Iterator<Key, Value, Lightweight_Hash> iterator;

    Lightweight_Hash()
        : vals_(0), size_(0), capacity_(0)
    {
    }

    template<class Iterator>
    Lightweight_Hash(Iterator first, Iterator last, size_t capacity = 0)
        : vals_(0), size_(0), capacity_(capacity)
    {
        if (capacity_ == 0)
            capacity_ = std::distance(first, last) * 2;

        if (capacity_ == 0) return;

        vals_ = allocator.allocate(capacity_);

        for (unsigned i = 0;  i < capacity_;  ++i)
            new (&vals_[i].first) Key(0);

        for (; first != last;  ++first)
            insert(*first);
    }

    Lightweight_Hash(const Lightweight_Hash & other)
        : vals_(0), size_(other.size_), capacity_(other.capacity_)
    {
        if (capacity_ == 0) return;

        vals_ = allocator.allocate(capacity_);

        // TODO: exception cleanup?

        for (unsigned i = 0;  i < capacity_;  ++i) {
            if (other.vals_[i].first)
                new (vals_ + i) std::pair<Key, Value>(other.vals_[i]);
            else new (&vals_[i].first) Key(0);
        }
    }

    ~Lightweight_Hash()
    {
        destroy();
    }

    Lightweight_Hash & operator = (const Lightweight_Hash & other)
    {
        Lightweight_Hash new_me(other);
        swap(new_me);
        return *this;
    }

    void swap(Lightweight_Hash & other)
    {
        std::swap(vals_, other.vals_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

    iterator begin()
    {
        if (empty()) return end();
        return iterator(this, 0);
    }

    iterator end()
    {
        return iterator(this, capacity_);
    }

    const_iterator begin() const
    {
        if (empty()) return end();
        return const_iterator(this, 0);
    }

    const_iterator end() const
    {
        return const_iterator(this, capacity_);
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_t capacity() const { return capacity_; }

    void clear()
    {
        // Run destructors
        for (unsigned i = 0;  i < capacity_;  ++i) {
            if (vals_[i].first) {
                try {
                    vals_[i].second.~Value();
                } catch (...) {}
                vals_[i].first = 0;
            }
        }

        size_ = 0;
    }

    void destroy()
    {
        clear();
        try {
            allocator.deallocate(vals_, capacity_);
        } catch (...) {}
        vals_ = 0;
        capacity_ = 0;
    }

    iterator find(const Key & key)
    {
        size_t hashed = Hash()(key);
        int bucket = find_bucket(hashed, key);
        if (bucket == -1) return end();
        else return iterator(this, bucket);
    }

    const_iterator find(const Key & key) const
    {
        size_t hashed = Hash()(key);
        int bucket = find_bucket(hashed, key);
        if (bucket == -1) return end();
        else return const_iterator(this, bucket);
    }

    std::pair<iterator, bool>
    insert(const std::pair<Key, Value> & val)
    {
        size_t hashed = Hash()(val.first);
        int bucket = find_bucket(hashed, val.first);
        if (bucket != -1 && vals_[bucket].first == val.first)
            return make_pair(iterator(this, bucket), false);

        if (size_ >= 3 * capacity_ / 4) {
            // expand
            reserve(std::max(4, capacity_ * 2));
            bucket = find_bucket(hashed, val.first);
            if (bucket == -1 || vals_[bucket].first == val.first)
                throw Exception("logic error: bucket appeared after reserve");
        }

        new (&vals_[bucket].second) Value(val.second);
        vals_[bucket].first = val.first;
        ++size_;

        return make_pair(iterator(this, bucket), true);
    }

    void reserve(size_t new_capacity)
    {
        if (new_capacity <= capacity_) return;

        if (new_capacity < capacity_ * 2)
            new_capacity = capacity_ * 2;

        Lightweight_Hash new_me(begin(), end(), new_capacity);
        swap(new_me);
    }

private:
    template<typename K, typename V, class H>
    friend class Lightweight_Hash_Iterator;

    std::pair<Key, Value> * vals_;
    int size_;
    int capacity_;

    int find_bucket(size_t hash, const Key & key) const
    {
        if (capacity_ == 0) return -1;
        int bucket = hash % capacity_;
        bool wrapped = false;
        int i;
        for (i = bucket;  vals_[i].first && (i != bucket || !wrapped);
             /* no inc */) {
            if (vals_[i].first == key) return i;
            ++i;
            if (i == capacity_) { i = 0;  wrapped = true; }
        }

        if (!vals_[i].first) return i;

        // No bucket found; will need to be expanded
        if (size_ != capacity_) {
            dump(cerr);
            throw Exception("find_bucket: inconsistency");
        }
        return -1;
    }

    void dump(std::ostream & stream) const
    {
        stream << "Lightweight_Hash: size " << size_ << " capacity "
               << capacity_ << endl;
        for (unsigned i = 0;  i < capacity_;  ++i) {
            stream << "  bucket " << i << ": key " << vals_[i].first;
            if (vals_[i].first)
                stream << " value " << vals_[i].second;
            stream << endl;
        }
    }

    static Allocator allocator;
};

template<typename Key, typename Value, class Hash, class Allocator>
Allocator
Lightweight_Hash<Key, Value, Hash, Allocator>::allocator;

/// A sandbox provides a place where writes don't affect the underlying
/// objects.  These writes can then be committed, with 
struct Sandbox {
    struct Entry {
        Entry() : val(0), size(0)
        {
        }

        void * val;
        size_t size;

        std::string print() const
        {
            return format("val: %p size: %zd", val, size);
        }
    };

    typedef Lightweight_Hash<Object *, Entry> Local_Values;
    //typedef std::hash_map<Object *, Entry> Local_Values;
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
        ACE_Guard<ACE_Mutex> guard(commit_lock);

        size_t new_epoch = get_current_epoch() + 1;

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
            
            // Make sure these writes are seen before we update the epoch
            __sync_synchronize();

            set_current_epoch(new_epoch);

            __sync_synchronize();
        }
        else {
            // Rollback any that were set up if there was a problem
            for (end = boost::prior(it), it = local_values.begin();
                 it != end;  ++it)
                it->first->rollback(new_epoch, it->second.val);
        }

        // TODO: for failed transactions, we'd do better to keep the
        // structure to avoid reallocations
        local_values.clear();
        
        return result;
    }

    void dump(std::ostream & stream = std::cerr, int indent = 0) const
    {
        string s(indent, ' ');
        stream << "sandbox: " << local_values.size() << " local values"
             << endl;
        int i = 0;
        for (Local_Values::const_iterator
                 it = local_values.begin(), end = local_values.end();
             it != end;  ++it, ++i) {
            stream << s << "  " << i << " at " << it->first << ": value "
                 << it->first->print_local_value(it->second.val)
                 << endl;
        }
    }
};

inline std::ostream &
operator << (std::ostream & stream,
             const Sandbox::Entry & entry)
{
    return stream << entry.print();
}

/// A transaction is both a snapshot and a sandbox.
struct Transaction : public Snapshot, public Sandbox {

    bool commit()
    {
        status = COMMITTING;
        bool result = Sandbox::commit(epoch());
        status = result ? COMMITTED : FAILED;
        if (!result) restart();
        return result;
    }

    void dump(std::ostream & stream = std::cerr, int indent = 0)
    {
        string s(indent, ' ');
        stream << s << "snapshot: epoch " << epoch() << " retries "
               << retries() << endl;
        stream << s << "sandbox" << endl;
        Sandbox::dump(stream, indent);
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

size_t
Snapshot_Info::
register_snapshot(Snapshot * snapshot)
{
    ACE_Guard<ACE_Mutex> guard(lock);
    snapshot->epoch_ = get_current_epoch();

    // NOTE: race with register cleanup: imagine that we finish our timeslice
    // exactly here.  Then something can commit a transaction, and call
    // register_cleanup.  That will run until it tries to get its lock, where
    // it will fail.


    entries[snapshot->epoch_].snapshots.insert(snapshot);
    return snapshot->epoch_;
}

void
Snapshot_Info::
remove_snapshot(Snapshot * snapshot)
{
    snapshot->status = RESTARTING0;

    ostringstream debug;

    dump(debug);  // having this here makes it much more likely

    vector<pair<Object *, size_t> > to_clean_up;
    {
        ACE_Guard<ACE_Mutex> guard(lock);

        snapshot->status = RESTARTING0A;

        dump_unlocked(debug);
        debug << "snapshot " << snapshot << ": epoch " << snapshot->epoch()
              << " retries " << snapshot->retries() << endl;

        
        Entries::iterator it = entries.find(snapshot->epoch());
        if (it == entries.end()) {
            cerr << "-------- snapshot not found -----------" << endl;
            cerr << "snapshot = " << snapshot << endl;
            cerr << "current_trans = " << current_trans << endl;
            cerr << "snapshot->epoch() = " << snapshot->epoch() << endl;
            snapshot_info.dump();
            //snapshot->dump();
            if (current_trans)
                current_trans->dump();
            cerr << "-------- end snapshot not found -----------" << endl;
            throw Exception("snapshot not found");
        }
        
        Entry & entry = it->second;
        if (!entry.snapshots.count(snapshot)) {
            cerr << "-------- snapshot out of sync -----------" << endl;
            snapshot_info.dump();
            //snapshot->dump();
            if (current_trans)
                current_trans->dump();
            cerr << "-------- end snapshot out of sync -----------" << endl;
            
            throw Exception("snapshots out of sync");
        }

    
        entry.snapshots.erase(snapshot);
    
        // TODO: try to hold the lock for less time here.  We only really need
        // the lock to add things to the previous snapshot.

        if (entry.snapshots.empty()) {
            /* Find where the previous snapshot is; any that can't be deleted
               here (due to being needed by a later snapshot) will need to be
               moved to that list */
            Entry * prev_snapshot = 0;
            size_t prev_epoch = 0;

            Entries::iterator itnext = boost::next(it);
            
            Entry * next_snapshot = 0;
            size_t next_epoch = 0;

            if (it != entries.begin()) {
                Entries::iterator jt = boost::prior(it);
                
                prev_snapshot = &jt->second;
                prev_epoch = jt->first;
            }
            else {
                // Earliest epoch has changed, as this is the earliest known
                // and it just disappeared.
                if (itnext == entries.end())
                    set_earliest_epoch(get_current_epoch());
                else set_earliest_epoch(itnext->first);
            }

            /* Find the next snapshot as well.  There is a race between
               snapshot creation and the addition to the list of snapshots;
               in this case it is possible for the cleanup to be added
               to the wrong entry.  */
            if (itnext != entries.end()) {
                next_snapshot = &itnext->second;
                next_epoch = itnext->first;
            }

            //cerr << "prev_epoch = " << prev_epoch << endl;
            //cerr << "prev_snapshot = " << prev_snapshot << endl;
            
            int num_to_cleanup = 0;

            debug << "cleaning up " << it->first << ": prev_snapshot "
                  << prev_snapshot << " prev_epoch " << prev_epoch
                  << " next_snapshot " << next_snapshot
                  << " next_epoch " << next_epoch << endl;

            for (unsigned i = 0;  i < entry.cleanups.size();  ++i) {
                Object * obj = entry.cleanups[i].first;
                size_t epoch = entry.cleanups[i].second;
                
                //cerr << "epoch = " << epoch << endl;
                
                debug << "object " << obj << " epoch " << epoch << " keep "
                      << (prev_epoch >= epoch) << endl;
                
#if 0
                /* Is the object needed by the previous snapshot?  It is if
                   the previous shapshot's epoch is greater than or equal the
                   current snapshot's epoch. */
                if (next_epoch >= epoch && next_snapshot) {
                    // should be in next snapshot's list
                    next_snapshot->cleanups.push_back(make_pair(obj, epoch));
#if 0
                    cerr << "*** WAS IN WRONG LIST ***" << endl;
                    cerr << "prev_epoch = " << prev_epoch << endl;
                    cerr << "prev_snapshot = " << prev_snapshot << endl;
                    cerr << "next_epoch = " << next_epoch << endl;
                    cerr << "next_snapshot = " << next_snapshot << endl;
                    cerr << "epoch = " << epoch << endl;
#endif
                }
                else
#endif                    
                if (prev_epoch >= epoch && prev_snapshot) {
                    // still needed by prev snapshot
                    prev_snapshot->cleanups.push_back(make_pair(obj, epoch));
                    obj->cleanup_list[epoch].push_back(make_pair(prev_epoch, get_current_epoch()));
                }
                else entry.cleanups[num_to_cleanup++] = entry.cleanups[i]; // not needed anymore
            }

            //debug << "num_to_cleanup = " << num_to_cleanup << endl;

            entry.cleanups.resize(num_to_cleanup);

            to_clean_up.swap(entry.cleanups);

            entries.erase(it);
        }
    }

    snapshot->status = RESTARTING0B;

    // Now do the actual cleanups with no lock held, to avoid deadlock (we can't
    // take the object lock with the snapshot_info lock held).
    for (unsigned i = 0;  i < to_clean_up.size();  ++i) {
        Object * obj = to_clean_up[i].first;
        size_t epoch = to_clean_up[i].second;

        //debug << "cleaning up object " << obj << " with unneeded epoch "
        //      << epoch << endl;

        try {
            obj->cleanup(epoch);
        }
        catch (...) {
            cerr << "got exception" << endl;
            cerr << debug.str();
            cerr << "object before cleanup: " << endl;
            obj->dump(cerr);
            abort();
        }
    }
}

void
Snapshot_Info::
register_cleanup(Object * obj, size_t epoch_to_cleanup,
                 size_t new_latest_epoch)
{
    // BUG: race condition
    // When we register this cleanup, we register with the last snapshot.
    // However, it is possible that there is another snapshot, with a higher
    // epoch, waiting to be registered.  Once that is created, this
    // cleanup should have been in that list, not this one.

    //global state: 
    //  current_epoch: 2800
    //  earliest_epoch: 2798
    //  current_trans: 0
    //  snapshot epochs: 2
    //  0 at epoch 2798
    //    1 snapshots
    //      0 0x7f2cf393b000 epoch 2798 COMMITTED
    //    2 cleanups
    //      0: object 0x7fff7da0a600 with version 2798
    //      1: object 0x7fff7da0a680 with version 2797
    //  1 at epoch 2799
    //    1 snapshots
    //      0 0x7f2cf313a000 epoch 2799 RESTARTING
    //    0 cleanups

    //object at 0x7fff7da0a680
    //  history with 3 values
    //    0: epoch 2797 addr 0x7f2ce4016920 value 3
    //    1: epoch 2802 addr 0x7f2cec025470 value 2
    //    2: epoch 2803 addr 0x7f2cec025370 value 2
    
    //      epochs = [ (2798,2800) ] (means transaction 2800 was committed
    //                                to put this value on the cleanup list,
    //                                and when it was the highest snapshot
    //                                on the list was 2798).


    // HERE, the cleanup for value 2797 should happen at 2799, not 2798
    // This must be because the highest snapshot in the list was 2798
    // when the cleanup was registered: either because a) snapshot 2799
    // wasn't registered yet, or b) snapshot 2799 has already terminated


    // NOTE: this is called with the object's lock held
    ACE_Guard<ACE_Mutex> guard(lock);

    if (entries.empty())
        throw Exception("register_cleanup with no snapshots");

    Entries::iterator it = boost::prior(entries.end());
    if (obj->cleanup_list.size() <= epoch_to_cleanup)
        obj->cleanup_list.resize(epoch_to_cleanup + 1);

    it->second.cleanups.push_back(make_pair(obj, epoch_to_cleanup));
    if (obj->cleanup_list[epoch_to_cleanup].size()) {
        cerr << "epoch_to_cleanup = " << epoch_to_cleanup << endl;
        cerr << "cleanup in " << it->first << endl;
        cerr << "cleanup list = " << obj->cleanup_list[epoch_to_cleanup]
             << endl;
        throw Exception("already had a cleanup");
    }
    obj->cleanup_list[epoch_to_cleanup].reserve(4);
    obj->cleanup_list[epoch_to_cleanup].push_back(make_pair(it->first, new_latest_epoch));
}

void
Snapshot_Info::
dump_unlocked(std::ostream & stream)
{

    stream << "global state: " << endl;
    stream << "  current_epoch: " << get_current_epoch() << endl;
    stream << "  earliest_epoch: " << get_earliest_epoch() << endl;
    stream << "  current_trans: " << current_trans << endl;
    stream << "  snapshot epochs: " << entries.size() << endl;
    int i = 0;
    for (map<size_t, Entry>::const_iterator
             it = entries.begin(), end = entries.end();
         it != end;  ++it, ++i) {
        const Entry & entry = it->second;
        stream << "  " << i << " at epoch " << it->first << endl;
        stream << "    " << entry.snapshots.size() << " snapshots"
             << endl;
        int j = 0;
        for (set<Snapshot *>::const_iterator
                 jt = entry.snapshots.begin(), jend = entry.snapshots.end();
             jt != jend;  ++jt, ++j)
            stream << "      " << j << " " << *jt << " epoch "
                   << (*jt)->epoch() << " status " << (*jt)->status
                   << endl;
        stream << "    " << entry.cleanups.size() << " cleanups" << endl;
        for (unsigned j = 0;  j < entry.cleanups.size();  ++j)
            stream << "      " << j << ": object " << entry.cleanups[j].first
                 << " with version " << entry.cleanups[j].second << endl;
    }
}

void
Snapshot_Info::
dump(std::ostream & stream)
{
    ACE_Guard<ACE_Mutex> guard (lock);
    dump_unlocked(stream);
}

void
Snapshot_Info::
validate_unlocked() const
{
    
}

void no_transaction_exception(const Object * obj)
{
    throw Exception("not in a transaction");
}

template<typename T>
const T &
History<T>::
value_at_epoch(size_t epoch, const Object * obj) const
{
    if (entries.empty())
        throw Exception("attempt to obtain value for object that never "
                        "existed");
    
    for (int i = entries.size() - 1;  i >= 0;  --i)
        if (entries[i]->epoch <= epoch) return entries[i]->value;
        
    cerr << "--------------- expired epoch -------------" << endl;
    cerr << "current_epoch = " << get_current_epoch() << endl;
    cerr << "epoch = " << epoch << endl;
    dump();
    snapshot_info.dump();
    if (current_trans) current_trans->dump();
    obj->dump_unlocked();
    cerr << "--------------- end expired epoch" << endl;
    
    abort();
    
    
    throw Exception("attempt to obtain value for expired epoch");
}

template<typename T>
struct Value : public Object {
    Value()
        : history(T())
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
            T value;
            {
                ACE_Guard<ACE_Mutex> guard(lock);
                //history.validate();
                value = history.value_at_epoch(current_trans->epoch(), this);
            }
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
    
    const T read() const
    {
        if (!current_trans) {
            ACE_Guard<ACE_Mutex> guard(lock);
            //history.validate();
            return history.most_recent_value(this);
        }
        
        const T * val = current_trans->local_value<T>(this);
        
        if (val) return *val;
     
        ACE_Guard<ACE_Mutex> guard(lock);
        return history.value_at_epoch(current_trans->epoch(), this);
    }

    //private:
    // Implement object interface

    History<T> history;
    mutable ACE_Mutex lock;

    std::string last_cleanup;  // debug

    virtual bool setup(size_t old_epoch, size_t new_epoch, void * data)
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        //history.validate();
        bool result = history.set_current_value(old_epoch, new_epoch,
                                                *reinterpret_cast<T *>(data));
        //cerr << "result = " << result << endl;
        //history.validate();
        return result;
    }

    virtual void commit(size_t new_epoch) throw ()
    {
        // Now that it's definitive, we can clean up any old values
        ACE_Guard<ACE_Mutex> guard(lock);
        //history.validate();
        history.cleanup_old_value(this);
        //history.validate();
    }

    virtual void rollback(size_t new_epoch, void * data) throw ()
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        //history.validate();
        //cerr << endl << "rollback: current_epoch = " << current_epoch
        //     << " new_epoch = " << new_epoch << endl;
        //cerr << "before rollback: " << endl;
        //dump_itl(cerr);
        history.rollback(new_epoch);
        //cerr << "after rollback: " << endl;
        //dump_itl(cerr);
        //history.validate();
    }

    virtual void cleanup(size_t unused_epoch)
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        std::ostringstream stream;
        stream << "cleaning up epoch " << unused_epoch << " for object "
               << this << " current_epoch = " << get_current_epoch() << endl;
        snapshot_info.dump(stream);
        stream << "before: " << endl;
        dump_itl(stream, 4, false);
        history.cleanup(unused_epoch, this);
        stream << "after: " << endl;
        dump_itl(stream, 4, false);
        stream << "current_epoch = " << get_current_epoch() << endl;
        last_cleanup = stream.str();
    }

    virtual void dump(std::ostream & stream = std::cerr, int indent = 0) const
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        dump_itl(stream, indent);
    }

    virtual void dump_unlocked(std::ostream & stream = std::cerr,
                               int indent = 0) const
    {
        dump_itl(stream, indent);
    }

    void dump_itl(std::ostream & stream, int indent = 0, bool lc = false) const
    {
        string s(indent, ' ');
        stream << s << "object at " << this << endl;
        history.dump(stream, indent + 2);
        if (lc) {
            stream << "last cleanup:" << endl;
            stream << last_cleanup << endl;
        }
    }

    virtual std::string print_local_value(void * val) const
    {
        return ostream_format(*reinterpret_cast<T *>(val));
    }
};


// What about list of objects?
struct List : public Object {
};

BOOST_AUTO_TEST_CASE( test0 )
{
    // Check basic invariants
    BOOST_CHECK_EQUAL(current_trans, (Transaction *)0);
    BOOST_CHECK_EQUAL(snapshot_info.entries.size(), 0);

    size_t starting_epoch = get_current_epoch();

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
        
        // Check that the snapshot is properly there
        BOOST_REQUIRE_EQUAL(snapshot_info.entries.size(), 1);
        BOOST_CHECK_EQUAL(snapshot_info.entries.begin()->first, get_current_epoch());
        BOOST_REQUIRE_EQUAL(snapshot_info.entries.begin()->second.snapshots.size(), 1);
        BOOST_CHECK_EQUAL(*snapshot_info.entries.begin()->second.snapshots.begin(), &trans1);
        
        // Check that the correct value is copied over
        BOOST_CHECK_EQUAL(myval.mutate(), 6);

        // Check that we can increment it
        BOOST_CHECK_EQUAL(++myval.mutate(), 7);

        // Check that it was recorded
        BOOST_CHECK_EQUAL(trans1.local_values.size(), 1);

        // FOR TESTING, increment the current epoch
        set_current_epoch(get_current_epoch() + 1);

        // Restart the transaction; check that it was properly recorded by the
        // snapshot info
        trans1.restart();

        // Check that the snapshot is properly there
        BOOST_REQUIRE_EQUAL(snapshot_info.entries.size(), 1);
        BOOST_CHECK_EQUAL(snapshot_info.entries.begin()->first, get_current_epoch());
        BOOST_REQUIRE_EQUAL(snapshot_info.entries.begin()->second.snapshots.size(), 1);
        BOOST_CHECK_EQUAL(*snapshot_info.entries.begin()->second.snapshots.begin(), &trans1);

        // Finish the transaction without committing it
    }

    BOOST_CHECK_EQUAL(myval.history.size(), 1);
    BOOST_CHECK_EQUAL(myval.read(), 6);
    BOOST_CHECK_EQUAL(snapshot_info.entries.size(), 0);
    BOOST_CHECK_EQUAL(get_current_epoch(), starting_epoch + 1);

    set_current_epoch(1);
}


void object_test_thread(Value<int> & var, int iter, boost::barrier & barrier,
                        size_t & failures)
{
    // Wait for all threads to start up before we continue
    barrier.wait();

    int errors = 0;
    int local_failures = 0;

    for (unsigned i = 0;  i < iter;  ++i) {
        //static Lock lock;
        //Guard guard(lock);

        // Keep going until we succeed
        int old_val = var.read();
#if 0
        cerr << endl << "=======================" << endl;
        cerr << "i = " << i << " old_val = " << old_val << endl;
#endif
        {
            {
#if 0
                cerr << "-------------" << endl << "state before trans"
                     << endl;
                snapshot_info.dump();
                var.dump();
                cerr << "-------------" << endl;
#endif

                Local_Transaction trans;

#if 0
                cerr << "-------------" << endl << "state after trans"
                     << endl;
                snapshot_info.dump();
                var.dump();
                trans.dump();
                cerr << "-------------" << endl;
                int old_val2 = var.read();
#endif

                //cerr << "transaction at epoch " << trans.epoch << endl;

#if 0
                cerr << "-------------" << endl << "state before read"
                     << endl;
                snapshot_info.dump();
                var.dump();
                trans.dump();
                cerr << "-------------" << endl;
#endif


                int tries = 0;
                do {
                    ++tries;
                    int & val = var.mutate();
                    

#if 0
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
#endif
                    
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

#if 0
                    cerr << "-------------" << endl << "state before commit"
                         << endl;
                    snapshot_info.dump();
                    var.dump();
                    trans.dump();
                    cerr << "-------------" << endl;
#endif

                } while (!trans.commit());

                local_failures += tries - 1;

#if 0
                cerr << "-------------" << endl << "state after commit"
                     << endl;
                snapshot_info.dump();
                var.dump();
                trans.dump();
                cerr << "-------------" << endl;
#endif

                //cerr << "var.history.size() = " << var.history.entries.size()
                //     << endl;
            
                if (var.read() % 2 != 0) {
                    ++errors;
                    cerr << "val should be even after trans: " << var.read()
                         << endl;
                }
            }

#if 0
            cerr << "-------------" << endl << "state after trans destroyed"
                 << endl;
            snapshot_info.dump();
            var.dump();
            cerr << "-------------" << endl;
#endif
            
            if (var.read() % 2 != 0) {
                ++errors;
                cerr << "val should be even after trans: " << var.read()
                     << endl;
            }
            
            //BOOST_CHECK_EQUAL(var.read() % 2, 0);
        }

        int new_val = var.read();
        if (new_val <= old_val) {
            ++errors;
            cerr << "no progress made: " << new_val << " <= " << old_val
                 << endl;
        }
        //BOOST_CHECK(var.read() > old_val);
    }

    static Lock lock;
    Guard guard(lock);

    BOOST_CHECK_EQUAL(errors, 0);

    failures += local_failures;
}

void run_object_test(int nthreads, int niter)
{
    cerr << "testing with " << nthreads << " threads and " << niter << " iter"
         << endl;
    Value<int> val(0);
    boost::barrier barrier(nthreads);
    boost::thread_group tg;

    size_t failures = 0;

    Timer timer;
    for (unsigned i = 0;  i < nthreads;  ++i)
        tg.create_thread(boost::bind(&object_test_thread, boost::ref(val),
                                     niter,
                                     boost::ref(barrier),
                                     boost::ref(failures)));
    
    tg.join_all();

    cerr << "elapsed: " << timer.elapsed() << endl;

    cerr << "val.history.entries.size() = " << val.history.size()
         << endl;

    cerr << "current_epoch = " << get_current_epoch() << endl;
    cerr << "failures: " << failures << endl;

#if 0
    cerr << "current_epoch: " << current_epoch << endl;
    for (unsigned i = 0;  i < val.history.entries.size();  ++i)
        cerr << "value at epoch " << val.history.entries[i].epoch << ": "
             << val.history.entries[i].value << endl;
#endif

    BOOST_CHECK_EQUAL(val.history.size(), 1);
    BOOST_CHECK_EQUAL(val.read(), niter * nthreads * 2);
}


BOOST_AUTO_TEST_CASE( test1 )
{
    //run_object_test(1, 100000);
    //run_object_test(10, 10000);
    //run_object_test(100, 1000);
    //run_object_test(1000, 100);
}

struct Object_Test_Thread2 {
    Value<int> * vars;
    int nvars;
    int iter;
    boost::barrier & barrier;
    size_t & failures;

    Object_Test_Thread2(Value<int> * vars,
                        int nvars,
                        int iter, boost::barrier & barrier,
                        size_t & failures)
        : vars(vars), nvars(nvars), iter(iter), barrier(barrier),
          failures(failures)
    {
    }

    void operator () ()
    {
        // Wait for all threads to start up before we continue
        barrier.wait();
        
        int errors = 0;
        int local_failures = 0;
        
        for (unsigned i = 0;  i < iter;  ++i) {
            // Keep going until we succeed
            int var1 = random() % nvars, var2 = random() % nvars;
            
            {
                Local_Transaction trans;
                
                // Now that we're inside, the total should be zero
                ssize_t total = 0;
                for (unsigned i = 0;  i < nvars;  ++i)
                    total += vars[i].read();
                if (total != 0) {
                    cerr << "total is " << total << endl;
                    ++errors;
                }
                
                int tries = 0;
                do {
                    ++tries;
                    int & val1 = vars[var1].mutate();
                    int & val2 = vars[var2].mutate();
                    
                    val1 -= 1;
                    val2 += 1;

                    if (tries == 1000) {
                        static Lock lock;
                        Guard guard(lock);
                        cerr << "-----------------------" << endl;
                        cerr << "thread " << &current_trans << endl;
                        cerr << "1000 failures" << endl;
                        cerr << "var1 = " << var1 << endl;
                        cerr << "var2 = " << var2 << endl;
                        cerr << "---- snapshot" << endl;
                        snapshot_info.dump();
                        cerr << "---- trans" << endl;
                        trans.dump();
                        cerr << "---- var1" << endl;
                        vars[var1].dump();
                        cerr << "---- var2" << endl;
                        vars[var2].dump();
                        
                        sleep(1);
                        abort();
                    }

                } while (!trans.commit());
                
                local_failures += tries - 1;
            }
        }

        static Lock lock;
        Guard guard(lock);
        
        BOOST_CHECK_EQUAL(errors, 0);
        
        failures += local_failures;
    }
};

void run_object_test2(int nthreads, int niter, int nvals)
{
    cerr << "testing with " << nthreads << " threads and " << niter << " iter"
         << endl;
    Value<int> vals[nvals];
    boost::barrier barrier(nthreads);
    boost::thread_group tg;

    size_t failures = 0;

    Timer timer;
    for (unsigned i = 0;  i < nthreads;  ++i)
        tg.create_thread(Object_Test_Thread2(vals, nvals, niter, barrier,
                                             failures));
    
    tg.join_all();

    cerr << "elapsed: " << timer.elapsed() << endl;

    ssize_t total = 0;
    for (unsigned i = 0;  i < nvals;  ++i)
        total += vals[i].read();

    BOOST_CHECK_EQUAL(total, 0);
    for (unsigned i = 0;  i < nvals;  ++i)
        BOOST_CHECK_EQUAL(vals[i].history.size(), 1);
}


BOOST_AUTO_TEST_CASE( test2 )
{
    cerr << endl << endl << "========= test 2: multiple variables" << endl;
    
    //run_object_test2(1, 100000, 10);
    run_object_test2(2,  50000, 2);
    //run_object_test2(10, 10000, 10);
    //run_object_test2(100, 1000, 10);
    //run_object_test2(1000, 100, 10);
}



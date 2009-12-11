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
#include "utils/lightweight_hash.h"
#include "arch/timers.h"
#include "ace/Mutex.h"
#include "arch/backtrace.h"


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
    virtual void cleanup(size_t unused_epoch, size_t trigger_epoch) = 0;
    
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

void set_earliest_epoch(size_t val)
{
    if (val < earliest_epoch_) {
        cerr << "val = " << val << endl;
        cerr << "earliest_epoch = " << earliest_epoch_ << endl;
        throw Exception("earliest epoch was not increasing");
    }
    earliest_epoch_ = val;
}

size_t get_earliest_epoch()
{
    return earliest_epoch_;
}

/// Current transaction for this thread
__thread Transaction * current_trans = 0;

size_t current_trans_epoch();

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

    void register_cleanup(Object * obj, size_t epoch_to_cleanup);

    void
    perform_cleanup(Entries::iterator it, ACE_Guard<ACE_Mutex> & guard);

    void dump(std::ostream & stream = std::cerr);

    void dump_unlocked(std::ostream & stream = std::cerr);

    void validate() const
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        validate_unlocked();
    }

    void validate_unlocked() const;

} snapshot_info;


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

        // Do the common case where the first entry is no longer needed
        // NOTE: dicey; needs to be properly analysed
        //while (entries.size() >= 2
        //       && entries[1]->epoch < get_earliest_epoch())
        //    entries.pop_front();
        //if (entries.size() < 2) return;

        // The second last entry needs to be cleaned up by the last snapshot
        size_t epoch = entries[-2]->epoch;

        snapshot_info.register_cleanup(obj, epoch);
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
    void cleanup(size_t unneeded_epoch, const Object * obj, size_t trigger_epoch)
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
                    backtrace();
                    cerr << "  unneeded_epoch = " << unneeded_epoch << endl;
                    cerr << "  trigger_epoch = " << trigger_epoch << endl;
                    cerr << "  earliest_epoch = " << my_earliest_epoch << endl;
                    cerr << "  OBJECT SHOULD BE DESTROYED AT EPOCH "
                         << my_earliest_epoch << endl;
                    cerr << "  current_trans = " << current_trans << endl;
                    cerr << "  current_trans epoch " << current_trans_epoch() << endl;
                    snapshot_info.dump();
                    obj->dump_unlocked();
                    //throw Exception("destroying earliest epoch");
                }

                //validate();
                delete entries[i];
                entries.erase_element(i);
                //validate();

                if (i == 0 && entries[0]->epoch > my_earliest_epoch)
                    throw Exception("destroying earliest epoch");

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
        if (get_current_epoch() != epoch_) {
            snapshot_info.remove_snapshot(this);
            register_me();
        }
    }

    void register_me()
    {
        snapshot_info.register_snapshot(this);

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

    ~Sandbox()
    {
        clear();
    }

    void clear()
    {
        for (Local_Values::iterator
                 it = local_values.begin(),
                 end = local_values.end();
             it != end;  ++it) {
            free(it->second.val);
        }
        local_values.clear();
    }

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
            it->second.val = malloc(sizeof(T));
            new (it->second.val) T(initial_value);
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
            // First we update the epoch.  This ensures that any new snapshot
            // created will see the correct epoch value, and won't look at
            // old values which might not have a list.
            //
            // IT IS REALLY IMPORTANT THAT THIS BE DONE IN THE GIVEN ORDER.
            // If we were to update the epoch afterwards, then new transactions
            // could be created with the old epoch.  These transactions might
            // need the values being cleaned up, racing with the creation
            // process.
            set_current_epoch(new_epoch);

            // Make sure these writes are seen before we clean up
            __sync_synchronize();

            // Success: we are in a new epoch
            for (it = local_values.begin(); it != end;  ++it)
                it->first->commit(new_epoch);
        }
        else {
            // Rollback any that were set up if there was a problem
            for (end = boost::prior(it), it = local_values.begin();
                 it != end;  ++it)
                it->first->rollback(new_epoch, it->second.val);
        }

        // TODO: for failed transactions, we'd do better to keep the
        // structure to avoid reallocations
        // TODO: clear as we go to better use cache
        clear();
        
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

    // TODO: since we know it will be inserted at the end, we can do a more
    // efficient lookup that only looks at the end.

    Entries::iterator previous_most_recent;
    if (entries.empty()) previous_most_recent = entries.end();
    else previous_most_recent = boost::prior(entries.end());

    entries[snapshot->epoch_].snapshots.insert(snapshot);

    /* INVARIANT: a registered snapshot should always go at the end of the
       list of snapshots; it is new and should therefore always be the last
       one.  We check it here. */
    Entries::iterator it = entries.find(snapshot->epoch_);
    if (it == entries.end())
        throw Exception("inserted but not found");
    if (it != boost::prior(entries.end())) {
        cerr << "stale snapshot" << endl;
        dump_unlocked();
        cerr << "snapshot->epoch_ = " << snapshot->epoch_ << endl;
        throw Exception("inserted stale snapshot");
    }

    /* Since we don't clean up anything based upon the most recent snapshot,
       we now need to look at what was the most recent snapshot and see if
       it needs to be cleaned up. */
    if (previous_most_recent != it && previous_most_recent != entries.end()) {
        /* Do we need to clean it up? */
        // NOTE: calling this function RELEASES the lock; we can't tough
        // entries after.
        if (previous_most_recent->second.snapshots.empty())
            perform_cleanup(previous_most_recent, guard);
    }

    return snapshot->epoch_;
}

ACE_Mutex expired_lock;

void
Snapshot_Info::
remove_snapshot(Snapshot * snapshot)
{
    snapshot->status = RESTARTING0;

    ACE_Guard<ACE_Mutex> guard(lock);

    if (entries.empty())
        throw Exception("remove_snapshot: empty entries");
    
    snapshot->status = RESTARTING0A;
    
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
    
    /* Is this the most recent snapshot?  If so, we can't clean up,
       even if we're removing the last entry, as there might be a
       new snapshot created with the same epoch. */
    //bool most_recent = (it == boost::prior(entries.end()));
    
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
    
    // NOTE: this must be last in the function; it causes the guard to be
    // released
    if (entry.snapshots.empty() /* && !most_recent*/)
        perform_cleanup(it, guard);
}

void
Snapshot_Info::
perform_cleanup(Entries::iterator it, ACE_Guard<ACE_Mutex> & guard)
{
    // TODO: try to hold the lock for less time here.  We only really need
    // the lock to add things to the previous snapshot.
    
    if (!it->second.snapshots.empty())
        throw Exception("perform_cleanup with snapshots");
    //if (it == boost::prior(entries.end()) && it->first == get_current_epoch())
    //    throw Exception("cleaning up most recent entry");
    if (it == entries.end())
        throw Exception("cleaning up invalid entry");

    /* Find where the previous snapshot is; any that can't be deleted
       here (due to being needed by a later snapshot) will need to be
       moved to that list */
    Entry * prev_snapshot = 0;
    size_t prev_epoch = 0;
    
    Entries::iterator itnext = boost::next(it);

    if (it != entries.begin()) {
        Entries::iterator jt = boost::prior(it);
        
        prev_snapshot = &jt->second;
        prev_epoch = jt->first;
    }
    else {
        // Earliest epoch has changed, as this is the earliest known
        // and it just disappeared.
        try {
            if (itnext == entries.end())
                set_earliest_epoch(get_current_epoch());
            else set_earliest_epoch(itnext->first);
        } catch (const std::exception & exc) {
            cerr << "exception setting earliest epoch" << endl;
            dump_unlocked();
            cerr << "itnext == entries.end() = "
                 << (itnext == entries.end()) << endl;
            if (itnext != entries.end())
                cerr << "itnext->first = " << itnext->first
                     << endl;
            throw;
        }
    }
    
    //cerr << "prev_epoch = " << prev_epoch << endl;
    //cerr << "prev_snapshot = " << prev_snapshot << endl;
    
    int num_to_cleanup = 0;
    
    Entry & entry = it->second;

    // List of things to clean up once we release the guard
    vector<pair<Object *, size_t> > to_clean_up;
    
    for (unsigned i = 0;  i < entry.cleanups.size();  ++i) {
        Object * obj = entry.cleanups[i].first;
        size_t epoch = entry.cleanups[i].second;
        
        //cerr << "epoch = " << epoch << endl;
        
        if (prev_epoch >= epoch && prev_snapshot) {
            // still needed by prev snapshot
            prev_snapshot->cleanups.push_back(make_pair(obj, epoch));
        }
        else entry.cleanups[num_to_cleanup++] = entry.cleanups[i]; // not needed anymore
    }
    
    //debug << "num_to_cleanup = " << num_to_cleanup << endl;
    
    entry.cleanups.resize(num_to_cleanup);
    
    to_clean_up.swap(entry.cleanups);

    size_t snapshot_epoch = it->first;

    entries.erase(it);

    // Release the guard so that we can lock the objects
    guard.release();
    
    // Now do the actual cleanups with no lock held, to avoid deadlock (we can't
    // take the object lock with the snapshot_info lock held).
    for (unsigned i = 0;  i < to_clean_up.size();  ++i) {
        Object * obj = to_clean_up[i].first;
        size_t epoch = to_clean_up[i].second;
        
        //debug << "cleaning up object " << obj << " with unneeded epoch "
        //      << epoch << endl;
        
        //ostringstream obj_stream_before;
        //obj->dump(obj_stream_before);
        
        try {
            obj->cleanup(epoch, snapshot_epoch);
        }
        catch (const std::exception & exc) {
            ostringstream obj_stream;
            obj->dump(obj_stream);
            ACE_Guard<ACE_Mutex> lock(expired_lock);
            cerr << "got exception: " << exc.what() << endl;
            //cerr << debug.str();
            //cerr << "object before cleanup: " << endl;
            //cerr << obj_stream_before.str();
            cerr << "object after cleanup: " << endl;
            cerr << obj_stream.str();
            //abort();  // let execution continus; see if this causes an error
        }
    }
}

void
Snapshot_Info::
register_cleanup(Object * obj, size_t epoch_to_cleanup)
{
    // NOTE: this is called with the object's lock held
    ACE_Guard<ACE_Mutex> guard(lock);

    if (entries.empty())
        throw Exception("register_cleanup with no snapshots");

    Entries::iterator it = boost::prior(entries.end());
    it->second.cleanups.push_back(make_pair(obj, epoch_to_cleanup));
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

size_t current_trans_epoch()
{
    return (current_trans ? current_trans->epoch() : 0);
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
    
    {
        ACE_Guard<ACE_Mutex> lock(expired_lock);
        
        cerr << "--------------- expired epoch -------------" << endl;
        cerr << "obj = " << obj << endl;
        cerr << "current_epoch = " << get_current_epoch() << endl;
        cerr << "earliest_epoch = " << get_earliest_epoch() << endl;
        cerr << "epoch = " << epoch << endl;
        dump();
        snapshot_info.dump();
        if (current_trans) current_trans->dump();
        obj->dump_unlocked();
        cerr << "--------------- end expired epoch" << endl;
    }    
    sleep(1);
    

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

    virtual bool setup(size_t old_epoch, size_t new_epoch, void * data)
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        bool result = history.set_current_value(old_epoch, new_epoch,
                                                *reinterpret_cast<T *>(data));
        return result;
    }

    virtual void commit(size_t new_epoch) throw ()
    {
        // Now that it's definitive, we can clean up any old values
        ACE_Guard<ACE_Mutex> guard(lock);
        history.cleanup_old_value(this);
    }

    virtual void rollback(size_t new_epoch, void * data) throw ()
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        history.rollback(new_epoch);
    }

    virtual void cleanup(size_t unused_epoch, size_t trigger_epoch)
    {
        ACE_Guard<ACE_Mutex> guard(lock);
        history.cleanup(unused_epoch, this, trigger_epoch);
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

    void dump_itl(std::ostream & stream, int indent = 0) const
    {
        string s(indent, ' ');
        stream << s << "object at " << this << endl;
        history.dump(stream, indent + 2);
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

    cerr << "------------------ at start" << endl;
    snapshot_info.dump();
    cerr << "------------------ end at start" << endl;
    
    // Create a transaction
    {
        Local_Transaction trans1;
        
        cerr << "&trans1 = " << &trans1 << endl;

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

    cerr << "------------------ at end" << endl;
    snapshot_info.dump();
    cerr << "------------------ end at end" << endl;

    BOOST_CHECK_EQUAL(myval.history.size(), 1);
    BOOST_CHECK_EQUAL(myval.read(), 6);
    BOOST_CHECK_EQUAL(snapshot_info.entries.size(), 0);
    BOOST_CHECK_EQUAL(get_current_epoch(), starting_epoch + 1);

    current_epoch_ = 1;
    earliest_epoch_ = 1;
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
    //run_object_test(1, 10000);
    //run_object_test(10, 1000);
    run_object_test(1, 100000);
    run_object_test(10, 10000);
    run_object_test(100, 1000);
    run_object_test(1000, 100);
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
            
            bool succeeded = false;

            while (!succeeded) {
                Local_Transaction trans;
                
                // Now that we're inside, the total should be zero
                ssize_t total = 0;
                for (unsigned i = 0;  i < nvars;  ++i)
                    total += vars[i].read();
                if (total != 0) {
                    cerr << "total is " << total << endl;
                    ++errors;
                }
                
                int & val1 = vars[var1].mutate();
                int & val2 = vars[var2].mutate();
                    
                val1 -= 1;
                val2 += 1;
                
                succeeded = trans.commit();
                local_failures += !succeeded;
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

    BOOST_CHECK_EQUAL(snapshot_info.entries.size(), 0);

    BOOST_CHECK_EQUAL(total, 0);
    for (unsigned i = 0;  i < nvals;  ++i) {
        if (vals[i].history.size() != 1)
            vals[i].dump();
        BOOST_CHECK_EQUAL(vals[i].history.size(), 1);
    }
}


BOOST_AUTO_TEST_CASE( test2 )
{
    cerr << endl << endl << "========= test 2: multiple variables" << endl;
    
    run_object_test2(1, 10, 1);
    //run_object_test2(2, 20, 10);
    run_object_test2(2,  50000, 2);
    run_object_test2(10, 10000, 100);
    run_object_test2(100, 1000, 10);
    run_object_test2(1000, 100, 100);
}



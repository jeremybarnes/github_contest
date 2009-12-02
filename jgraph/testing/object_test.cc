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
#include "arch/cmp_xchg.h"

using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;

struct Updater {
    virtual bool update() const = 0;
};

struct Ref {
    
    struct Value {
        Value * next;
        void * data;
    };
};

/// Global variable giving the current epoch; non-decreasing
size_t current_epoch = 0;

/// Global variable giving the earliest epoch for which there is a snapshot
size_t earliest_epoch = 0;

struct Type {
};


/// Base class for a value.  An object at any given epoch has an instantaneous
/// value.
struct Value {
};

struct RWSpinlock {
};

/// Object that records the history of committed values.  Only the values
/// needed for active transactions are kept.
template<typename T>
struct History {

    /// Once it goes into the history, it's immutable
    const T & latest_value() const;

    /// Return the value for the given epoch
    const T & value_at_epoch(size_t epoch) const;

    /// Update the current value at a new epoch.  Returns true if it
    /// succeeded.  If the value has changed since the old epoch, it will
    /// not succeed.
    bool set_current_value(size_t old_epoch, const T & new_value);

    /// Erase the current value.  Will fail if the object was modified since
    /// the given epoch.
    void erase(size_t old_epoch) const;

    /// Clean out any history entries that are associated with the given
    /// epoch and not the next epoch.  Works atomically.  Returns true if
    /// the object itself can now be reused.
    bool prune(size_t curr_epoch, size_t next_epoch);
    
private:
    struct Entry {
        size_t epoch;
        T value;
        bool deleted;  // If true, object has disappeared
    };
    
    Entry * entries;
    int start;
    int size;

    RWSpinlock lock;
};

/// This is an actual object.  Contains metadata and value history of an
/// object.
struct Object {

    // Lock the current value into memory, so that 
    virtual void lock_value() const = 0;
};

template<typename T>
struct Value : public Object {

    // Client interface.  Just two methods to get at the current value.
    T & mutate_value()
    {
        if (!current_trans) no_transaction_exception(this);
        T * local = current_trans->local_value<T>(this);

        if (!local) {
            T value = history.value_at_epoch(current_trans->epoch);
            local = current_trans->local_value<int>(this, value);
        }

        return *local;
    }
    
    const int & read_value() const
    {
        if (!current_trans) return history.latest_value();
        else return history.value_at_epoch(current_trans->epoch);
    }

private:
    // Implement object interface

    History<int> history;


    // Question: should the local value storage for transactions go in here
    // as well?
    // Pros: allows a global view that can find conflicts
    // Cons: May cause lots of extra memory allocations
};


// What about list of objects?
struct List : public Object {
};


/// A snapshot provides a view of all objects that is frozen at the moment
/// the shapshot was created.  Provides a read-only view.
///
/// Note that long-lived snapshots might be created (in order to take
/// hot backups or for replication).  We need to be efficient in order to
/// do so.

struct Snapshot {
    size_t epoch;  ///< Epoch at which snapshot was taken
    Snapshot * prev;  ///< Previous snapshot (by epoch)
    Snapshot * next;  ///< Next snapshot (by epoch)
};

struct Local_Snapshot {
};


/// A sandbox provides a place where writes don't affect the underlying
/// objects.  These writes can then be committed, with 
struct Sandbox {
    hash_map<Object *, size_t> local_values;
};

/// A transaction is both a snapshot and a sandbox.
struct Transaction : public Snapshot, public Sandbox {
};

__thread Sandbox * current_sandbox;
__thread Snapshot * current_snapshot;
__thread Transaction * current_trans;

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

#if 0
template<typename T>
struct UpdaterT : public Updater {
    UpdaterT(T * where, T old_val, T * new_val)
        : where(where), old_val(old_val), new_val(new_val)
    {
    }

    T * where;
    T old_val;
    T * new_val;

    virtual bool update() const
    {
        T old = old_val;

        if (*where != old) return false;  // changed; update impossible
        
        bool success = cmp_xchg(*where, old, *new_val);

        cerr << "after update: old = " << old << " new = " << *where << endl;

        return success;



        // Garbage collect either the old or the new
    }


    virtual void rollback() const
    {
    }
};

struct Transaction {
    void restart()
    {
    }

    bool commit()
    {
        bool succeeded = true;

        for (Updaters::iterator
                 it = updaters.begin(),
                 end = updaters.end();
             it != end;  ++it) {
            if (succeeded)
                succeeded = it->second->update();
            delete it->second;
        }
        
        updaters.clear();

        cerr << "commit: success = " << succeeded << endl;

        return succeeded;
    }

    void abort()
    {
        for (Updaters::iterator
                 it = updaters.begin(),
                 end = updaters.end();
             it != end;  ++it) {
            delete it->second;
        }
        updaters.clear();
    }
    
    template<typename T>
    T * get_local(T * val)
    {
        T * new_val = new T(*val);

        size_t address = reinterpret_cast<size_t>(val);
        
        updaters[address] = new UpdaterT<T>(val, *new_val, new_val);

        return new_val;
    }

    void update(Object & obj, const Object & new_value)
    {
    }

    typedef hash_map<size_t, Updater *> Updaters;
    Updaters updaters;
};

__thread Transaction * current_trans = 0;


template<class Payload>
struct Variable {
    Variable(Payload * val, bool is_local = false)
        : val(val), is_local(is_local)
    {
    }

    operator const Payload & () const { return *val; }

    void mutate()
    {
        if (!current_trans) return;

        // Tell that transaction that when it commits, it needs to transfer
        // from our new value to its value
        val = current_trans->get_local(val);
    }

    Variable operator + (Payload amount)
    {
        Variable result(current_trans->get_local(val));
    }

private:
    Payload * val;
    bool is_local;
};

#endif

void object_test_thread(Variable<int> var, int iter, boost::barrier & barrier)
{
    // Wait for all threads to start up before we continue
    barrier.wait();

    for (unsigned i = 0;  i < iter;  ++i) {
        // Keep going until we succeed
        Local_Transaction trans;
        do {
            trans->update(var, var + 1);
            BOOST_CHECK_EQUAL(var % 2, 1);
            trans->update(var, var + 1);
        } while (!trans.commit());

        BOOST_CHECK_EQUAL(var % 2, 0);
    }
}

void run_object_test()
{
    int value = 0;
    Variable<int> var(&value);
    int niter = 10;
    int nthreads = 1;
    boost::barrier barrier(nthreads);
    boost::thread_group tg;
    for (unsigned i = 0;  i < nthreads;  ++i)
        tg.create_thread(boost::bind(&object_test_thread, var, niter, boost::ref(barrier)));

    tg.join_all();

    BOOST_CHECK_EQUAL(var, niter * nthreads * 2);
}


BOOST_AUTO_TEST_CASE( test1 )
{
    run_object_test();
}

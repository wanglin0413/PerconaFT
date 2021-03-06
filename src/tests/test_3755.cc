/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
/*======
This file is part of PerconaFT.


Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

    PerconaFT is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License, version 2,
    as published by the Free Software Foundation.

    PerconaFT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PerconaFT.  If not, see <http://www.gnu.org/licenses/>.

----------------------------------------

    PerconaFT is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License, version 3,
    as published by the Free Software Foundation.

    PerconaFT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with PerconaFT.  If not, see <http://www.gnu.org/licenses/>.
======= */

#ident "Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved."

// stress test for update broadcast.  10M 8-byte keys should be 2, maybe 3
// levels of treeness, makes sure flushes work

#include "test.h"

const int envflags = DB_INIT_MPOOL|DB_CREATE|DB_THREAD |DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_TXN|DB_PRIVATE;

DB_ENV *env;

const unsigned int NUM_KEYS = 1024;


static int update_fun(DB *UU(db),
                      const DBT *UU(key),
                      const DBT *old_val, const DBT *extra,
                      void (*set_val)(const DBT *new_val,
                                      void *set_extra),
                      void *set_extra) 
{
    assert(extra->size == sizeof(unsigned int));
    assert(old_val->size == sizeof(unsigned int));
    unsigned int e = *(unsigned int *)extra->data;    
    unsigned int ov = *(unsigned int *)old_val->data;
    assert(e == (ov+1));
    {
        DBT newval;
        set_val(dbt_init(&newval, &e, sizeof(e)), set_extra);
    }
    //usleep(10);
    return 0;
}

static int
int_cmp(DB *UU(db), const DBT *a, const DBT *b) {
    unsigned int *ap, *bp;
    assert(a->size == sizeof(*ap));
    CAST_FROM_VOIDP(ap, a->data);
    assert(b->size == sizeof(*bp));
    CAST_FROM_VOIDP(bp, b->data);
    return (*ap > *bp) - (*ap < *bp);
}

static void setup (void) {
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    { int chk_r = toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(chk_r); }
    { int chk_r = db_env_create(&env, 0); CKERR(chk_r); }
    env->set_errfile(env, stderr);
    env->set_update(env, update_fun);
    { int chk_r = env->set_default_bt_compare(env, int_cmp); CKERR(chk_r); }
    { int chk_r = env->open(env, TOKU_TEST_FILENAME, envflags, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(chk_r); }
    // make a really small checkpointing period
    { int chk_r = env->checkpointing_set_period(env,1); CKERR(chk_r); }
}

static void cleanup (void) {
    { int chk_r = env->close(env, 0); CKERR(chk_r); }
}

static int do_inserts(DB_TXN *txn, DB *db) {
    int r = 0;
    DBT key, val;
    unsigned int i, v;
    DBT *keyp = dbt_init(&key, &i, sizeof(i));
    DBT *valp = dbt_init(&val, &v, sizeof(v));
    for (i = 0; i < NUM_KEYS; ++i) {
        v = 0;
        r = db->put(db, txn, keyp, valp, 0); CKERR(r);
    }
    return r;
}

static int do_updates(DB_TXN *txn, DB *db, unsigned int i) {
    DBT extra;
    unsigned int e = i;
    DBT *extrap = dbt_init(&extra, &e, sizeof(e));
    int r = db->update_broadcast(db, txn, extrap, 0); CKERR(r);
    return r;
}


int test_main(int argc, char * const argv[]) {
    parse_args(argc, argv);
    setup();

    DB *db;

    IN_TXN_COMMIT(env, NULL, txn_1, 0, {
            { int chk_r = db_create(&db, env, 0); CKERR(chk_r); }
            { int chk_r = db->set_pagesize(db, 1<<8); CKERR(chk_r); }
            { int chk_r = db->open(db, txn_1, "foo.db", NULL, DB_BTREE, DB_CREATE, 0666); CKERR(chk_r); }

            { int chk_r = do_inserts(txn_1, db); CKERR(chk_r); }
        });

    for(unsigned int i = 1; i < 100; i++) {
        IN_TXN_COMMIT(env, NULL, txn_2, 0, {
                { int chk_r = do_updates(txn_2, db, i); CKERR(chk_r); }
            });
        for (unsigned int curr_key = 0; curr_key < NUM_KEYS; ++curr_key) {
            DBT key, val;
            unsigned int *vp;
            DBT *keyp = dbt_init(&key, &curr_key, sizeof(curr_key));
            DBT *valp = dbt_init(&val, NULL, 0);
            IN_TXN_COMMIT(env, NULL, txn_3, 0, {
                    { int chk_r = db->get(db, txn_3, keyp, valp, 0); CKERR(chk_r); }
            });
            assert(val.size == sizeof(*vp));
            CAST_FROM_VOIDP(vp, val.data);
            assert(*vp==i);
        }
    }

    { int chk_r = db->close(db, 0); CKERR(chk_r); }

    cleanup();

    return 0;
}

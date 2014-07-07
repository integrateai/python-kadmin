
/* we are going to reuse the internal get_principal function as the foundation of our 
	kdb_entry to kadm5_principal_ent_t mapper
    
    svr_principal.c
	kadm5_ret_t
kadm5_get_principal(void *server_handle, krb5_principal principal,
                    kadm5_principal_ent_t entry,
                    long in_mask)

	*/

#include "PyKAdminCommon.h"

krb5_error_code pykadmin_unpack_xdr_osa_princ_ent_rec(PyKAdminObject *kadmin, krb5_db_entry *kdb, osa_princ_ent_rec *adb) {

    krb5_error_code retval = 0; 

    XDR xdrs;
    krb5_tl_data tl_data;

    tl_data.tl_data_type = KRB5_TL_KADM_DATA;

    if ((retval = krb5_dbe_lookup_tl_data(kadmin->context, kdb, &tl_data)) || (tl_data.tl_data_length == 0)) {
        adb->admin_history_kvno = 0;
    }

    xdrmem_create(&xdrs, (caddr_t)tl_data.tl_data_contents, tl_data.tl_data_length, XDR_DECODE);

    if (!pykadmin_xdr_osa_princ_ent_rec(&xdrs, adb)) {
        xdr_destroy(&xdrs);
        retval = KADM5_XDR_FAILURE;
        goto done;
    }

    xdr_destroy(&xdrs);

    retval = KADM5_OK;
done: 
    return retval;
}


/*
    
    The following two functions are taken directly from svr_principal.c 
        the comment preceeding them indicates that they *may* be released into the public api.
        until that point there they will simply be copied here as static versions

*/

static kadm5_ret_t krb5_copy_key_data_contents(context, from, to)
    krb5_context context;
    krb5_key_data *from, *to;
{
    int i, idx;

    *to = *from;

    idx = (from->key_data_ver == 1 ? 1 : 2);

    for (i = 0; i < idx; i++) {
        if ( from->key_data_length[i] ) {
            to->key_data_contents[i] = malloc(from->key_data_length[i]);
            if (to->key_data_contents[i] == NULL) {
                for (i = 0; i < idx; i++) {
                    if (to->key_data_contents[i]) {
                        memset(to->key_data_contents[i], 0,
                               to->key_data_length[i]);
                        free(to->key_data_contents[i]);
                    }
                }
                return ENOMEM;
            }
            memcpy(to->key_data_contents[i], from->key_data_contents[i],
                   from->key_data_length[i]);
        }
    }
    return 0;
}

static krb5_tl_data *dup_tl_data(krb5_tl_data *tl)
{
    krb5_tl_data *n;

    n = (krb5_tl_data *) malloc(sizeof(krb5_tl_data));
    if (n == NULL)
        return NULL;
    n->tl_data_contents = malloc(tl->tl_data_length);
    if (n->tl_data_contents == NULL) {
        free(n);
        return NULL;
    }
    memcpy(n->tl_data_contents, tl->tl_data_contents, tl->tl_data_length);
    n->tl_data_type = tl->tl_data_type;
    n->tl_data_length = tl->tl_data_length;
    n->tl_data_next = NULL;
    return n;
}


krb5_error_code pykadmin_kadm_from_kdb(PyKAdminObject *kadmin, krb5_db_entry *kdb, kadm5_principal_ent_rec *entry, long mask) {

    krb5_error_code retval = 0; 
    int i;

    osa_princ_ent_rec *adb = NULL;

    memset(entry, 0, sizeof(kadm5_principal_ent_rec));
    adb = calloc(1, sizeof(osa_princ_ent_rec));

    //memset(adb, 0, sizeof(osa_princ_ent_rec));

    /* principal */

    if (mask & KADM5_PRINCIPAL) {
        if ((retval = krb5_copy_principal(kadmin->context, kdb->princ, &entry->principal)))
            goto done;
    }

    /* members with a direct relationship */

    if (mask & KADM5_PRINC_EXPIRE_TIME)
        entry->princ_expire_time = kdb->expiration;
    
    if (mask & KADM5_PW_EXPIRATION)
        entry->pw_expiration = kdb->pw_expiration;

    if (mask & KADM5_MAX_LIFE)
        entry->max_life = kdb->max_life;

    if (mask & KADM5_MAX_RLIFE)
        entry->max_renewable_life = kdb->max_renewable_life;

    if (mask & KADM5_LAST_SUCCESS)
        entry->last_success = kdb->last_success;

    if (mask & KADM5_LAST_FAILED)
        entry->last_failed = kdb->last_failed;

    if (mask & KADM5_FAIL_AUTH_COUNT)
        entry->fail_auth_count = kdb->fail_auth_count;

    if (mask & KADM5_ATTRIBUTES)
        entry->attributes = kdb->attributes;

    /* members with computed values */

    if (mask & KADM5_LAST_PWD_CHANGE) {
        if ((retval = krb5_dbe_lookup_last_pwd_change(kadmin->context, kdb, &entry->last_pwd_change)))
            goto done; 
    }

    if ((mask & KADM5_MOD_NAME) || (mask & KADM5_MOD_TIME)) {
        if ((retval = krb5_dbe_lookup_mod_princ_data(kadmin->context, kdb, &(entry->mod_date), &(entry->mod_name))));
            goto done;

        if (! (mask & KADM5_MOD_TIME))
            entry->mod_date = 0;

        if (! (mask & KADM5_MOD_NAME)) {
            krb5_free_principal(kadmin->context, entry->mod_name);
            entry->mod_name = NULL;
        }
    }

    if (mask & KADM5_KVNO) {
        for (entry->kvno = 0, i=0; i<kdb->n_key_data; i++)
            if ((krb5_kvno) kdb->key_data[i].key_data_kvno > entry->kvno)
                entry->kvno = kdb->key_data[i].key_data_kvno;
    }

    /* api vs github src differ come back to  
    TODO
    if (mask & KADM5_MKVNO) {
        if ((retval = krb5_dbe_get_mkvno(kadmin->context, kdb, &entry->mkvno)))
            goto done;
    }
    */


    /* key data */

    if (mask & KADM5_TL_DATA) {
        krb5_tl_data *tl, *tl2;

        entry->tl_data = NULL;

        tl = kdb->tl_data;
        while (tl) {
            if (tl->tl_data_type > 255) {
                if ((tl2 = dup_tl_data(tl)) == NULL) {
                    goto done;
                }
                tl2->tl_data_next = entry->tl_data;
                entry->tl_data = tl2;
                entry->n_tl_data++;
            }

            tl = tl->tl_data_next;
        }
    }


    if (mask & KADM5_KEY_DATA) {

        entry->n_key_data = kdb->n_key_data;

        if(entry->n_key_data) {
            entry->key_data = calloc(entry->n_key_data, sizeof(krb5_key_data));
            if (!entry->key_data)
                goto done;
        } else
            entry->key_data = NULL;

            for (i = 0; i < entry->n_key_data; i++)
                retval = krb5_copy_key_data_contents(kadmin->context, &kdb->key_data[i], &entry->key_data[i]);
        if (retval)
            goto done;
    }


    /* 
        compute adb value of kadm5_get_principal function using the internal mechanism kdb_get_entry
 
        krb5/src/lib/kadm5/srv/svr_principal.c
        kadm5_get_principal()

        krb5/src/lib/kadm5/srv/server_kdb.c 
        kdb_get_entry()

    */

    if ((retval = pykadmin_unpack_xdr_osa_princ_ent_rec(kadmin, kdb, adb)))
        goto done;

    /* load data stored into the entry rec */

    if (mask & KADM5_POLICY) {
        if ((adb->aux_attributes & KADM5_POLICY) && adb->policy) {
            entry->policy = strdup(adb->policy);
        }
    }

    if (mask & KADM5_AUX_ATTRIBUTES)
        entry->aux_attributes = adb->aux_attributes;

    pykadmin_xdr_osa_free_princ_ent(adb);

    retval = KADM5_OK;

done: 
    if (retval && entry->principal) {
        krb5_free_principal(kadmin->context, entry->principal);
        entry->principal = NULL;
    }
    
    return retval;

}


/*
krb5_error_code pykadmin_copy_kadm_ent_rec(PyKAdminObject *kadmin, kadm5_principal_ent_rec *src, kadm5_principal_ent_rec *dst) {

    krb5_error_code retval = 0;

    memcpy(src, dst, sizeof(kadm5_principal_ent_rec));

    retval = krb5_copy_principal(kadmin->context, src->principal, &dst->principal);

    if (retval) goto done; 




done:
    if (retval && entry->principal) {
        krb5_free_principal(kadmin->context, entry->principal);
        entry->principal = NULL;
    }
    return retval;
}
*/


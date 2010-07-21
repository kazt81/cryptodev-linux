/*
 * New driver for /dev/crypto device (aka CryptoDev)

 * Copyright (c) 2010 Nikos Mavrogiannopoulos <nmav@gnutls.org>
 *
 * This file is part of linux cryptodev.
 *
 * cryptodev is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cryptodev is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/crypto.h>
#include "cryptodev.h"
#include "ncr.h"
#include "ncr_int.h"

static void _ncr_session_remove(struct list_sem_st* lst, ncr_session_t desc);

void ncr_sessions_list_deinit(struct list_sem_st* lst)
{
	if(lst) {
		struct session_item_st * item, *tmp;

		down(&lst->sem);
		
		list_for_each_entry_safe(item, tmp, &lst->list, list) {
			list_del(&item->list);
			_ncr_sessions_item_put( item); /* decrement ref count */
		}
		up(&lst->sem);

	}
}

/* must be called with data semaphore down
 */
static ncr_session_t _ncr_sessions_get_new_desc( struct list_sem_st* lst)
{
struct session_item_st* item;
int mx = 1;

	list_for_each_entry(item, &lst->list, list) {
		mx = max(mx, item->desc);
	}
	mx++;

	return mx;
}

/* returns the data item corresponding to desc */
struct session_item_st* ncr_sessions_item_get( struct list_sem_st* lst, ncr_session_t desc)
{
struct session_item_st* item;

	down(&lst->sem);
	list_for_each_entry(item, &lst->list, list) {
		if (item->desc == desc) {
			atomic_inc(&item->refcnt);
			up(&lst->sem);
			return item;
		}
	}
	up(&lst->sem);

	err();
	return NULL;
}

void _ncr_sessions_item_put( struct session_item_st* item)
{
	if (atomic_dec_and_test(&item->refcnt)) {
		cryptodev_cipher_deinit(&item->cipher);
		ncr_pk_cipher_deinit(&item->pk);
		cryptodev_hash_deinit(&item->hash);
		if (item->key)
			_ncr_key_item_put(item->key);
		kfree(item);
	}
}

struct session_item_st* ncr_session_new(struct list_sem_st* lst)
{
	struct session_item_st* sess;

	sess = kmalloc(sizeof(*sess), GFP_KERNEL);
	if (sess == NULL) {
		err();
		return NULL;
	}

	memset(sess, 0, sizeof(*sess));

	atomic_set(&sess->refcnt, 1);

	down(&lst->sem);

	sess->desc = _ncr_sessions_get_new_desc(lst);
	list_add(&sess->list, &lst->list);
	
	up(&lst->sem);

	return sess;
}

static const struct algo_properties_st {
	ncr_algorithm_t algo;
	const char* kstr;
	unsigned needs_iv:1;
	unsigned hmac:1;
	unsigned can_sign:1;
	unsigned can_digest:1;
	unsigned can_encrypt:1;
	unsigned symmetric:1;
	int digest_size;
} algo_properties[] = {
	{ .algo = NCR_ALG_NULL, .kstr = "ecb(cipher_null)", 
		.needs_iv = 0, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_3DES_CBC, .kstr = "cbc(des3_ede)", 
		.needs_iv = 1, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_AES_CBC, .kstr = "cbc(aes)", 
		.needs_iv = 1, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_CAMELLIA_CBC, .kstr = "cbc(camelia)", 
		.needs_iv = 1, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_AES_CTR, .kstr = "ctr(aes)", 
		.needs_iv = 1, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_CAMELLIA_CTR, .kstr = "ctr(camelia)", 
		.needs_iv = 1, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_ARCFOUR, .kstr = NULL, 
		.needs_iv = 0, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_AES_ECB, .kstr = "ecb(aes)", 
		.needs_iv = 0, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_CAMELLIA_ECB, .kstr = "ecb(camelia)", 
		.needs_iv = 0, .symmetric=1, .can_encrypt=1 },
	{ .algo = NCR_ALG_SHA1, .kstr = "sha1", 
		.digest_size = 20, .can_digest=1 },
	{ .algo = NCR_ALG_MD5, .kstr = "md5", 
		.digest_size = 16, .can_digest=1 },
	{ .algo = NCR_ALG_SHA2_224, .kstr = "sha224", 
		.digest_size = 28, .can_digest=1 },
	{ .algo = NCR_ALG_SHA2_256, .kstr = "sha256", 
		.digest_size = 32, .can_digest=1 },
	{ .algo = NCR_ALG_SHA2_384, .kstr = "sha384", 
		.digest_size = 48, .can_digest=1 },
	{ .algo = NCR_ALG_SHA2_512, .kstr = "sha512", 
		.digest_size = 64, .can_digest=1 },
	{ .algo = NCR_ALG_HMAC_SHA1, .hmac = 1, .kstr = "hmac(sha1)", 
		.digest_size = 20, .can_sign=1 },
	{ .algo = NCR_ALG_HMAC_MD5, .hmac = 1, .kstr = "hmac(md5)", 
		.digest_size = 16, .can_sign=1 },
	{ .algo = NCR_ALG_HMAC_SHA2_224, .hmac = 1, .kstr = "hmac(sha224)", 
		.digest_size = 28, .can_sign=1 },
	{ .algo = NCR_ALG_HMAC_SHA2_256, .hmac = 1, .kstr = "hmac(sha256)", 
		.digest_size = 32, .can_sign=1 },
	{ .algo = NCR_ALG_HMAC_SHA2_384, .hmac = 1, .kstr = "hmac(sha384)", 
		.digest_size = 48, .can_sign=1 },
	{ .algo = NCR_ALG_HMAC_SHA2_512, .hmac = 1, .kstr = "hmac(sha512)", 
		.digest_size = 64, .can_sign=1 },
	{ .algo = NCR_ALG_RSA, .kstr = NULL, 
		.can_encrypt=1, .can_sign=1},
	{ .algo = NCR_ALG_DSA, .kstr = NULL, 
		.can_sign=1 },
	{ .algo = NCR_ALG_NONE }

};

const char* _ncr_algo_to_str(ncr_algorithm_t algo)
{
ncr_algorithm_t a;
int i = 0;

	while((a=algo_properties[i].algo)!=NCR_ALG_NONE) {
		if (a == algo)
			return algo_properties[i].kstr;
		i++;
	}

	return NULL;
}

static int algo_needs_iv(ncr_algorithm_t algo)
{
ncr_algorithm_t a;
int i = 0;

	while((a=algo_properties[i].algo)!=NCR_ALG_NONE) {
		if (a == algo)
			return algo_properties[i].needs_iv;
		i++;
	}

	return 0;
}

static int algo_can_sign(ncr_algorithm_t algo)
{
ncr_algorithm_t a;
int i = 0;

	while((a=algo_properties[i].algo)!=NCR_ALG_NONE) {
		if (a == algo)
			return algo_properties[i].can_sign;
		i++;
	}

	return 0;
}

static int algo_can_encrypt(ncr_algorithm_t algo)
{
ncr_algorithm_t a;
int i = 0;

	while((a=algo_properties[i].algo)!=NCR_ALG_NONE) {
		if (a == algo)
			return algo_properties[i].can_encrypt;
		i++;
	}

	return 0;
}

static int algo_can_digest(ncr_algorithm_t algo)
{
ncr_algorithm_t a;
int i = 0;

	while((a=algo_properties[i].algo)!=NCR_ALG_NONE) {
		if (a == algo)
			return algo_properties[i].can_digest;
		i++;
	}

	return 0;
}


static int algo_is_hmac(ncr_algorithm_t algo)
{
ncr_algorithm_t a;
int i = 0;

	while((a=algo_properties[i].algo)!=NCR_ALG_NONE) {
		if (a == algo)
			return algo_properties[i].hmac;
		i++;
	}

	return 0;
}

static int algo_is_symmetric(ncr_algorithm_t algo)
{
ncr_algorithm_t a;
int i = 0;

	while((a=algo_properties[i].algo)!=NCR_ALG_NONE) {
		if (a == algo)
			return algo_properties[i].symmetric;
		i++;
	}

	return 0;
}

int _ncr_algo_digest_size(ncr_algorithm_t algo)
{
ncr_algorithm_t a;
int i = 0;

	while((a=algo_properties[i].algo)!=NCR_ALG_NONE) {
		if (a == algo)
			return algo_properties[i].digest_size;
		i++;
	}

	return 0;
}

static int _ncr_session_init(struct ncr_lists* lists, struct ncr_session_st* session)
{
	struct session_item_st* ns = NULL;
	int ret;
	const char* str = NULL;

	ns = ncr_session_new(&lists->sessions);
	if (ns == NULL) {
		err();
		return -EINVAL;
	}

	ns->op = session->op;
	ns->algorithm = session->algorithm;
	switch(session->op) {
		case NCR_OP_ENCRYPT:
		case NCR_OP_DECRYPT:
			if (algo_can_encrypt(session->algorithm)==0) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			/* read key */
			ret = ncr_key_item_get_read( &ns->key, &lists->key, session->params.key);
			if (ret < 0) {
				err();
				goto fail;
			}
			if (ns->key->type == NCR_KEY_TYPE_SECRET) {
				int keysize = ns->key->key.secret.size;
				
				if (session->algorithm == NCR_ALG_NULL)
				  keysize = 0;
				
				str = _ncr_algo_to_str(session->algorithm);
				if (str == NULL) {
					err();
					return -EINVAL;
				}

				ret = cryptodev_cipher_init(&ns->cipher, str, 
					ns->key->key.secret.data, keysize);
				if (ret < 0) {
					err();
					goto fail;
				}

				if (algo_needs_iv(session->algorithm)) {
					if (session->params.params.cipher.iv_size > sizeof(session->params.params.cipher.iv)) {
						err();
						ret = -EINVAL;
						goto fail;
					}
					cryptodev_cipher_set_iv(&ns->cipher, session->params.params.cipher.iv, session->params.params.cipher.iv_size);
				}
			} else if (ns->key->type == NCR_KEY_TYPE_PRIVATE || ns->key->type == NCR_KEY_TYPE_PUBLIC) {
				ret = ncr_pk_cipher_init(ns->algorithm, &ns->pk, 
					&session->params, ns->key);
				if (ret < 0) {
					err();
					goto fail;
				}
			} else {
				err();
				ret = -EINVAL;
				goto fail;
			}
			break;

		case NCR_OP_SIGN:
		case NCR_OP_VERIFY:
			if (algo_can_sign(session->algorithm)==0) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			/* read key */
			ret = ncr_key_item_get_read( &ns->key, &lists->key, session->params.key);
			if (ret < 0) {
				err();
				goto fail;
			}

			if (ns->key->type == NCR_KEY_TYPE_SECRET) {
				str = _ncr_algo_to_str(session->algorithm);
				if (str == NULL) {
					err();
					return -EINVAL;
				}

				ret = cryptodev_hash_init(&ns->hash, str, 1, 
					ns->key->key.secret.data, ns->key->key.secret.size);
				if (ret < 0) {
					err();
					goto fail;
				}

			} else if (ns->key->type == NCR_KEY_TYPE_PRIVATE || ns->key->type == NCR_KEY_TYPE_PUBLIC) {
				if (algo_can_digest(session->params.params.pk.sign_hash) == 0) {
					err();
					ret = -EINVAL;
					goto fail;
				}
				str = _ncr_algo_to_str(session->params.params.pk.sign_hash);
				if (str == NULL) {
					err();
					ret = -EINVAL;
					goto fail;
				}

				ret = ncr_pk_cipher_init(ns->algorithm, &ns->pk, 
					&session->params, ns->key);
				if (ret < 0) {
					err();
					goto fail;
				}

				ret = cryptodev_hash_init(&ns->hash, str, 0, NULL, 0);
				if (ret < 0) {
					err();
					goto fail;
				}
			} else {
				err();
				ret = -EINVAL;
				goto fail;
			}

			break;
		case NCR_OP_DIGEST:
			if (algo_can_digest(session->algorithm)==0) {
				err();
				ret = -EINVAL;
				goto fail;
			}
			str = _ncr_algo_to_str(session->algorithm);
			if (str == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			ret = cryptodev_hash_init(&ns->hash, str, 0, NULL, 0);
			if (ret < 0) {
				err();
				goto fail;
			}

			break;
		default:
			err();
			ret = -EINVAL;
			goto fail;
	}
	
	ret = 0;
	session->ses = ns->desc;

fail:
	if (ret < 0) {
		_ncr_session_remove(&lists->sessions, ns->desc);
	}

	return ret;
}

int ncr_session_init(struct ncr_lists* lists, void __user* arg)
{
	struct ncr_session_st session;
	int ret;

	if (unlikely(copy_from_user(&session, arg, sizeof(session)))) {
		err();
		return -EFAULT;
	}

	ret = _ncr_session_init(lists, &session);
	if (unlikely(ret)) {
		err();
		return ret;
	}

	ret = copy_to_user( arg, &session, sizeof(session));
	if (unlikely(ret)) {
		err();
		_ncr_session_remove(&lists->sessions, session.ses);
		return -EFAULT;
	}
	return ret;
}

/* Main update function
 */
static int _ncr_session_update(struct ncr_lists* lists, struct ncr_session_op_st* op)
{
	int ret;
	struct session_item_st* sess;
	struct data_item_st* data = NULL;
	struct data_item_st* odata = NULL;
	struct scatterlist *osg;
	struct scatterlist *isg;
	size_t osg_size, isg_size, new_size, max_odata_size, idata_size;
	unsigned int osg_cnt, isg_cnt;

	sess = ncr_sessions_item_get( &lists->sessions, op->ses);
	if (sess == NULL) {
		err();
		return -EINVAL;
	}

	switch(sess->op) {
		case NCR_OP_ENCRYPT:
			/* obtain data item */
			data = ncr_data_item_get( &lists->data, op->data.cipher.plaintext);
			if (data == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			odata = ncr_data_item_get( &lists->data, op->data.cipher.ciphertext);
			if (odata == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			/* output data will have the same status as input data */
			ret = ncr_data_item_get_sg(odata, &osg, &osg_cnt, NULL, &osg_size, data->flags, 1);
			if (ret < 0) {
				err();
				goto fail;
			}

			if (odata != data) {
				ret = ncr_data_item_get_sg(data, &isg, &isg_cnt, &isg_size, NULL, data->flags, 0);
				if (ret < 0) {
					err();
					goto fail;
				}
			} else {
				isg = osg;
				isg_cnt = osg_cnt;
				isg_size = osg_size;
			}

			ret = ncr_data_item_size(odata, 1);
			if (ret < 0) {
				err();
				goto fail;
			}
			max_odata_size = ret;

			ret = ncr_data_item_size(data, 0);
			if (ret < 0) {
				err();
				goto fail;
			}
			idata_size = ret;

			if (max_odata_size < idata_size) {
				err();
				ret = -EINVAL;
				goto fail;
			}
			
			if (algo_is_symmetric(sess->algorithm)) {
				/* read key */
				ret = cryptodev_cipher_encrypt(&sess->cipher, isg, osg, isg_size);
				if (ret < 0) {
					err();
					goto fail;
				}
	
				/* FIXME: handle ciphers that do not require that */
				new_size = idata_size;

			} else { /* public key */
				new_size = osg_size;
				ret = ncr_pk_cipher_encrypt(&sess->pk, isg, isg_cnt, isg_size,
					osg, osg_cnt, &new_size);
				if (ret < 0) {
					err();
					goto fail;
				}
			}
			ret = ncr_data_item_set_size(odata, new_size);
			if (ret < 0) {
				err();
				goto fail;
			}

			break;
		case NCR_OP_DECRYPT:
			/* obtain data item */
			data = ncr_data_item_get( &lists->data, op->data.cipher.ciphertext);
			if (data == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			odata = ncr_data_item_get( &lists->data, op->data.cipher.plaintext);
			if (odata == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			ret = ncr_data_item_get_sg(odata, &osg, &osg_cnt, NULL, &osg_size, data->flags, 1);
			if (ret < 0) {
				err();
				goto fail;
			}

			if (odata != data) {
				ret = ncr_data_item_get_sg(data, &isg, &isg_cnt, NULL, &isg_size, data->flags, 0);
				if (ret < 0) {
					err();
					goto fail;
				}
			} else {
				isg = osg;
				isg_cnt = osg_cnt;
				isg_size = osg_size;
			}

			ret = ncr_data_item_size(odata, 1);
			if (ret < 0) {
				err();
				goto fail;
			}
			max_odata_size = ret;

			ret = ncr_data_item_size(data, 0);
			if (ret < 0) {
				err();
				goto fail;
			}
			idata_size = ret;

			if (max_odata_size < idata_size) {
				err();
				ret = -EINVAL;
				goto fail;
			}
			
			/* read key */
			if (algo_is_symmetric(sess->algorithm)) {
				ret = cryptodev_cipher_decrypt(&sess->cipher, isg, osg, isg_size);
				if (ret < 0) {
					err();
					goto fail;
				}
				/* FIXME: handle ciphers that do not require that */
				new_size = idata_size;
			} else { /* public key */
				size_t new_size = osg_size;
				ret = ncr_pk_cipher_decrypt(&sess->pk, isg, isg_cnt, isg_size,
					osg, osg_cnt, &new_size);
				if (ret < 0) {
					err();
					goto fail;
				}
			}

			ret = ncr_data_item_set_size(odata, new_size);
			if (ret < 0) {
				err();
				goto fail;
			}


			break;

		case NCR_OP_SIGN:
		case NCR_OP_DIGEST:
			/* obtain data item */
			data = ncr_data_item_get( &lists->data, op->data.sign.text);
			if (data == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			ret = ncr_data_item_get_sg(data, &isg, &isg_cnt, &isg_size, NULL, data->flags, 0);
			if (ret < 0) {
				err();
				goto fail;
			}

			ret = cryptodev_hash_update(&sess->hash, isg, isg_size);
			if (ret < 0) {
				err();
				goto fail;
			}
			break;

		case NCR_OP_VERIFY:
			/* obtain data item */
			data = ncr_data_item_get( &lists->data, op->data.verify.text);
			if (data == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			ret = ncr_data_item_get_sg(data, &isg, &isg_cnt, &isg_size, NULL, data->flags, 0);
			if (ret < 0) {
				err();
				goto fail;
			}

			ret = cryptodev_hash_update(&sess->hash, isg, isg_size);
			if (ret < 0) {
				err();
				goto fail;
			}
			break;

		default:
			err();
			ret = -EINVAL;
			goto fail;
	}

	ret = 0;

fail:
	if (odata) {
		ncr_data_item_put_sg(odata);
		_ncr_data_item_put(odata);
	}
	if (data) {
		ncr_data_item_put_sg(data);
		_ncr_data_item_put(data);
	}
	_ncr_sessions_item_put(sess);

	return ret;
}

int ncr_session_update(struct ncr_lists* lists, void __user* arg)
{
	struct ncr_session_op_st op;

	if (unlikely(copy_from_user( &op, arg, sizeof(op)))) {
		err();
		return -EFAULT;
	}
	
	return _ncr_session_update(lists, &op);
}

static void _ncr_session_remove(struct list_sem_st* lst, ncr_session_t desc)
{
	struct session_item_st * item, *tmp;

	down(&lst->sem);
	
	list_for_each_entry_safe(item, tmp, &lst->list, list) {
		if(item->desc == desc) {
			list_del(&item->list);
			_ncr_sessions_item_put( item); /* decrement ref count */
			break;
		}
	}
	
	up(&lst->sem);

	return;
}

static int _ncr_session_final(struct ncr_lists* lists, struct ncr_session_op_st* op)
{
	int ret;
	struct session_item_st* sess;
	struct data_item_st* odata = NULL;
	int digest_size;
	uint8_t digest[NCR_HASH_MAX_OUTPUT_SIZE];
	struct scatterlist *osg;
	size_t osg_size, odata_size, max_odata_size;
	unsigned int osg_cnt;

	sess = ncr_sessions_item_get( &lists->sessions, op->ses);
	if (sess == NULL) {
		err();
		return -EINVAL;
	}

	switch(sess->op) {
		case NCR_OP_ENCRYPT:
		case NCR_OP_DECRYPT:
			/* obtain data item */
			if (op->data.cipher.plaintext != NCR_DATA_INVALID &&
				op->data.cipher.ciphertext != NCR_DATA_INVALID) {
				ret = _ncr_session_update(lists, op);
				if (ret < 0)
					goto fail;
			}
			break;

		case NCR_OP_VERIFY:
			/* obtain data item */
			if (op->data.sign.text != NCR_DATA_INVALID) {
				ret = _ncr_session_update(lists, op);
				if (ret < 0)
					goto fail;
			}
			
			odata = ncr_data_item_get( &lists->data, op->data.verify.signature);
			if (odata == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			digest_size = sess->hash.digestsize;
			if (digest_size == 0 || sizeof(digest) < digest_size) {
				err();
				ret = -EINVAL;
				goto fail;
			}
			ret = cryptodev_hash_final(&sess->hash, digest);
			if (ret < 0) {
				err();
				goto fail;
			}

			ret = ncr_data_item_size(odata, 0);
			if (ret < 0) {
				err();
				goto fail;
			}
			odata_size = ret;

			if (algo_is_hmac(sess->algorithm)) {
				uint8_t vdigest[digest_size];
				
				ret = ncr_data_item_getd( odata, vdigest, sizeof(vdigest), odata->flags);
				if (ret < 0) {
					err();
					goto fail;
				}

				if (digest_size != odata_size ||
					memcmp(vdigest, digest, digest_size) != 0) {

					op->err = NCR_VERIFICATION_FAILED;
				} else {
					op->err = NCR_SUCCESS;
				}
			} else {
				/* PK signature */
				ret = ncr_data_item_get_sg(odata, &osg, &osg_cnt, NULL, &osg_size, odata->flags, 0);
				if (ret < 0) {
					err();
					goto fail;
				}

				ret = ncr_pk_cipher_verify(&sess->pk, osg, osg_cnt, osg_size,
					digest, digest_size, &op->err);
				if (ret < 0) {
					err();
					goto fail;
				}
			}
			break;

		case NCR_OP_SIGN:
		case NCR_OP_DIGEST:
			/* obtain data item */
			if (op->data.sign.text != NCR_DATA_INVALID) {
				ret = _ncr_session_update(lists, op);
				if (ret < 0)
					goto fail;
			}
			odata = ncr_data_item_get( &lists->data, op->data.sign.output);
			if (odata == NULL) {
				err();
				ret = -EINVAL;
				goto fail;
			}

			ret = ncr_data_item_size(odata, 1);
			if (ret < 0) {
				err();
				goto fail;
			}
			max_odata_size = ret;

			digest_size = sess->hash.digestsize;
			if (digest_size == 0 || max_odata_size < digest_size) {
				err();
				ret = -EINVAL;
				goto fail;
			}
			ret = cryptodev_hash_final(&sess->hash, digest);
			if (ret < 0) {
				err();
				goto fail;
			}
			
			ret = ncr_data_item_setd( odata, digest, digest_size, odata->flags);
			if (ret < 0) {
				err();
				goto fail;
			}

			cryptodev_hash_deinit(&sess->hash);

			if (sess->op != NCR_OP_DIGEST && !algo_is_hmac(sess->algorithm)) {
				size_t new_size;

				ret = ncr_data_item_get_sg(odata, &osg, &osg_cnt, NULL, &osg_size, odata->flags, 1);
				if (ret < 0) {
					err();
					goto fail;
				}
				
				new_size = osg_size;

				/* PK signature */
				ret = ncr_pk_cipher_sign(&sess->pk, osg, osg_cnt, digest_size,
					osg, osg_cnt, &new_size);
				if (ret < 0) {
					err();
					goto fail;
				}
				
				ret = ncr_data_item_set_size(odata, new_size);
				if (ret < 0) {
					err();
					goto fail;
				}
			}

			break;
		default:
			err();
			ret = -EINVAL;
			goto fail;
	}

	ret = 0;

fail:
	if (odata) {
		ncr_data_item_put_sg(odata);
		_ncr_data_item_put(odata);
	}
	cryptodev_hash_deinit(&sess->hash);
	if (algo_is_symmetric(sess->algorithm)) {
		cryptodev_cipher_deinit(&sess->cipher);
	} else {
		ncr_pk_cipher_deinit(&sess->pk);
	}

	_ncr_sessions_item_put(sess);
	_ncr_session_remove(&lists->sessions, op->ses);

	return ret;
}

int ncr_session_final(struct ncr_lists* lists, void __user* arg)
{
	struct ncr_session_op_st op;
	int ret;

	if (unlikely(copy_from_user(&op, arg, sizeof(op)))) {
		err();
		return -EFAULT;
	}

	ret = _ncr_session_final(lists, &op);
	if (unlikely(ret)) {
		err();
		return ret;
	}

	if (unlikely(copy_to_user(arg, &op, sizeof(op)))) {
		err();
		return -EFAULT;
	}
	return 0;
}

int ncr_session_once(struct ncr_lists* lists, void __user* arg)
{
	struct ncr_session_once_op_st kop;
	int ret;

	if (unlikely(copy_from_user(&kop, arg, sizeof(kop)))) {
		err();
		return -EFAULT;
	}

	ret = _ncr_session_init(lists, &kop.init);
	if (ret < 0) {
		err();
		return ret;
	}
	kop.op.ses = kop.init.ses;

	ret = _ncr_session_final(lists, &kop.op);
	if (ret < 0) {
		err();
		return ret;
	}

	if (unlikely(copy_to_user(arg, &kop, sizeof(kop))))
		return -EFAULT;
	return 0;
}


/*
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2014 Zend Technologies Ltd. (http://www.zend.com) |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
   | Author: Zeev Suraski <zeev@zend.com>                                 |
   +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "zend.h"
#include "zend_qsort.h"
#include "zend_API.h"
#include "zend_ini.h"
#include "zend_alloc.h"
#include "zend_operators.h"
#include "zend_strtod.h"

static HashTable *registered_zend_ini_directives;

#define NO_VALUE_PLAINTEXT		"no value"
#define NO_VALUE_HTML			"<i>no value</i>"

/*
 * hash_apply functions
 */
static int zend_remove_ini_entries(zval *el, void *arg TSRMLS_DC) /* {{{ */
{
	zend_ini_entry *ini_entry = (zend_ini_entry *)Z_PTR_P(el);
	int module_number = *(int *)arg;
	if (ini_entry->module_number == module_number) {
		return 1;
	} else {
		return 0;
	}
}
/* }}} */

static int zend_restore_ini_entry_cb(zend_ini_entry *ini_entry, int stage TSRMLS_DC) /* {{{ */
{
	int result = FAILURE;

	if (ini_entry->modified) {
		if (ini_entry->on_modify) {
			zend_try {
			/* even if on_modify bails out, we have to continue on with restoring,
				since there can be allocated variables that would be freed on MM shutdown
				and would lead to memory corruption later ini entry is modified again */
				result = ini_entry->on_modify(ini_entry, ini_entry->orig_value, ini_entry->mh_arg1, ini_entry->mh_arg2, ini_entry->mh_arg3, stage TSRMLS_CC);
			} zend_end_try();
		}
		if (stage == ZEND_INI_STAGE_RUNTIME && result == FAILURE) {
			/* runtime failure is OK */
			return 1;
		}
		if (ini_entry->value != ini_entry->orig_value) {
			zend_string_release(ini_entry->value);
		}
		ini_entry->value = ini_entry->orig_value;
		ini_entry->modifiable = ini_entry->orig_modifiable;
		ini_entry->modified = 0;
		ini_entry->orig_value = NULL;
		ini_entry->orig_modifiable = 0;
	}
	return 0;
}
/* }}} */

static int zend_restore_ini_entry_wrapper(zval *el TSRMLS_DC) /* {{{ */
{
	zend_ini_entry *ini_entry = (zend_ini_entry *)Z_PTR_P(el);
	zend_restore_ini_entry_cb(ini_entry, ZEND_INI_STAGE_DEACTIVATE TSRMLS_CC);
	return 1;
}
/* }}} */

static void free_ini_entry(zval *zv) /* {{{ */
{
	zend_ini_entry *entry = (zend_ini_entry*)Z_PTR_P(zv);

	zend_string_release(entry->name);
	if (entry->value) {
		zend_string_release(entry->value);
	}
	if (entry->orig_value) {
		zend_string_release(entry->orig_value);
	}
	free(entry);
}
/* }}} */

/*
 * Startup / shutdown
 */
ZEND_API int zend_ini_startup(TSRMLS_D) /* {{{ */
{
	registered_zend_ini_directives = (HashTable *) malloc(sizeof(HashTable));

	EG(ini_directives) = registered_zend_ini_directives;
	EG(modified_ini_directives) = NULL;
	EG(error_reporting_ini_entry) = NULL;
	zend_hash_init_ex(registered_zend_ini_directives, 128, NULL, free_ini_entry, 1, 0);
	return SUCCESS;
}
/* }}} */

ZEND_API int zend_ini_shutdown(TSRMLS_D) /* {{{ */
{
	zend_hash_destroy(EG(ini_directives));
	free(EG(ini_directives));
	return SUCCESS;
}
/* }}} */

ZEND_API int zend_ini_global_shutdown(TSRMLS_D) /* {{{ */
{
	zend_hash_destroy(registered_zend_ini_directives);
	free(registered_zend_ini_directives);
	return SUCCESS;
}
/* }}} */

ZEND_API int zend_ini_deactivate(TSRMLS_D) /* {{{ */
{
	if (EG(modified_ini_directives)) {
		zend_hash_apply(EG(modified_ini_directives), zend_restore_ini_entry_wrapper TSRMLS_CC);
		zend_hash_destroy(EG(modified_ini_directives));
		FREE_HASHTABLE(EG(modified_ini_directives));
		EG(modified_ini_directives) = NULL;
	}
	return SUCCESS;
}
/* }}} */

#ifdef ZTS
static void copy_ini_entry(zval *zv) /* {{{ */
{
	zend_ini_entry *old_entry = (zend_ini_entry*)Z_PTR_P(zv);
	zend_ini_entry *new_entry = pemalloc(sizeof(zend_ini_entry), 1);

	Z_PTR_P(zv) = new_entry;
	memcpy(new_entry, old_entry, sizeof(zend_ini_entry));
	if (old_entry->name) {
		new_entry->name = zend_string_init(old_entry->name->val, old_entry->name->len, 1);
	}
	if (old_entry->value) {
		new_entry->value = zend_string_init(old_entry->value->val, old_entry->value->len, 1);
	}
	if (old_entry->orig_value) {
		new_entry->orig_value = zend_string_init(old_entry->orig_value->val, old_entry->orig_value->len, 1);
	}
}
/* }}} */

ZEND_API int zend_copy_ini_directives(TSRMLS_D) /* {{{ */
{
	EG(modified_ini_directives) = NULL;
	EG(error_reporting_ini_entry) = NULL;
	EG(ini_directives) = (HashTable *) malloc(sizeof(HashTable));
	zend_hash_init_ex(EG(ini_directives), registered_zend_ini_directives->nNumOfElements, NULL, free_ini_entry, 1, 0);
	zend_hash_copy(EG(ini_directives), registered_zend_ini_directives, copy_ini_entry);
	return SUCCESS;
}
/* }}} */
#endif

static int ini_key_compare(const void *a, const void *b TSRMLS_DC) /* {{{ */
{
	const Bucket *f;
	const Bucket *s;

	f = (const Bucket *) a;
	s = (const Bucket *) b;

	if (!f->key && !s->key) { /* both numeric */
		return ZEND_NORMALIZE_BOOL(f->h - s->h);
	} else if (!f->key) { /* f is numeric, s is not */
		return -1;
	} else if (!s->key) { /* s is numeric, f is not */
		return 1;
	} else { /* both strings */
		return zend_binary_strcasecmp(f->key->val, f->key->len, s->key->val, s->key->len);
	}
}
/* }}} */

ZEND_API void zend_ini_sort_entries(TSRMLS_D) /* {{{ */
{
	zend_hash_sort(EG(ini_directives), zend_qsort, ini_key_compare, 0 TSRMLS_CC);
}
/* }}} */

/*
 * Registration / unregistration
 */
ZEND_API int zend_register_ini_entries(const zend_ini_entry_def *ini_entry, int module_number TSRMLS_DC) /* {{{ */
{
	zend_ini_entry *p;
	zval *default_value;
	HashTable *directives = registered_zend_ini_directives;

#ifdef ZTS
	/* if we are called during the request, eg: from dl(),
	 * then we should not touch the global directives table,
	 * and should update the per-(request|thread) version instead.
	 * This solves two problems: one is that ini entries for dl()'d
	 * extensions will now work, and the second is that updating the
	 * global hash here from dl() is not mutex protected and can
	 * lead to death.
	 */
	if (directives != EG(ini_directives)) {
		directives = EG(ini_directives);
	}
#endif

	while (ini_entry->name) {
		p = pemalloc(sizeof(zend_ini_entry), 1);
		p->name = zend_string_init(ini_entry->name, ini_entry->name_length, 1);
		p->on_modify = ini_entry->on_modify;
		p->mh_arg1 = ini_entry->mh_arg1;
		p->mh_arg2 = ini_entry->mh_arg2;
		p->mh_arg3 = ini_entry->mh_arg3;
		p->value = NULL;
		p->orig_value = NULL;
		p->displayer = ini_entry->displayer;
		p->modifiable = ini_entry->modifiable;

		p->orig_modifiable = 0;
		p->modified = 0;
		p->module_number = module_number;

		if (zend_hash_add_ptr(directives, p->name, (void*)p) == NULL) {
			if (p->name) {
				zend_string_release(p->name);
			}
			zend_unregister_ini_entries(module_number TSRMLS_CC);
			return FAILURE;
		}
		if (((default_value = zend_get_configuration_directive(p->name)) != NULL) &&
            (!p->on_modify || p->on_modify(p, Z_STR_P(default_value), p->mh_arg1, p->mh_arg2, p->mh_arg3, ZEND_INI_STAGE_STARTUP TSRMLS_CC) == SUCCESS)) {
			
			p->value = zend_string_copy(Z_STR_P(default_value));
		} else {
			p->value = ini_entry->value ?
				zend_string_init(ini_entry->value, ini_entry->value_length, 1) : NULL;

			if (p->on_modify) {
				p->on_modify(p, p->value, p->mh_arg1, p->mh_arg2, p->mh_arg3, ZEND_INI_STAGE_STARTUP TSRMLS_CC);
			}
		}
		ini_entry++;
	}
	return SUCCESS;
}
/* }}} */

ZEND_API void zend_unregister_ini_entries(int module_number TSRMLS_DC) /* {{{ */
{
	zend_hash_apply_with_argument(registered_zend_ini_directives, zend_remove_ini_entries, (void *) &module_number TSRMLS_CC);
}
/* }}} */

#ifdef ZTS
static int zend_ini_refresh_cache(zval *el, void *arg TSRMLS_DC) /* {{{ */
{
	zend_ini_entry *p = (zend_ini_entry *)Z_PTR_P(el);
	int stage = (int)(zend_intptr_t)arg;

	if (p->on_modify) {
		p->on_modify(p, p->value, p->mh_arg1, p->mh_arg2, p->mh_arg3, stage TSRMLS_CC);
	}
	return 0;
}
/* }}} */

ZEND_API void zend_ini_refresh_caches(int stage TSRMLS_DC) /* {{{ */
{
	zend_hash_apply_with_argument(EG(ini_directives), zend_ini_refresh_cache, (void *)(zend_intptr_t) stage TSRMLS_CC);
}
/* }}} */
#endif

ZEND_API int zend_alter_ini_entry(zend_string *name, zend_string *new_value, int modify_type, int stage) /* {{{ */
{
	TSRMLS_FETCH();

	return zend_alter_ini_entry_ex(name, new_value, modify_type, stage, 0 TSRMLS_CC);
}
/* }}} */

ZEND_API int zend_alter_ini_entry_chars(zend_string *name, const char *value, size_t value_length, int modify_type, int stage) /* {{{ */
{
    int ret;
    zend_string *new_value;
	TSRMLS_FETCH();

	new_value = zend_string_init(value, value_length, stage != ZEND_INI_STAGE_RUNTIME);
	ret = zend_alter_ini_entry_ex(name, new_value, modify_type, stage, 0 TSRMLS_CC);
	zend_string_release(new_value);
	return ret;
}
/* }}} */

ZEND_API int zend_alter_ini_entry_chars_ex(zend_string *name, const char *value, size_t value_length, int modify_type, int stage, int force_change TSRMLS_DC) /* {{{ */
{
    int ret;
    zend_string *new_value;

	new_value = zend_string_init(value, value_length, stage != ZEND_INI_STAGE_RUNTIME);
	ret = zend_alter_ini_entry_ex(name, new_value, modify_type, stage, force_change TSRMLS_CC);
	zend_string_release(new_value);
	return ret;
}
/* }}} */

ZEND_API int zend_alter_ini_entry_ex(zend_string *name, zend_string *new_value, int modify_type, int stage, int force_change TSRMLS_DC) /* {{{ */
{
	zend_ini_entry *ini_entry;
	zend_string *duplicate;
	zend_bool modifiable;
	zend_bool modified;

	if ((ini_entry = zend_hash_find_ptr(EG(ini_directives), name)) == NULL) {
		return FAILURE;
	}

	modifiable = ini_entry->modifiable;
	modified = ini_entry->modified;

	if (stage == ZEND_INI_STAGE_ACTIVATE && modify_type == ZEND_INI_SYSTEM) {
		ini_entry->modifiable = ZEND_INI_SYSTEM;
	}

	if (!force_change) {
		if (!(ini_entry->modifiable & modify_type)) {
			return FAILURE;
		}
	}

	if (!EG(modified_ini_directives)) {
		ALLOC_HASHTABLE(EG(modified_ini_directives));
		zend_hash_init(EG(modified_ini_directives), 8, NULL, NULL, 0);
	}
	if (!modified) {
		ini_entry->orig_value = ini_entry->value;
		ini_entry->orig_modifiable = modifiable;
		ini_entry->modified = 1;
		zend_hash_add_ptr(EG(modified_ini_directives), name, ini_entry);
	}

	duplicate = zend_string_copy(new_value);

	if (!ini_entry->on_modify
		|| ini_entry->on_modify(ini_entry, duplicate, ini_entry->mh_arg1, ini_entry->mh_arg2, ini_entry->mh_arg3, stage TSRMLS_CC) == SUCCESS) {
		if (modified && ini_entry->orig_value != ini_entry->value) { /* we already changed the value, free the changed value */
			zend_string_release(ini_entry->value);
		}
		ini_entry->value = duplicate;
	} else {
		zend_string_release(duplicate);
		return FAILURE;
	}

	return SUCCESS;
}
/* }}} */

ZEND_API int zend_restore_ini_entry(zend_string *name, int stage) /* {{{ */
{
	zend_ini_entry *ini_entry;
	TSRMLS_FETCH();

	if ((ini_entry = zend_hash_find_ptr(EG(ini_directives), name)) == NULL ||
		(stage == ZEND_INI_STAGE_RUNTIME && (ini_entry->modifiable & ZEND_INI_USER) == 0)) {
		return FAILURE;
	}

	if (EG(modified_ini_directives)) {
		if (zend_restore_ini_entry_cb(ini_entry, stage TSRMLS_CC) == 0) {
			zend_hash_del(EG(modified_ini_directives), name);
		} else {
			return FAILURE;
		}
	}

	return SUCCESS;
}
/* }}} */

ZEND_API int zend_ini_register_displayer(char *name, uint name_length, void (*displayer)(zend_ini_entry *ini_entry, int type)) /* {{{ */
{
	zend_ini_entry *ini_entry;

	ini_entry = zend_hash_str_find_ptr(registered_zend_ini_directives, name, name_length);	
	if (ini_entry == NULL) {
		return FAILURE;
	}

	ini_entry->displayer = displayer;
	return SUCCESS;
}
/* }}} */

/*
 * Data retrieval
 */

ZEND_API zend_long zend_ini_long(char *name, uint name_length, int orig) /* {{{ */
{
	zend_ini_entry *ini_entry;
	TSRMLS_FETCH();

	ini_entry = zend_hash_str_find_ptr(EG(ini_directives), name, name_length);
	if (ini_entry) {
		if (orig && ini_entry->modified) {
			return (ini_entry->orig_value ? ZEND_STRTOL(ini_entry->orig_value->val, NULL, 0) : 0);
		} else {
			return (ini_entry->value      ? ZEND_STRTOL(ini_entry->value->val, NULL, 0)      : 0);
		}
	}

	return 0;
}
/* }}} */

ZEND_API double zend_ini_double(char *name, uint name_length, int orig) /* {{{ */
{
	zend_ini_entry *ini_entry;
	TSRMLS_FETCH();

	ini_entry = zend_hash_str_find_ptr(EG(ini_directives), name, name_length);
	if (ini_entry) {
		if (orig && ini_entry->modified) {
			return (double) (ini_entry->orig_value ? zend_strtod(ini_entry->orig_value->val, NULL) : 0.0);
		} else {
			return (double) (ini_entry->value      ? zend_strtod(ini_entry->value->val, NULL)      : 0.0);
		}
	}

	return 0.0;
}
/* }}} */

ZEND_API char *zend_ini_string_ex(char *name, uint name_length, int orig, zend_bool *exists) /* {{{ */
{
	zend_ini_entry *ini_entry;
	TSRMLS_FETCH();

	ini_entry = zend_hash_str_find_ptr(EG(ini_directives), name, name_length);
	if (ini_entry) {
		if (exists) {
			*exists = 1;
		}

		if (orig && ini_entry->modified) {
			return ini_entry->orig_value ? ini_entry->orig_value->val : NULL;
		} else {
			return ini_entry->value ? ini_entry->value->val : NULL;
		}
	} else {
		if (exists) {
			*exists = 0;
		}
		return NULL;
	}
}
/* }}} */

ZEND_API char *zend_ini_string(char *name, uint name_length, int orig) /* {{{ */
{
	zend_bool exists = 1;
	char *return_value;

	return_value = zend_ini_string_ex(name, name_length, orig, &exists);
	if (!exists) {
		return NULL;
	} else if (!return_value) {
		return_value = "";
	}
	return return_value;
}
/* }}} */

#if TONY_20070307
static void zend_ini_displayer_cb(zend_ini_entry *ini_entry, int type) /* {{{ */
{
	if (ini_entry->displayer) {
		ini_entry->displayer(ini_entry, type);
	} else {
		char *display_string;
		uint display_string_length;

		if (type == ZEND_INI_DISPLAY_ORIG && ini_entry->modified) {
			if (ini_entry->orig_value) {
				display_string = ini_entry->orig_value;
				display_string_length = ini_entry->orig_value_length;
			} else {
				if (zend_uv.html_errors) {
					display_string = NO_VALUE_HTML;
					display_string_length = sizeof(NO_VALUE_HTML) - 1;
				} else {
					display_string = NO_VALUE_PLAINTEXT;
					display_string_length = sizeof(NO_VALUE_PLAINTEXT) - 1;
				}
			}
		} else if (ini_entry->value && ini_entry->value[0]) {
			display_string = ini_entry->value;
			display_string_length = ini_entry->value_length;
		} else {
			if (zend_uv.html_errors) {
				display_string = NO_VALUE_HTML;
				display_string_length = sizeof(NO_VALUE_HTML) - 1;
			} else {
				display_string = NO_VALUE_PLAINTEXT;
				display_string_length = sizeof(NO_VALUE_PLAINTEXT) - 1;
			}
		}
		ZEND_WRITE(display_string, display_string_length);
	}
}
/* }}} */
#endif

ZEND_INI_DISP(zend_ini_boolean_displayer_cb) /* {{{ */
{
	int value;
	zend_string *tmp_value;

	if (type == ZEND_INI_DISPLAY_ORIG && ini_entry->modified) {
		tmp_value = (ini_entry->orig_value ? ini_entry->orig_value : NULL );
	} else if (ini_entry->value) {
		tmp_value = ini_entry->value;
	} else {
		tmp_value = NULL;
	}

	if (tmp_value) {
		if (tmp_value->len == 4 && strcasecmp(tmp_value->val, "true") == 0) {
			value = 1;
		} else if (tmp_value->len == 3 && strcasecmp(tmp_value->val, "yes") == 0) {
			value = 1;
		} else if (tmp_value->len == 2 && strcasecmp(tmp_value->val, "on") == 0) {
			value = 1;
		} else {
			value = atoi(tmp_value->val);
		}
	} else {
		value = 0;
	}

	if (value) {
		ZEND_PUTS("On");
	} else {
		ZEND_PUTS("Off");
	}
}
/* }}} */

ZEND_INI_DISP(zend_ini_color_displayer_cb) /* {{{ */
{
	char *value;

	if (type == ZEND_INI_DISPLAY_ORIG && ini_entry->modified) {
		value = ini_entry->orig_value->val;
	} else if (ini_entry->value) {
		value = ini_entry->value->val;
	} else {
		value = NULL;
	}
	if (value) {
		if (zend_uv.html_errors) {
			zend_printf("<font style=\"color: %s\">%s</font>", value, value);
		} else {
			ZEND_PUTS(value);
		}
	} else {
		if (zend_uv.html_errors) {
			ZEND_PUTS(NO_VALUE_HTML);
		} else {
			ZEND_PUTS(NO_VALUE_PLAINTEXT);
		}
	}
}
/* }}} */

ZEND_INI_DISP(display_link_numbers) /* {{{ */
{
	char *value;

	if (type == ZEND_INI_DISPLAY_ORIG && ini_entry->modified) {
		value = ini_entry->orig_value->val;
	} else if (ini_entry->value) {
		value = ini_entry->value->val;
	} else {
		value = NULL;
	}

	if (value) {
		if (atoi(value) == -1) {
			ZEND_PUTS("Unlimited");
		} else {
			zend_printf("%s", value);
		}
	}
}
/* }}} */

/* Standard message handlers */
ZEND_API ZEND_INI_MH(OnUpdateBool) /* {{{ */
{
	zend_bool *p;
#ifndef ZTS
	char *base = (char *) mh_arg2;
#else
	char *base;

	base = (char *) ts_resource(*((int *) mh_arg2));
#endif

	p = (zend_bool *) (base+(size_t) mh_arg1);

	if (new_value->len == 2 && strcasecmp("on", new_value->val) == 0) {
		*p = (zend_bool) 1;
	}
	else if (new_value->len == 3 && strcasecmp("yes", new_value->val) == 0) {
		*p = (zend_bool) 1;
	}
	else if (new_value->len == 4 && strcasecmp("true", new_value->val) == 0) {
		*p = (zend_bool) 1;
	}
	else {
		*p = (zend_bool) atoi(new_value->val);
	}
	return SUCCESS;
}
/* }}} */

ZEND_API ZEND_INI_MH(OnUpdateLong) /* {{{ */
{
	zend_long *p;
#ifndef ZTS
	char *base = (char *) mh_arg2;
#else
	char *base;

	base = (char *) ts_resource(*((int *) mh_arg2));
#endif

	p = (zend_long *) (base+(size_t) mh_arg1);

	*p = zend_atol(new_value->val, new_value->len);
	return SUCCESS;
}
/* }}} */

ZEND_API ZEND_INI_MH(OnUpdateLongGEZero) /* {{{ */
{
	zend_long *p, tmp;
#ifndef ZTS
	char *base = (char *) mh_arg2;
#else
	char *base;

	base = (char *) ts_resource(*((int *) mh_arg2));
#endif

	tmp = zend_atol(new_value->val, new_value->len);
	if (tmp < 0) {
		return FAILURE;
	}

	p = (zend_long *) (base+(size_t) mh_arg1);
	*p = tmp;

	return SUCCESS;
}
/* }}} */

ZEND_API ZEND_INI_MH(OnUpdateReal) /* {{{ */
{
	double *p;
#ifndef ZTS
	char *base = (char *) mh_arg2;
#else
	char *base;

	base = (char *) ts_resource(*((int *) mh_arg2));
#endif

	p = (double *) (base+(size_t) mh_arg1);

	*p = zend_strtod(new_value->val, NULL);
	return SUCCESS;
}
/* }}} */

ZEND_API ZEND_INI_MH(OnUpdateString) /* {{{ */
{
	char **p;
#ifndef ZTS
	char *base = (char *) mh_arg2;
#else
	char *base;

	base = (char *) ts_resource(*((int *) mh_arg2));
#endif

	p = (char **) (base+(size_t) mh_arg1);

	*p = new_value ? new_value->val : NULL;
	return SUCCESS;
}
/* }}} */

ZEND_API ZEND_INI_MH(OnUpdateStringUnempty) /* {{{ */
{
	char **p;
#ifndef ZTS
	char *base = (char *) mh_arg2;
#else
	char *base;

	base = (char *) ts_resource(*((int *) mh_arg2));
#endif

	if (new_value && !new_value->val[0]) {
		return FAILURE;
	}

	p = (char **) (base+(size_t) mh_arg1);

	*p = new_value ? new_value->val : NULL;
	return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */

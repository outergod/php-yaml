/**
 * YAML parser binding part implementation
 *
 * Copyright (C) 2007  Ryusuke SEKIYAMA. All rights reserved.
 * Copyright (C) 2008  Alexander Kahl
 *
 * This file is part of php-yaml.
 * php-yaml is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * php-yaml is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with php-yaml.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * @package     php-yaml
 * @author      Ryusuke SEKIYAMA <rsky0711@gmail.com>
 * @author      Alexander Kahl <e-user@gmx.net>
 * @copyright   2007 Ryusuke SEKIYAMA
 * @copyright   2008 Alexander Kahl
 * @license     http://www.gnu.org/licenses/lgpl.html  LGPLv3+
 * @version     $Id: parser.c 22 2008-09-20 14:24:47Z e-user $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <yaml.h>
#include <ext/standard/php_smart_str.h>
#include <ext/standard/php_var.h>
#include <ext/standard/base64.h>
#include <ext/standard/basic_functions.h>
#include "php_yaml.h"
#include "zval_refcount.h" /* for PHP < 5.3 */
#include "parser.h"

/* {{{ internal function prototypes */
static char *
php_yaml_convert_to_char(zval *zv TSRMLS_DC);

static int
php_yaml_apply_filter(zval **zpp, yaml_event_t event, HashTable *callbacks TSRMLS_DC);

static int
php_yaml_scalar_is_null(const char *value, size_t length, yaml_event_t event);

static int
php_yaml_scalar_is_bool(const char *value, size_t length, yaml_event_t event);

static int
php_yaml_scalar_is_numeric(const char *value, size_t length,
		long *lval, double *dval, char **str);

static int
php_yaml_scalar_is_timestamp(const char *value, size_t length);

static long
php_yaml_eval_sexagesimal_l(long lval, char *sg, char *eos);

static double
php_yaml_eval_sexagesimal_d(double dval, char *sg, char *eos);

static int
php_yaml_eval_timestamp(zval **zpp, char *ts, int ts_len TSRMLS_DC);

static void
php_yaml_print_parser_error (yaml_parser_t *parser TSRMLS_DC);
/* }}} */

/* {{{ php_yaml_convert_to_char() */
static char *
php_yaml_convert_to_char(zval *zv TSRMLS_DC)
{
	char *str = NULL;

	switch (Z_TYPE_P(zv)) {
	  case IS_BOOL:
		if (Z_BVAL_P(zv)) {
			str = estrndup("1", 1);
		} else {
			str = estrndup("", 0);
		}
		break;
	  case IS_DOUBLE:
		{
			char buf[64] = {'\0'};
			(void)snprintf(buf, 64, "%G", Z_DVAL_P(zv));
			str = estrdup(buf);
		}
		break;
	  case IS_LONG:
		{
			char buf[32] = {'\0'};
			(void)snprintf(buf, 32, "%ld", Z_LVAL_P(zv));
			str = estrdup(buf);
		}
		break;
	  case IS_NULL:
		str = estrndup("", 0);
		break;
	  case IS_STRING:
		str = estrndup(Z_STRVAL_P(zv), Z_STRLEN_P(zv));
		break;
#ifdef IS_UNICODE
	  case IS_UNICODE:
		{
			int len;
			UErrorCode status = U_ZERO_ERROR;

			zend_unicode_to_string_ex(UG(utf8_conv), &str, &len, Z_USTRVAL_P(zv), Z_USTRLEN_P(zv), &status);
			if (U_FAILURE(status)) {
				if (str != NULL) {
					efree(str);
					str = NULL;
				}
			}
		}
		break;
#endif
#ifdef ZEND_ENGINE_2
	  case IS_OBJECT:
		{
			zval tmp;

			if (zend_std_cast_object_tostring(
#if PHP_MAJOR_VERSION >= 6
				zv, &tmp, IS_STRING, UG(utf8_conv) TSRMLS_CC
#elif PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 2
				zv, &tmp, IS_STRING TSRMLS_CC
#else
				zv, &tmp, IS_STRING, 0 TSRMLS_CC
#endif
			) == SUCCESS) {
				str = estrndup(Z_STRVAL(tmp), Z_STRLEN(tmp));
				zval_dtor(&tmp);
				return str;
			}
		}
#endif
	  default:
		{
			php_serialize_data_t var_hash;
			smart_str buf = {0};

			PHP_VAR_SERIALIZE_INIT(var_hash);
			php_var_serialize(&buf, &zv, &var_hash TSRMLS_CC);
			PHP_VAR_SERIALIZE_DESTROY(var_hash);

			str = buf.c;
		}
	}

	if (str == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING,
				"Failed to convert %s to string", zend_zval_type_name(zv));
	}

	return str;
}
/* }}} */

/* {{{ php_yaml_read_impl() */
zval *
php_yaml_read_impl(yaml_parser_t *parser, yaml_event_t *parent,
		zval *aliases, zval *zv, long *ndocs,
		eval_scalar_func_t eval_func, HashTable *callbacks TSRMLS_DC)
{
	zval *retval = NULL;
	yaml_event_t event = {0};
	char *key = NULL;
	int code = Y_PARSER_CONTINUE;

	if (zv != NULL) {
		retval = zv;
	} else {
		MAKE_STD_ZVAL(retval);
		array_init(retval);
#ifdef IS_UNICODE
		Z_ARRVAL_P(retval)->unicode = UG(unicode);
#endif
	}

	do {
		zval *tmp_p = NULL;
		zval **tmp_pp = NULL;

		if (!yaml_parser_parse(parser, &event)) {
			php_yaml_print_parser_error (parser TSRMLS_CC);
			code = Y_PARSER_FAILURE;
			break;
		}

		switch (event.type) {
		  case YAML_NO_EVENT:
		  case YAML_STREAM_START_EVENT:
			break;

		  case YAML_DOCUMENT_START_EVENT:
			{
				zval *a = NULL;
				MAKE_STD_ZVAL(a);
				array_init(a);
#ifdef IS_UNICODE
				Z_ARRVAL_P(a)->unicode = UG(unicode);
#endif
				if (php_yaml_read_impl(parser, &event, a, retval, ndocs, eval_func, callbacks TSRMLS_CC) == NULL) {
					code = Y_PARSER_FAILURE;
				}
				zval_ptr_dtor(&a);
			}
			(*ndocs)++;
			break;

		  case YAML_SEQUENCE_START_EVENT:
		  case YAML_MAPPING_START_EVENT:
			MAKE_STD_ZVAL(tmp_p);
			array_init(tmp_p);
#ifdef IS_UNICODE
			Z_ARRVAL_P(tmp_p)->unicode = UG(unicode);
#endif

			if (event.type == YAML_SEQUENCE_START_EVENT) {
				if (event.data.sequence_start.anchor != NULL) {
					Z_ADDREF_P(tmp_p);
					Z_SET_ISREF_P(tmp_p);
					add_assoc_zval(aliases, (char *)event.data.sequence_start.anchor, tmp_p);
				}
			} else if (event.type == YAML_MAPPING_START_EVENT) {
				if (event.data.mapping_start.anchor != NULL) {
					Z_ADDREF_P(tmp_p);
					Z_SET_ISREF_P(tmp_p);
					add_assoc_zval(aliases, (char *)event.data.mapping_start.anchor, tmp_p);
				}
			}

			tmp_p = php_yaml_read_impl(parser, &event, aliases, tmp_p, ndocs, eval_func, callbacks TSRMLS_CC);
			if (tmp_p == NULL) {
				code = Y_PARSER_FAILURE;
				break;
			}

			if (callbacks != NULL) {
				if (php_yaml_apply_filter(&tmp_p, event, callbacks TSRMLS_CC) == Y_FILTER_FAILURE) {
					zval_ptr_dtor(&tmp_p);
					code = Y_PARSER_FAILURE;
					break;
				}
			}

			if (parent->type == YAML_MAPPING_START_EVENT) {
				if (key == NULL) {
					key = php_yaml_convert_to_char(tmp_p TSRMLS_CC);
					if (key == NULL) {
						zval_ptr_dtor(&tmp_p);
						code = Y_PARSER_FAILURE;
						break;
					}
					/* assign tmp_p to the alias-storage
					   tmp_p will be freed in its destructor */
					add_next_index_zval(aliases, tmp_p);
				} else {
					add_assoc_zval(retval, key, tmp_p);
					efree(key);
					key = NULL;
				}
			} else {
				add_next_index_zval(retval, tmp_p);
			}
			break;

		  case YAML_STREAM_END_EVENT:
		  case YAML_DOCUMENT_END_EVENT:
		  case YAML_SEQUENCE_END_EVENT:
		  case YAML_MAPPING_END_EVENT:
			code = Y_PARSER_SUCCESS;
			break;

		  case YAML_ALIAS_EVENT:
			if (zend_hash_find(Z_ARRVAL_P(aliases),
					(char *)event.data.alias.anchor,
					(uint)strlen((char *)event.data.alias.anchor) + 1,
					(void **)&tmp_pp) == SUCCESS)
			{
				if (parent->type == YAML_MAPPING_START_EVENT) {
					if (key == NULL) {
						key = php_yaml_convert_to_char(*tmp_pp TSRMLS_CC);
						if (key == NULL) {
							code = Y_PARSER_FAILURE;
							break;
						}
					} else {
						Z_ADDREF_PP(tmp_pp);
						add_assoc_zval(retval, key, *tmp_pp);
						efree(key);
						key = NULL;
					}
				} else {
					Z_ADDREF_PP(tmp_pp);
					add_next_index_zval(retval, *tmp_pp);
				}
			} else {
				php_error_docref(NULL TSRMLS_CC, E_WARNING,
						"alias %s is not registered", (char *)event.data.alias.anchor);
				code = Y_PARSER_FAILURE;
			}
			break;

		  case YAML_SCALAR_EVENT:
			if (parent->type == YAML_MAPPING_START_EVENT) {
				if (key == NULL) {
					key = estrndup((char *)event.data.scalar.value, event.data.scalar.length);
				} else {
					tmp_p = eval_func(event, callbacks TSRMLS_CC);
					if (tmp_p == NULL) {
						code = Y_PARSER_FAILURE;
						break;
					}
					add_assoc_zval(retval, key, tmp_p);
					efree(key);
					key = NULL;
				}
			} else {
				tmp_p = eval_func(event, callbacks TSRMLS_CC);
				if (tmp_p == NULL) {
					code = Y_PARSER_FAILURE;
					break;
				}
				add_next_index_zval(retval, tmp_p);
			}

			if (event.data.scalar.anchor != NULL) {
				if (tmp_p == NULL) {
					add_assoc_string(aliases, (char *)event.data.scalar.anchor, key, 1);
				} else {
					Z_ADDREF_P(tmp_p);
					Z_SET_ISREF_P(tmp_p);
					add_assoc_zval(aliases, (char *)event.data.scalar.anchor, tmp_p);
				}
			}
			break;

		  default:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "unknown event type");
			code = Y_PARSER_FAILURE;
		}

		yaml_event_delete(&event);
	} while (code == Y_PARSER_CONTINUE);

	if (key != NULL) {
		if (code == Y_PARSER_SUCCESS) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "invalid mapping structure");
		}
		efree(key);
		code = Y_PARSER_FAILURE;
	}

	if (code == Y_PARSER_FAILURE) {
		*ndocs = -1;
		if (zv == NULL && retval != NULL) {
			zval_ptr_dtor(&retval);
		}
		return NULL;
	}

	return retval;
}
/* }}} */

/* {{{ php_yaml_read_partial() */
zval *
php_yaml_read_partial(yaml_parser_t *parser, long pos, long *ndocs,
		eval_scalar_func_t eval_func, HashTable *callbacks TSRMLS_DC)
{
	zval *retval = NULL;
	yaml_event_t event = {0};
	int code = Y_PARSER_CONTINUE;

	do {
		if (!yaml_parser_parse(parser, &event)) {
			php_yaml_print_parser_error (parser TSRMLS_CC);
			code = Y_PARSER_FAILURE;
			break;
		}

		if (event.type == YAML_DOCUMENT_START_EVENT) {
			if (*ndocs == pos) {
				zval *tmp_p = NULL;
				zval *aliases = NULL;
				MAKE_STD_ZVAL(aliases);
				array_init(aliases);
#ifdef IS_UNICODE
				Z_ARRVAL_P(aliases)->unicode = UG(unicode);
#endif
				tmp_p = php_yaml_read_impl(parser, &event, aliases, NULL, ndocs, eval_func, callbacks TSRMLS_CC);
				if (tmp_p == NULL) {
					code = Y_PARSER_FAILURE;
				} else {
					zval **tmp_pp = NULL;
					if (zend_hash_index_find(Z_ARRVAL_P(tmp_p), 0, (void **)&tmp_pp) == SUCCESS) {
						MAKE_STD_ZVAL(retval);
						ZVAL_ZVAL(retval, *tmp_pp, 1, 0);
					}
					zval_ptr_dtor(&tmp_p);
				}
				zval_ptr_dtor(&aliases);
			}
			(*ndocs)++;
		} else if (event.type == YAML_STREAM_END_EVENT) {
			code = Y_PARSER_SUCCESS;
		}

		yaml_event_delete(&event);
	} while (code == Y_PARSER_CONTINUE);

	if (code == Y_PARSER_FAILURE) {
		*ndocs = -1;
		if (retval != NULL) {
			zval_ptr_dtor(&retval);
		}
		return NULL;
	}

	return retval;
}
/* }}} */

/* {{{ php_yaml_apply_filter() */
static int
php_yaml_apply_filter(zval **zpp, yaml_event_t event, HashTable *callbacks TSRMLS_DC)
{
	char *tag = NULL;
	zval **callback = NULL;

	/* detect event type and get tag */
	switch (event.type) {
	  case YAML_SEQUENCE_START_EVENT:
		if (!event.data.sequence_start.implicit) {
			tag = (char *)event.data.sequence_start.tag;
		}
		break;
	  case YAML_MAPPING_START_EVENT:
		if (!event.data.mapping_start.implicit) {
			tag = (char *)event.data.mapping_start.tag;
		}
		break;
    default: /* this shouldn't happen */
        break;
	}

	if (tag == NULL) {
		return Y_FILTER_NONE;
	}

	/* find and apply the filter function */
	if (zend_hash_find(callbacks, tag, strlen(tag) + 1, (void **)&callback) == SUCCESS) {
		zval **argv[] = { zpp };
		zval *retval = NULL;

		if (call_user_function_ex(EG(function_table), NULL, *callback,
				&retval, 1, argv, 0, NULL TSRMLS_CC) == FAILURE ||
			retval == NULL)
		{
			php_error_docref(NULL TSRMLS_CC, E_WARNING,
					"Failed to apply filter for tag '%s'"
					" with user defined function", tag);
			return Y_FILTER_FAILURE;
		} else {
			zval_dtor(*zpp);
			ZVAL_ZVAL(*zpp, retval, 1, 1);
			return Y_FILTER_SUCCESS;
		}
	} else {
		return Y_FILTER_NONE;
	}
}
/* }}} */

#define SCALAR_TAG_IS(event, name) \
	!strcmp((const char *)event.data.scalar.tag, "tag:yaml.org,2002:" name)

#define IS_NOT_IMPLICIT_AND_TAG_IS(event, name) \
	(!event.data.scalar.quoted_implicit && !event.data.scalar.plain_implicit && SCALAR_TAG_IS(event, name))

#define IS_NOT_QUOTED_OR_TAG_IS(event, name) \
	(!event.data.scalar.quoted_implicit && (event.data.scalar.plain_implicit || SCALAR_TAG_IS(event, name)))

/* {{{ php_yaml_eval_scalar()
 * All YAML scalar types found at http://yaml.org/type/index.html.
 */
zval *
php_yaml_eval_scalar(yaml_event_t event, HashTable *callbacks TSRMLS_DC)
{
	zval *tmp = NULL;
	char *value = (char *)event.data.scalar.value;
	size_t length = event.data.scalar.length;
	int flags = 0;

	MAKE_STD_ZVAL(tmp);
	ZVAL_NULL(tmp);

	/* check for null */
	if (php_yaml_scalar_is_null(value, length, event)) {
		return tmp;
	}

	/* check for bool */
	if ((flags = php_yaml_scalar_is_bool(value, length, event)) != -1) {
		ZVAL_BOOL(tmp, (zend_bool)flags);
		return tmp;
	}

	/* check for numeric (int or float) */
	if (!event.data.scalar.quoted_implicit && (event.data.scalar.plain_implicit ||
		SCALAR_TAG_IS(event, "int") || SCALAR_TAG_IS(event, "float")))
	{
		long lval = 0;
		double dval = 0.0;

		flags = php_yaml_scalar_is_numeric(value, length, &lval, &dval, NULL);
		if (flags != Y_SCALAR_IS_NOT_NUMERIC) {
			if (flags & Y_SCALAR_IS_FLOAT) {
				ZVAL_DOUBLE(tmp, dval);
			} else {
				ZVAL_LONG(tmp, lval);
			}
			if (event.data.scalar.plain_implicit) {
				/* pass */
			} else if (SCALAR_TAG_IS(event, "float") && (flags & Y_SCALAR_IS_INT)) {
				convert_to_double(tmp);
			} else if (SCALAR_TAG_IS(event, "int") && (flags & Y_SCALAR_IS_FLOAT)) {
				convert_to_long(tmp);
			}
			return tmp;
		} else if (IS_NOT_IMPLICIT_AND_TAG_IS(event, "float")) {
			ZVAL_STRINGL(tmp, value, length, 1);
			convert_to_double(tmp);
			return tmp;
		} else if (IS_NOT_IMPLICIT_AND_TAG_IS(event, "int")) {
			ZVAL_STRINGL(tmp, value, length, 1);
			convert_to_long(tmp);
			return tmp;
		}
	}

	/* check for timestamp */
	if (event.data.scalar.plain_implicit || event.data.scalar.quoted_implicit) {
		if (php_yaml_scalar_is_timestamp(value, length)) {
			if (php_yaml_eval_timestamp(&tmp, value, (int)length TSRMLS_CC) == FAILURE) {
				zval_ptr_dtor(&tmp);
				return NULL;
			}
			return tmp;
		}
	} else if (SCALAR_TAG_IS(event, "timestamp")) {
		if (php_yaml_eval_timestamp(&tmp, value, (int)length TSRMLS_CC) == FAILURE) {
			zval_ptr_dtor(&tmp);
			return NULL;
		}
		return tmp;
	}

	/* check for binary */
	if (IS_NOT_IMPLICIT_AND_TAG_IS(event, "binary")) {
		if (YAML_G(decode_binary)) {
			unsigned char *data = NULL;
			int data_len = 0;

			data = php_base64_decode((const unsigned char *)value, (int)length, &data_len);
			if (data == NULL) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode binary");
				ZVAL_NULL(tmp);
			} else {
				ZVAL_STRINGL(tmp, (char *)data, data_len, 0);
			}
		} else {
			ZVAL_STRINGL(tmp, value, length, 1);
		}
		return tmp;
	}

	/* others (treat as a string) */
#ifdef IS_UNICODE
	ZVAL_U_STRINGL(UG(utf8_conv), tmp, value, length, ZSTR_DUPLICATE);
#else
	ZVAL_STRINGL(tmp, value, length, 1);
#endif

	return tmp;
}
/* }}} */

/* {{{ php_yaml_eval_scalar_with_callbacks() */
zval *
php_yaml_eval_scalar_with_callbacks(yaml_event_t event, HashTable *callbacks TSRMLS_DC)
{
	char *tag = (char *)event.data.scalar.tag;
	zval **callback = NULL;

	/* find and apply the evaluation function */
	if (!event.data.scalar.quoted_implicit && !event.data.scalar.plain_implicit &&
			zend_hash_find(callbacks, tag, strlen(tag) + 1, (void **)&callback) == SUCCESS)
	{
		zval **argv[] = { NULL };
		zval *arg = NULL;
		zval *retval = NULL;

		MAKE_STD_ZVAL(arg);
		ZVAL_STRINGL(arg, (char *)event.data.scalar.value, event.data.scalar.length, 1);
		argv[0] = &arg;

		if (call_user_function_ex(EG(function_table), NULL, *callback,
				&retval, 1, argv, 0, NULL TSRMLS_CC) == FAILURE ||
			retval == NULL)
		{
			php_error_docref(NULL TSRMLS_CC, E_WARNING,
					"Failed to evaluate value for tag '%s'"
					" with user defined function", tag);
		}
		zval_ptr_dtor(&arg);

		return retval;
	}

	return php_yaml_eval_scalar(event, NULL TSRMLS_CC);
}
/* }}} */

/* {{{ php_yaml_scalar_is_null() */
static int
php_yaml_scalar_is_null(const char *value, size_t length, yaml_event_t event)
{
	if (event.data.scalar.quoted_implicit) {
		return 0;
	}
	if (event.data.scalar.plain_implicit) {
		if ((length == 1 && *value == '~') || length == 0 ||
			!strcmp("NULL", value) || !strcmp("Null", value) || !strcmp("null", value))
		{
			return 1;
		}
	} else if (SCALAR_TAG_IS(event, "null")) {
		return 1;
	}

	return 0;
}
/* }}} */

/* {{{ php_yaml_scalar_is_true() */
static int
php_yaml_scalar_is_bool(const char *value, size_t length, yaml_event_t event)
{
	if (IS_NOT_QUOTED_OR_TAG_IS(event, "bool")) {
		if (/*(length == 1 && (*value == 'Y' || *value == 'y')) ||*/
			!strcmp("YES", value)   || !strcmp("Yes", value)   || !strcmp("yes", value) ||
			!strcmp("TRUE", value)  || !strcmp("True", value)  || !strcmp("true", value) ||
			!strcmp("ON", value)    || !strcmp("On", value)    || !strcmp("on", value))
		{
			return 1;
		}
		if (/*(length == 1 && (*value == 'N' || *value == 'n')) ||*/
			!strcmp("NO", value)    || !strcmp("No", value)    || !strcmp("no", value) ||
			!strcmp("FALSE", value) || !strcmp("False", value) || !strcmp("false", value) ||
			!strcmp("OFF", value)   || !strcmp("Off", value)   || !strcmp("off", value))
		{
			return 0;
		}
	} else if (IS_NOT_IMPLICIT_AND_TAG_IS(event, "bool")) {
		if (length == 0 || (length == 1 && *value == '0')) {
			return 0;
		} else {
			return 1;
		}
	}

	return -1;
}
/* }}} */

/* {{{ php_yaml_scalar_is_numeric() */
static int
php_yaml_scalar_is_numeric(const char *value, size_t length,
		long *lval, double *dval, char **str)
{
	const char* end = value + length;
	char *buf = NULL, *ptr = NULL;
	int negative = 0;
	int type = 0;

	if (length == 0) {
		goto not_numeric;
	}

	/* trim */
	while (value < end && (*(end - 1) == ' ' || *(end - 1) == '\t')) {
		end--;
	}
	while (value < end && (*value == ' ' || *value == '\t')) {
		value++;
	}
	if (value == end) {
		goto not_numeric;
	}

	/* not a number */
	if (!strcmp(".NAN", value) || !strcmp(".NaN", value) || !strcmp(".nan", value)) {
		type = Y_SCALAR_IS_FLOAT | Y_SCALAR_IS_NAN;
		goto finish;
	}

	/* sign */
	if (*value == '+') {
		value++;
	} else if (*value == '-') {
		negative = 1;
		value++;
	}
	if (value == end) {
		goto not_numeric;
	}

	/* infinity */
	if (!strcmp(".INF", value) || !strcmp(".Inf", value) || !strcmp(".inf", value)) {
		type = Y_SCALAR_IS_FLOAT;
		type |= (negative ? Y_SCALAR_IS_INFINITY_N : Y_SCALAR_IS_INFINITY_P);
		goto finish;
	}

	/* alloc */
	buf = (char *)emalloc(length + 3);
	ptr = buf;
	if (negative) {
		*ptr++ = '-';
	}

	/* parse */
	if (*value == '0') {
		*ptr++ = *value++;
		if (value == end) {
			goto return_zero;
		}

		if (*value == 'b') {
			/* binary integer */
			*ptr++ = *value++;
			if (value == end) {
				goto not_numeric;
			}
			while (value < end && (*value == '_' || *value == '0')) {
				value++;
			}
			if (value == end) {
				goto return_zero;
			}
			/* check the sequence */
			while (value < end) {
				if (*value == '_') {
					*value++;
				} else if (*value == '0' || *value == '1') {
					*ptr++ = *value++;
				} else {
					goto not_numeric;
				}
			}
			type = Y_SCALAR_IS_INT | Y_SCALAR_IS_BINARY;

		} else if (*value == 'x') {
			/* hexadecimal integer */
			*ptr++ = *value++;
			if (value == end) {
				goto not_numeric;
			}
			while (value < end && (*value == '_' || *value == '0')) {
				value++;
			}
			if (value == end) {
				goto return_zero;
			}
			/* check the sequence */
			while (value < end) {
				if (*value == '_') {
					*value++;
				} else if ((*value >= '0' && *value <= '9') ||
					(*value >= 'A' && *value <= 'F') ||
					(*value >= 'a' && *value <= 'f'))
				{
					*ptr++ = *value++;
				} else {
					goto not_numeric;
				}
			}
			type = Y_SCALAR_IS_INT | Y_SCALAR_IS_HEXADECIMAL;

		} else if (*value == '_' || (*value >= '0' && *value <= '7')) {
			/* octal integer */
			while (value < end) {
				if (*value == '_') {
					*value++;
				} else if (*value >= '0' && *value <= '7') {
					*ptr++ = *value++;
				} else {
					goto not_numeric;
				}
			}
			type = Y_SCALAR_IS_INT | Y_SCALAR_IS_OCTAL;

		} else if (*value == '.') {
			goto check_float;

		} else {
			goto not_numeric;
		}

	} else if (*value >= '1' && *value <= '9') {
		/* integer */
		*ptr++ = *value++;
		while (value < end) {
			if (*value == '_' || *value == ',') {
				*value++;
			} else if (*value >= '0' && *value <= '9') {
				*ptr++ = *value++;
			} else if (*value == ':') {
				goto check_sexa;
			} else if (*value == '.') {
				goto check_float;
			} else {
				goto not_numeric;
			}
		}
		type = Y_SCALAR_IS_INT | Y_SCALAR_IS_DECIMAL;

	} else if (*value == ':') {
		/* sexagecimal */
	  check_sexa:
		while (value < end - 2) {
			if (*value == '.') {
				type = Y_SCALAR_IS_FLOAT | Y_SCALAR_IS_SEXAGECIMAL;
				goto check_float;
			}
			if (*value != ':') {
				goto not_numeric;
			}
			*ptr++ = *value++;
			if (*(value + 1) == ':') {
				if (*value >= '0' && *value <= '9') {
					*ptr++ = *value++;
				} else {
					goto not_numeric;
				}
			} else {
				if ((*value >= '0' && *value <= '5') &&
					(*(value + 1) >= '0' && *(value + 1) <= '9'))
				{
					*ptr++ = *value++;
					*ptr++ = *value++;
				} else {
					goto not_numeric;
				}
			}
		}
		if (*value == '.') {
			type = Y_SCALAR_IS_FLOAT | Y_SCALAR_IS_SEXAGECIMAL;
			goto check_float;
		} else if (value == end) {
			type = Y_SCALAR_IS_INT | Y_SCALAR_IS_SEXAGECIMAL;
		} else {
			goto not_numeric;
		}

	} else if (*value == '.') {
		/* float */
		*ptr++ = '0';
	  check_float:
		*ptr++ = *value++;
		if (type == (Y_SCALAR_IS_FLOAT | Y_SCALAR_IS_SEXAGECIMAL)) {
			/* sexagecimal float */
			while (value < end && (*(end - 1) == '_' || *(end - 1) == '0')) {
				end--;
			}
			if (value == end) {
				*ptr++ = '0';
			}
			while (value < end) {
				if (*value == '_') {
					*value++;
				} else if (*value >= '0' && *value <= '9') {
					*ptr++ = *value++;
				} else {
					goto not_numeric;
				}
			}
		} else {
			/* decimal float */
			int is_exp = 0;
			while (value < end) {
				if (*value == '_') {
					*value++;
				} else if (*value >= '0' && *value <= '9') {
					*ptr++ = *value++;
				} else if (*value == 'E' || *value == 'e') {
					/* exponential */
					is_exp = 1;
					*ptr++ = *value++;
					if (value == end || (*value != '+' && *value != '-')) {
						goto not_numeric;
					}
					*ptr++ = *value++;
					if (value == end || *value < '0' || *value > '9' || (*value == '0' && value + 1 == end)) {
						goto not_numeric;
					}
					*ptr++ = *value++;
					while (value < end) {
						if (*value >= '0' && *value <= '9') {
							*ptr++ = *value++;
						} else {
							goto not_numeric;
						}
					}
				} else {
					goto not_numeric;
				}
			}
			/* trim */
			if (!is_exp) {
				while (*(ptr - 1) == '0') {
					ptr--;
				}
				if (*(ptr - 1) == '.') {
					*ptr++ = '0';
				}
			}
			type = Y_SCALAR_IS_FLOAT | Y_SCALAR_IS_DECIMAL;
		}

	} else {
		goto not_numeric;
	}

	/* terminate */
	*ptr = '\0';

  finish:
	/* convert & assign */
	if ((type & Y_SCALAR_IS_INT) && lval != NULL) {
		switch (type & Y_SCALAR_FORMAT_MASK) {
		  case Y_SCALAR_IS_BINARY:
			ptr = buf + 2;
			if (*ptr == 'b') {
				ptr++;
			}
			*lval = strtol(ptr, (char **)NULL, 2);
			if (*buf == '-') {
				*lval *= -1L;
			}
			break;
		  case Y_SCALAR_IS_OCTAL:
			*lval = strtol(buf, (char **)NULL, 8);
			break;
		  case Y_SCALAR_IS_HEXADECIMAL:
			*lval = strtol(buf, (char **)NULL, 16);
			break;
		  case Y_SCALAR_IS_SEXAGECIMAL:
			*lval = php_yaml_eval_sexagesimal_l(0, buf, ptr);
			if (*buf == '-') {
				*lval *= -1L;
			}
			break;
		  default:
			*lval = atol(buf);
		}
	} else if ((type & Y_SCALAR_IS_FLOAT) && dval != NULL) {
		switch (type & Y_SCALAR_FORMAT_MASK) {
		  case Y_SCALAR_IS_SEXAGECIMAL:
			*dval = php_yaml_eval_sexagesimal_d(0.0, buf, ptr);
			if (*buf == '-') {
				*dval *= -1.0;
			}
			break;
		  case Y_SCALAR_IS_INFINITY_P:
			*dval = php_get_inf();
			break;
		  case Y_SCALAR_IS_INFINITY_N:
			*dval = -php_get_inf();
			break;
		  case Y_SCALAR_IS_NAN:
			*dval = php_get_nan();
			break;
		  default:
			*dval = atof(buf);
		}
	}
	if (buf != NULL) {
		if (str != NULL) {
			*str = buf;
		} else {
			efree(buf);
		}
	}

	/* return */
	return type;

  return_zero:
	if (lval != NULL) {
		*lval = 0;
	}
	if (dval != NULL) {
		*dval = 0.0;
	}
	if (buf != NULL) {
		efree(buf);
	}
	return (Y_SCALAR_IS_INT | Y_SCALAR_IS_ZERO);

  not_numeric:
	if (buf != NULL) {
		efree(buf);
	}
	return Y_SCALAR_IS_NOT_NUMERIC;
}
/* }}} */

#define ts_skip_space() \
	while (ptr < end && (*ptr == ' ' || *ptr == '\t')) { \
		ptr++; \
	}

#define ts_skip_number() \
	while (ptr < end && *ptr >= '0' && *ptr <= '9') { \
		ptr++; \
	}

/* {{{ php_yaml_scalar_is_timestamp()
 * timestamp specification is found at http://yaml.org/type/timestamp.html.
 */
static int
php_yaml_scalar_is_timestamp(const char *value, size_t length)
{
	const char *ptr = value;
	const char *end = value + length;
	const char *pos1, *pos2;

	/* skip leading space */
	ts_skip_space();

	/* check year and separator */
	pos1 = pos2 = ptr;
	ts_skip_number();
	if (ptr == pos1 || ptr == end || ptr - pos2 != 4 || *ptr != '-') {
		return 0;
	}

	/* check month and separator */
	pos2 = ++ptr;
	ts_skip_number();
	if (ptr == pos2 || ptr == end || ptr - pos2 > 2 || *ptr != '-') {
		return 0;
	}

	/* check day and separator */
	pos2 = ++ptr;
	ts_skip_number();
	if (ptr == pos2 || ptr - pos2 > 2) {
		return 0;
	}

	/* check separator */
	pos2 = ptr;
	ts_skip_space();
	if (ptr == end) {
		return (pos2 - pos1 == 10) ? 1 : 0;
	}
	if (*ptr == 'T' || *ptr == 't') {
		*ptr++;
	}

	/* check hour and separator */
	pos1 = ptr;
	ts_skip_number();
	if (ptr == pos1 || ptr == end || ptr - pos1 > 2 || *ptr != ':') {
		return 0;
	}

	/* check minute and separator */
	pos1 = ++ptr;
	ts_skip_number();
	if (ptr == end || ptr - pos1 != 2 || *ptr != ':') {
		return 0;
	}

	/* check second */
	pos1 = ++ptr;
	ts_skip_number();
	if (ptr == end) {
		return (ptr - pos1 == 2) ? 1 : 0;
	}

	/* check fraction */
	if (*ptr == '.') {
		ptr++;
		ts_skip_number();
	}

	/* skip separator space */
	ts_skip_space();
	if (ptr == end) {
		return 1;
	}

	/* check time zone */
	if (*ptr == 'Z') {
		ptr++;
		ts_skip_space();
		return (ptr == end) ? 1 : 0;
	}
	if (*ptr != '+' && *ptr != '-') {
		return 0;
	}
	pos1 = ++ptr;
	ts_skip_number();
	if (ptr - pos1 == 0 || ptr - pos1 > 4) {
		return 0;
	}
	if (ptr - pos1 < 3 && *ptr == ':') {
		pos1 = ++ptr;
		ts_skip_number();
		if (ptr - pos1 != 2) {
			return 0;
		}
	}

	/* skip following space */
	ts_skip_space();
	return (ptr == end) ? 1 : 0;
}
/* }}} */

/* {{{ php_yaml_eval_sexagesimal_l() */
static long
php_yaml_eval_sexagesimal_l(long lval, char *sg, char *eos)
{
	char *ep;
	while (sg < eos && (*sg < '0' || *sg > '9')) {
		*sg++;
	}
	ep = sg;
	while (ep < eos && *ep >= '0' && *ep <= '9') {
		*ep++;
	}
	if (sg == eos) {
		return lval;
	}
	return php_yaml_eval_sexagesimal_l(lval * 60 + strtol(sg, (char **)NULL, 10), ep, eos);
}
/* }}} */

/* {{{ php_yaml_eval_sexagesimal_d() */
static double
php_yaml_eval_sexagesimal_d(double dval, char *sg, char *eos)
{
	char *ep;
	while (sg < eos && *sg != '.' && (*sg < '0' || *sg > '9')) {
		*sg++;
	}
	ep = sg;
	while (ep < eos && *ep >= '0' && *ep <= '9') {
		*ep++;
	}
	if (sg == eos || *sg == '.') {
		return dval;
	}
	return php_yaml_eval_sexagesimal_d(dval * 60.0 + strtod(sg, (char **)NULL), ep, eos);
}
/* }}} */

/* {{{ php_yaml_eval_timestamp() */
static int
php_yaml_eval_timestamp(zval **zpp, char *ts, int ts_len TSRMLS_DC)
{
	if (YAML_G(timestamp_decoder) != NULL || YAML_G(decode_timestamp) == 1L || YAML_G(decode_timestamp) == 2L) {
		zval **argv[] = { NULL };
		zval *arg, *retval, *func, afunc;
		char *funcs[] = { "strtotime", "date_create" };

		INIT_ZVAL(afunc);
		if (YAML_G(timestamp_decoder) == NULL) {
			if (YAML_G(decode_timestamp) == 2L) {
				ZVAL_STRING(&afunc, funcs[1], 0);
			} else {
				ZVAL_STRING(&afunc, funcs[0], 0);
			}
			func = &afunc;
		} else {
			func = YAML_G(timestamp_decoder);
		}

		MAKE_STD_ZVAL(arg);
#ifdef ZEND_ENGINE_2
		ZVAL_STRINGL(arg, ts, ts_len, 1);
#else
		{
			/* fix timestamp format for PHP4 */
			char *buf, *dst, *end, *src;

			buf = (char *)emalloc((size_t)ts_len + 1);
			dst = buf;
			end = ts + ts_len;
			src = ts;

			while (src < end && *src != '.') {
				if (src + 1 < end && (*(src - 1) >= '0' && *(src - 1) <= '9') &&
					(*src == 'T' || *src == 't') && (*(src + 1) >= '0' && *(src + 1) <= '9'))
				{
					src++;
					*dst++ = ' ';
				} else if (*src == ':' && src > ts + 2 && (
					((*(src - 2) == '+' || *(src - 2) == '-') &&
					 (*(src - 1) >= '0' || *(src - 1) <= '5'))
					||
					((*(src - 3) == '+' || *(src - 3) == '-') &&
					 (*(src - 2) >= '0' || *(src - 2) <= '5') &&
					 (*(src - 1) >= '0' || *(src - 1) <= '9'))
					))
				{
					src++;
				} else {
					*dst++ = *src++;
				}
			}
			if (src < end && *src == '.') {
				src++;
				while (src < end && *src >= '0' && *src <= '9') {
					src++;
				}
			}
			while (src < end) {
				if (*src == ':' && src > ts + 2 && (
					((*(src - 2) == '+' || *(src - 2) == '-') &&
					 (*(src - 1) >= '0' || *(src - 1) <= '5'))
					||
					((*(src - 3) == '+' || *(src - 3) == '-') &&
					 (*(src - 2) >= '0' || *(src - 2) <= '5') &&
					 (*(src - 1) >= '0' || *(src - 1) <= '9'))
					))
				{
					src++;
				} else {
					*dst++ = *src++;
				}
			}
			*dst = '\0';

			ZVAL_STRINGL(arg, buf, dst - buf, 0);
		}
#endif
		argv[0] = &arg;

		if (call_user_function_ex(EG(function_table), NULL, func,
				&retval, 1, argv, 0, NULL TSRMLS_CC) == FAILURE ||
			retval == NULL)
		{
			php_error_docref(NULL TSRMLS_CC, E_WARNING,
					"Failed to evaluate string '%s' as timestamp", ts);
			zval_ptr_dtor(&arg);
			return FAILURE;
		} else {
			zval_ptr_dtor(&arg);
			zval_dtor(*zpp);
			ZVAL_ZVAL(*zpp, retval, 1, 1);
			return SUCCESS;
		}
	} else {
		zval_dtor(*zpp);
#ifdef IS_UNICODE
		ZVAL_U_STRINGL(UG(utf8_conv), *zpp, ts, ts_len, 1);
#else
		ZVAL_STRINGL(*zpp, ts, ts_len, 1);
#endif
		return SUCCESS;
	}
}
/* }}} */

/* {{{ php_yaml_parser_error() */
static void
php_yaml_print_parser_error (yaml_parser_t *parser TSRMLS_DC)
{
    switch (parser->error)
        {
        case YAML_MEMORY_ERROR:
            php_error_docref (NULL TSRMLS_CC, E_WARNING,
                              "Memory error: Not enough memory for parsing");
            break;
            
        case YAML_READER_ERROR:
            if (parser->problem_value != -1)
                {
                    php_error_docref (NULL TSRMLS_CC, E_WARNING,
                                      "Reader error: %s: #%X at %zu", parser->problem,
                                      parser->problem_value, parser->problem_offset);
                }
            else
                {
                    php_error_docref (NULL TSRMLS_CC, E_WARNING,
                                      "Reader error: %s at %zu", parser->problem,
                                      parser->problem_offset);
                }
            break;
            
        case YAML_SCANNER_ERROR:
            if (parser->context)
                {
                    php_error_docref (NULL TSRMLS_CC, E_WARNING,
                                      "Scanner error: %s at line %zu, column %zu\n"
                                      "%s at line %zu, column %zu", parser->context,
                                      parser->context_mark.line+1, parser->context_mark.column+1,
                                      parser->problem, parser->problem_mark.line+1,
                                      parser->problem_mark.column+1);
                }
            else
                {
                    php_error_docref (NULL TSRMLS_CC, E_WARNING,
                                      "Scanner error: %s at line %zu, column %zu",
                                      parser->problem, parser->problem_mark.line+1,
                                      parser->problem_mark.column+1);
                }
            break;
            
        case YAML_PARSER_ERROR:
            if (parser->context)
                {
                    php_error_docref (NULL TSRMLS_CC, E_WARNING,
                                      "Parser error: %s at line %zu, column %zu\n"
                                      "%s at line %zu, column %zu\n", parser->context,
                                      parser->context_mark.line+1, parser->context_mark.column+1,
                                      parser->problem, parser->problem_mark.line+1,
                                      parser->problem_mark.column+1);
                }
            else
                {
                    php_error_docref (NULL TSRMLS_CC, E_WARNING,
                                      "Parser error: %s at line %zu, column %zu",
                                      parser->problem, parser->problem_mark.line+1,
                                      parser->problem_mark.column+1);
                }
            break;
            
        default:
            /* Couldn't happen. */
            php_error_docref (NULL TSRMLS_CC, E_ERROR, "Internal error");
            break;
        }
}
/* }}} */

/* {{{ php_yaml_check_callbacks() */
int
php_yaml_check_callbacks(HashTable *callbacks TSRMLS_DC)
{
	zval **entry = NULL;
#ifdef IS_UNICODE
	zstr key;
#else
	char *key = NULL;
#endif
	uint key_len = 0;
	ulong idx = 0L;

	zend_hash_internal_pointer_reset(callbacks);

	while (zend_hash_get_current_data(callbacks, (void **)&entry) == SUCCESS) {
		int key_type = zend_hash_get_current_key_ex(callbacks, &key, &key_len, &idx, 0, NULL);

#ifdef IS_UNICODE
		if (key_type == HASH_KEY_IS_STRING || key_type == HASH_KEY_IS_UNICODE) {
			zval name;
			int type = (key_type == HASH_KEY_IS_STRING) ? IS_STRING : IS_UNICODE;

			INIT_ZVAL(name);

			if (!zend_is_callable(*entry, 0, &name)) {
				if (Z_TYPE(name) == IS_UNICODE || Z_TYPE(name) == IS_STRING) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING,
							"Callback for tag '%R', '%R' is not valid",
							type, key, Z_TYPE(name), Z_UNIVAL(name));
				} else {
					php_error_docref(NULL TSRMLS_CC, E_WARNING,
							"Callback for tag '%R' is not valid", type, key);
				}
				zval_dtor(&name);
				return FAILURE;
			}

			if (ZEND_U_EQUAL(type, key, key_len - 1, "tag:yaml.org,2002:timestamp", sizeof("tag:yaml.org,2002:timestamp") - 1)) {
				YAML_G(timestamp_decoder) = *entry;
			}

			zval_dtor(&name);
		} else {
			php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Callback key shoule be a string");
		}
#else
		if (key_type == HASH_KEY_IS_STRING) {
			char *name;

			if (!zend_is_callable(*entry, 0, &name TSRMLS_CC)) {
				if (name != NULL) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING,
							"Callback for tag '%s', '%s' is not valid", key, name);
					efree(name);
				} else {
					php_error_docref(NULL TSRMLS_CC, E_WARNING,
							"Callback for tag '%s' is not valid", key);
				}
				return FAILURE;
			}

			if (!strcmp(key, "tag:yaml.org,2002:timestamp")) {
				YAML_G(timestamp_decoder) = *entry;
			}

			if (name != NULL) {
				efree(name);
			}
		} else {
			php_error_docref(NULL TSRMLS_CC, E_NOTICE,
					"Callback key shoule be a string");
		}
#endif

		zend_hash_move_forward(callbacks);
	}

	return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

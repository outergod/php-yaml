/**
 * YAML parser and emitter PHP extension
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
 * @version     $Id: yaml.c 27 2008-09-22 20:55:55Z e-user $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <yaml.h>
#include <ext/standard/php_smart_str.h>
#include <ext/standard/php_var.h>
#include <ext/standard/info.h>
#include "php_yaml.h"
#include "zval_refcount.h" /* for PHP < 5.3 */
#include "parser.h"
#include "emitter.h"

/* {{{ cross-extension dependencies */
#if ZEND_EXTENSION_API_NO >= 220050617
static zend_module_dep yaml_deps[] = {
  ZEND_MOD_OPTIONAL ("date")
  {NULL, NULL, NULL, 0}
};
#endif
/* }}} */

/* {{{ argument information */
#ifdef ZEND_BEGIN_ARG_INFO
/* Handle PHP 5.3 correctly */
#if ZEND_EXTENSION_API_NO >= 220090626
ZEND_BEGIN_ARG_INFO_EX (arginfo_yaml_parse, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO (0, input)
  ZEND_ARG_INFO (0, pos)
  ZEND_ARG_INFO (1, ndocs)
  ZEND_ARG_ARRAY_INFO (0, callbacks, 0)
  ZEND_END_ARG_INFO ()

ZEND_BEGIN_ARG_INFO_EX (arginfo_yaml_parse_file, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO (0, filename)
  ZEND_ARG_INFO (0, pos)
  ZEND_ARG_INFO (1, ndocs)
  ZEND_ARG_ARRAY_INFO (0, callbacks, 0)
  ZEND_END_ARG_INFO ()

ZEND_BEGIN_ARG_INFO_EX (arginfo_yaml_parse_url, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO (0, url)
  ZEND_ARG_INFO (0, pos)
  ZEND_ARG_INFO (1, ndocs)
  ZEND_ARG_ARRAY_INFO (0, callbacks, 0)
  ZEND_END_ARG_INFO ()
#else
static ZEND_BEGIN_ARG_INFO_EX (arginfo_yaml_parse, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO (0, input)
  ZEND_ARG_INFO (0, pos)
  ZEND_ARG_INFO (1, ndocs)
  ZEND_ARG_ARRAY_INFO (0, callbacks, 0)
  ZEND_END_ARG_INFO ()

static ZEND_BEGIN_ARG_INFO_EX (arginfo_yaml_parse_file, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO (0, filename)
  ZEND_ARG_INFO (0, pos)
  ZEND_ARG_INFO (1, ndocs)
  ZEND_ARG_ARRAY_INFO (0, callbacks, 0)
  ZEND_END_ARG_INFO ()

static ZEND_BEGIN_ARG_INFO_EX (arginfo_yaml_parse_url, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO (0, url)
  ZEND_ARG_INFO (0, pos)
  ZEND_ARG_INFO (1, ndocs)
  ZEND_ARG_ARRAY_INFO (0, callbacks, 0)
  ZEND_END_ARG_INFO ()
#endif
#else
#define arginfo_yaml_parse third_arg_force_ref
#define arginfo_yaml_parse_file third_arg_force_ref
#define arginfo_yaml_parse_url third_arg_force_ref
#endif
/* }}} */

/* {{{ yaml_functions[] */
  static zend_function_entry yaml_functions[] = {
  PHP_FE (yaml_parse,      arginfo_yaml_parse)
  PHP_FE (yaml_parse_file, arginfo_yaml_parse_file)
  PHP_FE (yaml_parse_url,  arginfo_yaml_parse_url)
  PHP_FE (yaml_emit,       NULL)
  PHP_FE (yaml_emit_file,  NULL)
  { NULL, NULL, NULL }
};
/* }}} */

/* {{{ yaml_module_entry */
zend_module_entry yaml_module_entry = {
#if ZEND_EXTENSION_API_NO >= 220050617
  STANDARD_MODULE_HEADER_EX,
  NULL,
  yaml_deps,
#else
  STANDARD_MODULE_HEADER,
#endif
  PHP_YAML_EXTNAME,
  yaml_functions,
  PHP_MINIT (yaml),
  PHP_MSHUTDOWN (yaml),
  NULL,
  NULL,
  PHP_MINFO (yaml),
  PHP_YAML_VERSION,
#if ZEND_EXTENSION_API_NO >= 220060519
  PHP_MODULE_GLOBALS (yaml),
  PHP_GINIT (yaml),
  NULL,
  NULL,
  STANDARD_MODULE_PROPERTIES_EX
#else
  STANDARD_MODULE_PROPERTIES
#endif
};

#ifdef COMPILE_DL_YAML
ZEND_GET_MODULE (yaml)
#endif
/* }}} */

/* {{{ ini entries */
#ifndef ZEND_ENGINE_2
#define OnUpdateLong OnUpdateInt
#endif

PHP_INI_BEGIN ()
STD_PHP_INI_ENTRY ("yaml.decode_binary", "1", PHP_INI_ALL, OnUpdateBool,
                   decode_binary, zend_yaml_globals, yaml_globals)
STD_PHP_INI_ENTRY ("yaml.decode_timestamp", "1", PHP_INI_ALL, OnUpdateLong,
                   decode_timestamp, zend_yaml_globals, yaml_globals)
STD_PHP_INI_BOOLEAN ("yaml.throw_exceptions", "1", PHP_INI_ALL, OnUpdateBool,
                     throw_exceptions, zend_yaml_globals, yaml_globals)
STD_PHP_INI_ENTRY ("yaml.fill_column", "80", PHP_INI_ALL, OnUpdateLong,
                   fill_column, zend_yaml_globals, yaml_globals)
STD_PHP_INI_BOOLEAN ("yaml.nomnom", "0", PHP_INI_ALL, OnUpdateBool,
                     nomnom, zend_yaml_globals, yaml_globals)
PHP_INI_END ()

/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION (yaml)
{
#if ZEND_EXTENSION_API_NO < 220060519
  ZEND_INIT_MODULE_GLOBALS (yaml, php_yaml_init_globals, NULL)
#endif
  REGISTER_LONG_CONSTANT ("YAML_ANY_ENCODING", YAML_ANY_ENCODING, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("YAML_UTF8_ENCODING", YAML_UTF8_ENCODING, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("YAML_UTF16LE_ENCODING", YAML_UTF16LE_ENCODING, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("YAML_UTF16BE_ENCODING", YAML_UTF16BE_ENCODING, CONST_CS | CONST_PERSISTENT);
    
  REGISTER_LONG_CONSTANT ("YAML_ANY_BREAK", YAML_ANY_BREAK, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("YAML_CR_BREAK", YAML_CR_BREAK, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("YAML_LN_BREAK", YAML_LN_BREAK, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("YAML_CRLN_BREAK", YAML_CRLN_BREAK, CONST_CS | CONST_PERSISTENT);

  REGISTER_INI_ENTRIES ();
  return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION (yaml)
{
  UNREGISTER_INI_ENTRIES ();
  return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION (yaml)
{
  php_info_print_table_start ();
  php_info_print_table_row (2, "LibYAML Support", "enabled");
  php_info_print_table_row (2, "Module Version", PHP_YAML_VERSION);
  php_info_print_table_end ();

  DISPLAY_INI_ENTRIES ();
}
/* }}} */

/* {{{ PHP_GINIT_FUNCTION () */
PHP_GINIT_FUNCTION (yaml)
{
  yaml_globals->decode_binary = 1;
  yaml_globals->decode_timestamp = 1;
  yaml_globals->timestamp_decoder = NULL;
  yaml_globals->throw_exceptions = 1;
  yaml_globals->fill_column = 80;
  yaml_globals->nomnom = 0;
#ifdef IS_UNICODE
  yaml_globals->orig_runtime_encoding_conv = NULL;
#endif
}
/* }}} */

/* {{{ php_yaml_write_to_buffer () */
static int
php_yaml_write_to_buffer (void *data, unsigned char *buffer, size_t size)
{
  smart_str_appendl ((smart_str *)data, (char *)buffer, size);
  return 1;
}
/* }}} */

/* {{{ proto mixed yaml_parse (string input[, int pos[, int &ndocs[, array callbacks]]]) */
PHP_FUNCTION (yaml_parse)
{
  char *input = NULL;
  int input_len = 0;
  long pos = 0;
  zval *zndocs = NULL;
  zval *zcallbacks = NULL;
  HashTable *callbacks = NULL;
  eval_scalar_func_t eval_func;

  yaml_parser_t parser = {0};
  zval *yaml = NULL;
  long ndocs = 0;

#ifdef IS_UNICODE
  YAML_G (orig_runtime_encoding_conv) = UG (runtime_encoding_conv);
#endif
  YAML_G (timestamp_decoder) = NULL;

#ifdef IS_UNICODE
  if (zend_parse_parameters (ZEND_NUM_ARGS () TSRMLS_CC, "s&|lza/",
                             &input, &input_len, UG (utf8_conv),
                             &pos, &zndocs, &zcallbacks) == FAILURE)
    {
      return;
    }
#else
  if (zend_parse_parameters (ZEND_NUM_ARGS () TSRMLS_CC, "s|lza/",
                             &input, &input_len,
                             &pos, &zndocs, &zcallbacks) == FAILURE)
    {
      return;
    }
#endif

  if (zcallbacks != NULL)
    {
      callbacks = Z_ARRVAL_P (zcallbacks);
      if (php_yaml_check_callbacks (callbacks TSRMLS_CC) == FAILURE)
        {
          RETURN_FALSE;
        }
     
      eval_func = php_yaml_eval_scalar_with_callbacks;
    }
  else 
    eval_func = php_yaml_eval_scalar;
  

#ifdef IS_UNICODE
  UG (runtime_encoding_conv) = UG (utf8_conv);
#endif

  yaml_parser_initialize (&parser);
  yaml_parser_set_input_string (&parser, (unsigned char *)input, (size_t)input_len);

  if (pos < 0)
    yaml = php_yaml_read_all (&parser, &ndocs, eval_func, callbacks);
  else
    yaml = php_yaml_read_partial (&parser, pos, &ndocs, eval_func, callbacks TSRMLS_CC);
  
  yaml_parser_delete (&parser);

#ifdef IS_UNICODE
  UG (runtime_encoding_conv) = YAML_G (orig_runtime_encoding_conv);
#endif

  if (zndocs != NULL)
    {
      zval_dtor (zndocs);
      ZVAL_LONG (zndocs, ndocs);
    }

  if (yaml == NULL)
    {
      RETURN_FALSE;
    }

  RETURN_ZVAL (yaml, 1, 1);
}
/* }}} yaml_parse */

/* {{{ proto mixed yaml_parse_file (string filename[, int pos[, int &ndocs[, array callbacks]]]) */
PHP_FUNCTION (yaml_parse_file)
{
  char *filename = NULL;
  int filename_len = 0;
  long pos = 0;
  zval *zndocs = NULL;
  zval *zcallbacks = NULL;
  HashTable *callbacks = NULL;
  eval_scalar_func_t eval_func;

  php_stream *stream = NULL;
  FILE *fp = NULL;

  yaml_parser_t parser = {0};
  zval *yaml = NULL;
  long ndocs = 0;

#ifdef IS_UNICODE
  YAML_G (orig_runtime_encoding_conv) = UG (runtime_encoding_conv);
#endif
  YAML_G (timestamp_decoder) = NULL;

#ifdef IS_UNICODE
  if (zend_parse_parameters (ZEND_NUM_ARGS () TSRMLS_CC, "s&|lza/",
                            &filename, &filename_len, ZEND_U_CONVERTER (UG (filesystem_encoding_conv)),
                            &pos, &zndocs, &zcallbacks) == FAILURE)
    return;
#else
  if (zend_parse_parameters (ZEND_NUM_ARGS () TSRMLS_CC, "s|lza/",
                            &filename, &filename_len, &pos, &zndocs, &zcallbacks) == FAILURE)
    return;
#endif

  if (zcallbacks != NULL)
    {
      callbacks = Z_ARRVAL_P (zcallbacks);
      if (php_yaml_check_callbacks (callbacks TSRMLS_CC) == FAILURE)
        RETURN_FALSE;

      eval_func = php_yaml_eval_scalar_with_callbacks;
    }
  else 
    eval_func = php_yaml_eval_scalar;

  if ((stream = php_stream_open_wrapper (filename, "rb",
                                        IGNORE_URL | ENFORCE_SAFE_MODE | REPORT_ERRORS | STREAM_WILL_CAST, NULL)) == NULL)
    {
      RETURN_FALSE;
    }

  if (php_stream_cast (stream, PHP_STREAM_AS_STDIO, (void **)&fp, 1) == FAILURE)
    {
      php_stream_close (stream);
      RETURN_FALSE;
    }

#ifdef IS_UNICODE
  UG (runtime_encoding_conv) = UG (utf8_conv);
#endif

  yaml_parser_initialize (&parser);
  yaml_parser_set_input_file (&parser, fp);

  if (pos < 0)
    yaml = php_yaml_read_all (&parser, &ndocs, eval_func, callbacks);
  else
    yaml = php_yaml_read_partial (&parser, pos, &ndocs, eval_func, callbacks TSRMLS_CC);

  yaml_parser_delete (&parser);
  php_stream_close (stream);

#ifdef IS_UNICODE
  UG (runtime_encoding_conv) = YAML_G (orig_runtime_encoding_conv);
#endif

  if (zndocs != NULL)
    {
      zval_dtor (zndocs);
      ZVAL_LONG (zndocs, ndocs);
    }

  if (yaml == NULL)
    {
      RETURN_FALSE;
    }

  RETURN_ZVAL (yaml, 1, 1);
}
/* }}} yaml_parse_file */

/* {{{ proto mixed yaml_parse_url (string url[, int pos[, int &ndocs[, array callbacks]]]) */
PHP_FUNCTION (yaml_parse_url)
{
  char *url = NULL;
  int url_len = 0;
  long pos = 0;
  zval *zndocs = NULL;
  zval *zcallbacks = NULL;
  HashTable *callbacks = NULL;
  eval_scalar_func_t eval_func;

  php_stream *stream = NULL;
  char *input = NULL;
  size_t size = 0;

  yaml_parser_t parser = {0};
  zval *yaml = NULL;
  long ndocs = 0;

#ifdef IS_UNICODE
  YAML_G (orig_runtime_encoding_conv) = UG (runtime_encoding_conv);
#endif
  YAML_G (timestamp_decoder) = NULL;

  if (zend_parse_parameters (ZEND_NUM_ARGS () TSRMLS_CC, "s|lza/",
                            &url, &url_len, &pos, &zndocs, &zcallbacks) == FAILURE)
    return;

  if (zcallbacks != NULL)
    {
      callbacks = Z_ARRVAL_P (zcallbacks);
      if (php_yaml_check_callbacks (callbacks TSRMLS_CC) == FAILURE)
        {
          RETURN_FALSE;
        }
      
      eval_func = php_yaml_eval_scalar_with_callbacks;
  }
  else
    eval_func = php_yaml_eval_scalar;
  

  if ( (stream = php_stream_open_wrapper (url, "rb",
                                        ENFORCE_SAFE_MODE | REPORT_ERRORS, NULL)) == NULL)
    {
      RETURN_FALSE;
    }
#ifdef IS_UNICODE
  size = php_stream_copy_to_mem (stream, (void **)&input, PHP_STREAM_COPY_ALL, 0);
#else
  size = php_stream_copy_to_mem (stream, &input, PHP_STREAM_COPY_ALL, 0);
#endif

#ifdef IS_UNICODE
  UG (runtime_encoding_conv) = UG (utf8_conv);
#endif

  yaml_parser_initialize (&parser);
  yaml_parser_set_input_string (&parser, (unsigned char *)input, size);

  if (pos < 0)
    yaml = php_yaml_read_all (&parser, &ndocs, eval_func, callbacks);
  else
    yaml = php_yaml_read_partial (&parser, pos, &ndocs, eval_func, callbacks TSRMLS_CC);

  yaml_parser_delete (&parser);
  php_stream_close (stream);
  efree (input);

#ifdef IS_UNICODE
  UG (runtime_encoding_conv) = YAML_G (orig_runtime_encoding_conv);
#endif

  if (zndocs != NULL)
    {
      zval_dtor (zndocs);
      ZVAL_LONG (zndocs, ndocs);
    }

  if (yaml == NULL)
    {
      RETURN_FALSE;
    }

  RETURN_ZVAL (yaml, 1, 1);
}
/* }}} yaml_parse_url */

/* {{{ proto string yaml_emit (mixed data[, int encoding[, int linebreak]]) */
PHP_FUNCTION (yaml_emit)
{
  zval *data = NULL;
  long encoding = 0;
  long linebreak = 0;

  yaml_emitter_t emitter = {0};
  smart_str str = {0};

  if (zend_parse_parameters (ZEND_NUM_ARGS () TSRMLS_CC, "z/|ll",
                            &data, &encoding, &linebreak) == FAILURE)
    {
      RETURN_NULL ();
    }

  yaml_emitter_initialize (&emitter);
  yaml_emitter_set_output (&emitter, &php_yaml_write_to_buffer, (void *)&str);
  yaml_emitter_set_unicode (&emitter, 1);
  yaml_emitter_set_canonical (&emitter, 0);
  yaml_emitter_set_encoding (&emitter, encoding);
  yaml_emitter_set_break (&emitter, linebreak);

  if (YAML_G (nomnom))
    yaml_emitter_set_width (&emitter, -1);
  else
    yaml_emitter_set_width (&emitter, YAML_G (fill_column));


  if (php_yaml_write_impl (&emitter, data, encoding TSRMLS_CC) == SUCCESS) {
#ifdef IS_UNICODE
    RETVAL_U_STRINGL (UG (utf8_conv), str.c, str.len, ZSTR_DUPLICATE);
#else
    RETVAL_STRINGL (str.c, str.len, 1);
#endif
  } else {
    RETVAL_FALSE;
  }
 
  yaml_emitter_delete (&emitter);
  smart_str_free (&str);
}
/* }}} yaml_emit */

/* {{{ proto bool yaml_emit_file (string filename, mixed data[, int encoding[, int linebreak]]) */
PHP_FUNCTION (yaml_emit_file)
{
  char *filename = NULL;
  int filename_len = 0;
  php_stream *stream = NULL;
  FILE *fp = NULL;
  zval *data = NULL;
  long encoding = 0;
  long linebreak = 0;

  yaml_emitter_t emitter = {0};

#ifdef IS_UNICODE
  if (zend_parse_parameters (ZEND_NUM_ARGS () TSRMLS_CC, "s&z/|ll",
                             &filename, &filename_len,
                             ZEND_U_CONVERTER (UG (filesystem_encoding_conv)),
                             &data, &encoding, &linebreak) == FAILURE)
    return;
#else
  if (zend_parse_parameters (ZEND_NUM_ARGS () TSRMLS_CC, "sz/|ll",
                            &filename, &filename_len, &data, &encoding, &linebreak) == FAILURE)
    return;
#endif

  if ( (stream = php_stream_open_wrapper (filename, "wb",
                                          IGNORE_URL | ENFORCE_SAFE_MODE | REPORT_ERRORS | STREAM_WILL_CAST, NULL)) == NULL)
    {
      RETURN_FALSE;
    }

  if (php_stream_cast (stream, PHP_STREAM_AS_STDIO, (void **)&fp, 1) == FAILURE)
    {
      php_stream_close (stream);
      RETURN_FALSE;
    }

  yaml_emitter_initialize (&emitter);
  yaml_emitter_set_output_file (&emitter, fp);
  yaml_emitter_set_unicode (&emitter, 1);
  yaml_emitter_set_canonical (&emitter, 0);
  yaml_emitter_set_encoding (&emitter, encoding);
  yaml_emitter_set_break (&emitter, linebreak);

  if (YAML_G (nomnom))
    yaml_emitter_set_width (&emitter, -1);
  else
    yaml_emitter_set_width (&emitter, YAML_G (fill_column));

  RETVAL_BOOL ((php_yaml_write_impl (&emitter, data, encoding TSRMLS_CC) == SUCCESS));

  yaml_emitter_delete (&emitter);
  php_stream_close (stream);
}
/* }}} yaml_emit_file */

/*
 * Local variables:
 * tab-width: 2
 * indent-tabs-mode: nil
 * End:
 */

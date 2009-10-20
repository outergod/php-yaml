/**
 * YAML emitter binding part implementation
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
 * @author      Alexander Kahl <e-user@gmx.net>
 * @copyright   2008 Alexander Kahl
 * @license     http://www.gnu.org/licenses/lgpl.html  LGPLv3+
 * @version     $Id: emitter.c 27 2008-09-22 20:55:55Z e-user $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <yaml.h>
#include <ext/standard/php_smart_str.h>
#include <ext/standard/php_var.h>
#include "php_yaml.h"
#include "zval_refcount.h" /* for PHP < 5.3 */
#include "emitter.h"

/* {{{ internal function prototypes */
static void
php_yaml_print_emitter_error (yaml_emitter_t *emitter TSRMLS_DC);

static int
php_yaml_determine_array_type (HashTable *myht TSRMLS_DC);

static int
php_yaml_determine_array_depth (HashTable *table TSRMLS_DC);
     
static int
php_yaml_mangle_queue (zval *data, yaml_parser_t *parser,
                       yaml_emitter_t *emitter TSRMLS_DC);
/* }}} */

static int
php_yaml_determine_array_type (HashTable *table TSRMLS_DC)
{
  if (!table)
    return Y_ARRAY_IS_LIST;
        
  unsigned int i = zend_hash_num_elements (table);
  char *key = NULL;
  uint key_len;
  HashPosition pos;
  ulong index, idx = 0;

  zend_hash_internal_pointer_reset_ex (table, &pos);

  for (;; zend_hash_move_forward_ex (table, &pos))
    {
      i = zend_hash_get_current_key_ex (table, &key, &key_len, &index, 0, &pos);

      if (i == HASH_KEY_NON_EXISTANT)
        break;

      if (i == HASH_KEY_IS_STRING)
        return Y_ARRAY_IS_HASH;
      else
        {
          if (index != idx) 
            return Y_ARRAY_IS_HASH;
        }
        
      idx++;
    }

  return Y_ARRAY_IS_LIST;
}

static int
php_yaml_determine_array_depth (HashTable *table TSRMLS_DC)
{
  if (table->nNumOfElements > 6) /* TODO: Make configurable */
    return 1;

  for (zend_hash_internal_pointer_reset (table);
       zend_hash_has_more_elements (table) == SUCCESS;
       zend_hash_move_forward (table))
    {
      zval **ppzval = NULL;
      zend_hash_get_current_data (table, (void **)&ppzval);

      if (Z_TYPE_PP (ppzval) == IS_OBJECT || Z_TYPE_PP (ppzval) == IS_ARRAY)
        return 1; /* TODO: Check ArrayAccess / __toString () */
    }

  return 0;
}

/* {{{ php_yaml_emitter_error () */
static void
php_yaml_print_emitter_error (yaml_emitter_t *emitter TSRMLS_DC)
{
  switch (emitter->error) /* TODO: Throw exceptions if configured */
    {
    case YAML_MEMORY_ERROR:
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
                        "Memory error: Not enough memory for emitting");
      break;
            
    case YAML_WRITER_ERROR:
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
                        "Writer error: %s\n", emitter->problem);
      break;
            
    case YAML_EMITTER_ERROR:
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
                        "Emitter error: %s\n", emitter->problem);
      break;
            
    default:
      /* Couldn't happen. */
      php_error_docref (NULL TSRMLS_CC, E_ERROR,
                        "Internal error");
      break;
    }
}
/* }}} */

/* {{{ php_yaml_write_impl () */
int
php_yaml_write_impl (yaml_emitter_t *emitter, zval *data, long encoding TSRMLS_DC)
{
  yaml_parser_t parser;
  yaml_event_t event;
    
  if (!yaml_stream_start_event_initialize (&event, (int) encoding))
    goto emitter_error;
  if (!yaml_emitter_emit (emitter, &event))
    goto emitter_error;
    
  if (!yaml_document_start_event_initialize (&event,
                                             NULL, NULL, NULL, 0))
    goto emitter_error;
  if (!yaml_emitter_emit (emitter, &event))
    goto emitter_error;
    
  yaml_parser_initialize (&parser);
    
  if (php_yaml_mangle_queue (data, &parser, emitter TSRMLS_CC) == FAILURE)
    goto emitter_error;

  if (!yaml_document_end_event_initialize (&event, 0))
    goto emitter_error;
  if (!yaml_emitter_emit (emitter, &event))
    goto emitter_error;
    
  if (!yaml_stream_end_event_initialize (&event))
    goto emitter_error;
  if (!yaml_emitter_emit (emitter, &event))
    goto emitter_error;

  yaml_event_delete (&event);
  yaml_parser_delete (&parser);
  return SUCCESS;

 emitter_error:
  php_yaml_print_emitter_error (emitter TSRMLS_CC);
  yaml_event_delete (&event);
  yaml_parser_delete (&parser);
  return FAILURE;
}
/* }}} */

static int
php_yaml_mangle_queue (zval *data, yaml_parser_t *parser,
                       yaml_emitter_t *emitter TSRMLS_DC)
{
  yaml_event_t event;

  if (Z_TYPE_P (data) == IS_ARRAY)
    {
      HashTable *table = Z_ARRVAL_P (data);
      HashPosition pointer;
      zval **hash_data;
      char *key;
      unsigned int key_len;
      unsigned long index;
      yaml_mapping_style_t style;

      if (php_yaml_determine_array_type (table TSRMLS_CC) == Y_ARRAY_IS_HASH)
        {
          if (YAML_G (nomnom))
            style = YAML_FLOW_MAPPING_STYLE;
          else
            {
              style =
                php_yaml_determine_array_depth (table TSRMLS_CC)
                ? YAML_BLOCK_MAPPING_STYLE
                : YAML_FLOW_MAPPING_STYLE;
            }

          if (!yaml_mapping_start_event_initialize (&event, NULL,
                                                    (yaml_char_t *)YAML_MAP_TAG,
                                                    1, style))
            return FAILURE;

          if (!yaml_emitter_emit (emitter, &event))
            return FAILURE;

          for (zend_hash_internal_pointer_reset_ex (table, &pointer);
               zend_hash_get_current_data_ex (table, (void**) &hash_data, &pointer) == SUCCESS;
               zend_hash_move_forward_ex (table, &pointer))
            {
              if (zend_hash_get_current_key_ex (table,
                                                &key, &key_len, &index, 0, &pointer)
                  == HASH_KEY_IS_STRING)
                {
                  if (!yaml_scalar_event_initialize (&event, NULL,
                                                     (yaml_char_t *)YAML_STR_TAG,
                                                     (yaml_char_t *)key, key_len - 1, 1, 1,
                                                     YAML_PLAIN_SCALAR_STYLE))
                    return FAILURE;
                }
              else
                {
                  key_len = index ? (int)(log (index) / log (10)) + 2 : 2;
                  key = (char *)emalloc (key_len);
                  snprintf (key, key_len, "%ld", index);

                  if (!yaml_scalar_event_initialize (&event, NULL,
                                                     (yaml_char_t *)YAML_INT_TAG,
                                                     (yaml_char_t *)key, key_len - 1, 1, 1,
                                                     YAML_PLAIN_SCALAR_STYLE))
                    {
                      efree (key);
                      return FAILURE;
                    }

                  efree (key);
                }

              if (!yaml_emitter_emit (emitter, &event))
                return FAILURE;
                   
              if (php_yaml_mangle_queue (*hash_data, parser, emitter TSRMLS_CC) == FAILURE)
                return FAILURE;
            }

          if (!yaml_mapping_end_event_initialize (&event))
            return FAILURE;
            
          if (!yaml_emitter_emit (emitter, &event))
            return FAILURE;
        }
      else /* Y_ARRAY_IS_LIST */
        {
          if (YAML_G (nomnom))
            style = YAML_FLOW_SEQUENCE_STYLE;
          else
            {
              style =
                php_yaml_determine_array_depth (table TSRMLS_CC)
                ? YAML_BLOCK_SEQUENCE_STYLE
                : YAML_FLOW_SEQUENCE_STYLE;
            }
                    
          if (!yaml_sequence_start_event_initialize (&event, NULL,
                                                     (yaml_char_t *)YAML_SEQ_TAG,
                                                     1, style))
            return FAILURE;

          if (!yaml_emitter_emit (emitter, &event))
            return FAILURE;

          for (zend_hash_internal_pointer_reset_ex (table, &pointer);
               zend_hash_get_current_data_ex (table, (void**) &hash_data, &pointer) == SUCCESS;
               zend_hash_move_forward_ex (table, &pointer))
            {                    
              if (php_yaml_mangle_queue (*hash_data, parser, emitter TSRMLS_CC) == FAILURE)
                return FAILURE;                            
            }
                    
          if (!yaml_sequence_end_event_initialize (&event))
            return FAILURE;
            
          if (!yaml_emitter_emit (emitter, &event))
            return FAILURE;
        }
  

      return SUCCESS;
    }

  char *input;
  int input_len;
  zval temp;

  switch (Z_TYPE_P (data))
    {
#ifdef IS_UNICODE
    case IS_UNICODE:
#endif            
    case IS_STRING:
      input = Z_STRVAL_P (data);
      input_len = Z_STRLEN_P (data);

      if (!yaml_scalar_event_initialize (&event, NULL,
                                         (yaml_char_t *)YAML_STR_TAG,
                                         (yaml_char_t *)input, input_len, 1, 1,
                                         YAML_PLAIN_SCALAR_STYLE))
        return FAILURE;

      if (!yaml_emitter_emit (emitter, &event))
        return FAILURE;
      break;

    case IS_NULL:
      if (!yaml_scalar_event_initialize (&event, NULL,
                                         (yaml_char_t *)YAML_NULL_TAG,
                                         (yaml_char_t *)"~", -1, 1, 1,
                                         YAML_PLAIN_SCALAR_STYLE))
        return FAILURE;

      if (!yaml_emitter_emit (emitter, &event))
        return FAILURE;
      break;
            
    case IS_DOUBLE:
      temp = *data;
      zval_copy_ctor(&temp);
      convert_to_string(&temp);
            
      input = Z_STRVAL (temp);
      input_len = Z_STRLEN (temp);

      if (!yaml_scalar_event_initialize (&event, NULL,
                                         (yaml_char_t *)YAML_FLOAT_TAG,
                                         (yaml_char_t *)input, input_len, 1, 1,
                                         YAML_PLAIN_SCALAR_STYLE))
        {
          zval_dtor(&temp);
          return FAILURE;
        }

      if (!yaml_emitter_emit (emitter, &event))
        {
          zval_dtor(&temp);
          return FAILURE;
        }

      zval_dtor(&temp);
      break;

    case IS_BOOL:
      if (!yaml_scalar_event_initialize (&event, NULL, (yaml_char_t *)YAML_BOOL_TAG,
                                         Z_LVAL_P (data)
                                         ? (yaml_char_t *)"true"
                                         : (yaml_char_t *)"false",
                                         -1, 1, 1,
                                         YAML_PLAIN_SCALAR_STYLE))
        return FAILURE;

      if (!yaml_emitter_emit (emitter, &event))
        return FAILURE;

      break;

    case IS_LONG:
      temp = *data;
      zval_copy_ctor(&temp);
      convert_to_string(&temp);
            
      input = Z_STRVAL (temp);
      input_len = Z_STRLEN (temp);

      if (!yaml_scalar_event_initialize (&event, NULL,
                                         (yaml_char_t *)YAML_INT_TAG,
                                         (yaml_char_t *)input, input_len, 1, 1,
                                         YAML_PLAIN_SCALAR_STYLE))
        {
          zval_dtor(&temp);
          return FAILURE;
        }

      if (!yaml_emitter_emit (emitter, &event))
        {
          zval_dtor(&temp);
          return FAILURE;
        }

      zval_dtor(&temp);
      break;
            
    case IS_RESOURCE:
    case IS_OBJECT:
    default:
      /* TODO: Check __toString () / DateTime etc. */
      php_error_docref (NULL TSRMLS_CC, E_WARNING, "Not implemented Yet");
      return FAILURE;
    }

  return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 2
 * indent-tabs-mode: nil
 * End:
 */

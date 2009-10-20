/**
 * YAML parser and emitter PHP extension
 *
 * Copyright (C)  2007 Ryusuke SEKIYAMA. All rights reserved.
 * Copyright (C)  2008 Alexander Kahl
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
 * @version     $Id: php_yaml.h 26 2008-09-21 19:40:42Z e-user $
 */

#ifndef PHP_YAML_H
#define PHP_YAML_H

#include <Zend/zend_extensions.h>

#define PHP_YAML_EXTNAME "yaml"
#define PHP_YAML_VERSION "0.4.0"

#ifdef ZTS
#include "TSRM.h"
#endif

/* {{{ module globals */
ZEND_BEGIN_MODULE_GLOBALS (yaml)
	zend_bool decode_binary;
	long decode_timestamp;
	zval *timestamp_decoder;
    zend_bool throw_exceptions;
	long fill_column;
    zend_bool nomnom;
#ifdef IS_UNICODE
	UConverter *orig_runtime_encoding_conv;
#endif
ZEND_END_MODULE_GLOBALS (yaml)

ZEND_DECLARE_MODULE_GLOBALS (yaml)

#ifdef ZTS
#define YAML_G(v) TSRMG (yaml_globals_id, zend_yaml_globals *, v)
#else
#define YAML_G(v) (yaml_globals.v)
#endif
/* }}} */

/* {{{ module function prototypes */
PHP_MINIT_FUNCTION (yaml);
PHP_MSHUTDOWN_FUNCTION (yaml);
PHP_MINFO_FUNCTION (yaml);

#if ZEND_EXTENSION_API_NO < 220060519
#define PHP_GINIT_FUNCTION (yaml) \
	void php_yaml_init_globals (zend_yaml_globals *yaml_globals)
#endif

PHP_GINIT_FUNCTION (yaml);
/* }}} */

/* {{{ PHP function prototypes */
PHP_FUNCTION (yaml_parse);
PHP_FUNCTION (yaml_parse_file);
PHP_FUNCTION (yaml_parse_url);
PHP_FUNCTION (yaml_emit);
PHP_FUNCTION (yaml_emit_file);
/* }}} */

typedef zval* (*eval_scalar_func_t)(yaml_event_t event, HashTable *callbacks TSRMLS_DC);

extern zend_module_entry yaml_module_entry;
#define phpext_yaml_ptr &yaml_module_entry;

#endif /* PHP_YAML_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

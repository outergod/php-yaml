#ifndef PARSER_H
#define PARSER_H

#define Y_PARSER_CONTINUE 0
#define Y_PARSER_SUCCESS  1
#define Y_PARSER_FAILURE -1

#define Y_FILTER_NONE     0
#define Y_FILTER_SUCCESS  1
#define Y_FILTER_FAILURE -1

#define Y_SCALAR_IS_NOT_NUMERIC 0x00
#define Y_SCALAR_IS_INT         0x10
#define Y_SCALAR_IS_FLOAT       0x20
#define Y_SCALAR_IS_ZERO        0x00
#define Y_SCALAR_IS_BINARY      0x01
#define Y_SCALAR_IS_OCTAL       0x02
#define Y_SCALAR_IS_DECIMAL     0x03
#define Y_SCALAR_IS_HEXADECIMAL 0x04
#define Y_SCALAR_IS_SEXAGECIMAL 0x05
#define Y_SCALAR_IS_INFINITY_P  0x06
#define Y_SCALAR_IS_INFINITY_N  0x07
#define Y_SCALAR_IS_NAN         0x08
#define Y_SCALAR_FORMAT_MASK    0x0F

zval *
php_yaml_read_impl(yaml_parser_t *parser, yaml_event_t *parent,
		zval *aliases, zval *zv, long *ndocs,
		eval_scalar_func_t eval_func, HashTable *callbacks TSRMLS_DC);

#define php_yaml_read_all(parser, ndocs, eval_func, callbacks) \
	php_yaml_read_impl((parser), NULL, NULL, NULL, (ndocs), (eval_func), (callbacks) TSRMLS_CC)

zval *
php_yaml_read_partial(yaml_parser_t *parser, long pos, long *ndocs,
		eval_scalar_func_t eval_func, HashTable *callbacks TSRMLS_DC);

zval *
php_yaml_eval_scalar(yaml_event_t event, HashTable *callbacks TSRMLS_DC);

zval *
php_yaml_eval_scalar_with_callbacks(yaml_event_t event, HashTable *callbacks TSRMLS_DC);

int
php_yaml_check_callbacks(HashTable *callbacks TSRMLS_DC);

#endif

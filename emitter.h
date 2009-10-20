#ifndef EMITTER_H
#define EMITTER_H

#define Y_ARRAY_IS_LIST         0
#define Y_ARRAY_IS_HASH         1

int
php_yaml_write_impl(yaml_emitter_t *emitter, zval *data, long encoding TSRMLS_DC);

#endif

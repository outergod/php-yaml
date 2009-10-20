<?php

$scalar1 = 'ä';
/* $scalar1 = 'a'; */

$output = yaml_emit ($scalar1);
echo $output ?: "ERROR";
echo PHP_EOL;

